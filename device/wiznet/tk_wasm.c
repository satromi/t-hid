/*
 *----------------------------------------------------------------------
 *    tk_wasm — WebAssembly (wasm3) ランタイム実装
 *
 *    状態機械:
 *      EMPTY --begin--> LOADING --chunk*--> LOADED --run--> RUNNING
 *                                                               |
 *                                  ← stop/完了 ← STOPPED ← task_exit
 *      LOADING/LOADED --abort--> EMPTY
 *
 *    メモリ:
 *      bytecode    : Kmalloc (begin 時確保、stop/abort で free)
 *      runtime     : m3 内部 (calloc/free 経由)
 *      task stack  : μT-Kernel が管理
 *
 *    host function は tk_wasm_host.c が tk_wasm_register_host() で登録する
 *    (Step 4)。Step 3 時点ではプレースホルダとして呼出のみ予約。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "tk_wasm.h"
#include "wasm3.h"
#include "m3_env.h"

/* Flash I/O 関連 — tk_mcp_int.h は cJSON などを pull-in するため避け、
 * 必要な定数と関数だけここで再宣言する (サイズは tk_mcp_int.h と一致) */
#if defined(CPU_RP2040)
#define FLASH_STORAGE_OFFSET        (2 * 1024 * 1024 - 4096)
#define FLASH_STORAGE_BASE          (0x10000000u + FLASH_STORAGE_OFFSET)
#define FLASH_SECTOR_SIZE           4096
#define WASM_AUTOBOOT_FLASH_OFFSET  0x2C0
#define WASM_AUTOBOOT_MAGIC         0x31304157  /* "WA01" */
#define WASM_AUTOBOOT_RECORD_SIZE   32
extern void flash_safe_erase_program(UW offset, const UB *data, UW len);

/* Hard fault 復帰用: RP2040 watchdog SCRATCH0..3 は SYSRESETREQ 経由の reset で
 * 値が保持される特性を利用して fault 情報を伝達 */
#define WASM_FAULT_SCRATCH_BASE     0x4005800Cu
#define WASM_FAULT_MAGIC            0xDEADFA17u
#define AIRCR_ADDR                  0xE000ED0Cu
#define AIRCR_SYSRESETREQ           0x05FA0004u
#endif

/* HardFault handler から安全に読める「現在 wasm を実行中」フラグ。
 * tk_get_tid() や knl_ctxtsk 等のカーネル API を handler mode から呼ぶのは
 * 危険なので、wasm_run で立て wasm_task 終了で降ろす単純な volatile を使う。
 * naked 関数の inline asm から symbol 参照するため static (LOCAL) にしない */
EXPORT volatile ID  g_running_wasm_tskid = 0;
EXPORT volatile INT g_running_wasm_slot  = -1;
/* autoboot 経由で起動された wasm が実行中の slot (-1=該当なし)。
 * fault 時 autoboot 自動 disable の判断に使う */
EXPORT volatile INT g_autoboot_ram_slot  = -1;

/*----------------------------------------------------------------------
 * libc allocator の override (wasm3 は calloc/realloc/free を使う)
 *
 * wasm3 デフォルトの calloc 経路は newlib _sbrk → `end` シンボル 起点の
 * 領域を消費するが、μT-Kernel Kmalloc も同じ領域を管理するため衝突。
 * linker は strong symbol を優先するので、ここで calloc/realloc/free を
 * 定義すれば newlib 版が上書きされ Kmalloc ヒープが共有される。
 *
 * 他のコード (newlib 内部、cJSON 等) が calloc/free を使っても安全。
 */
extern void *Kmalloc(size_t size);
extern void *Kcalloc(size_t nmemb, size_t size);
extern void *Krealloc(void *ptr, size_t size);

EXPORT void *calloc(size_t nmemb, size_t size)
{
    return Kcalloc(nmemb, size);
}

EXPORT void *malloc(size_t size)
{
    return Kmalloc(size);
}

EXPORT void free(void *ptr)
{
    if (ptr != NULL) Kfree(ptr);
}

EXPORT void *realloc(void *ptr, size_t size)
{
    return Krealloc(ptr, size);
}

/* tk_sha256.c — 既存の純 C 実装 */
#include "tk_sha256.h"
LOCAL void sha256_compute(const UB *data, UW len, UB out[32])
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

/* Step 4 で実装: host functions を runtime にリンク */
extern M3Result tk_wasm_register_host(IM3Runtime runtime, IM3Module module);
/* Weak stub: host 登録が未実装でも link を通す */
__attribute__((weak)) M3Result tk_wasm_register_host(IM3Runtime runtime, IM3Module module)
{
    (void)runtime; (void)module;
    return NULL;  /* 成功 (no functions linked) */
}

/*----------------------------------------------------------------------
 * 内部状態
 */
typedef struct {
    BOOL        in_use;
    UB          state;
    UB         *bytecode;           /* Kmalloc alloc、size byte */
    UW          size;
    UW          received;
    UB          expected_sha256[32];
    UW          memory_kb;
    IM3Runtime  runtime;
    IM3Module   module;
    ID          tskid;
    char        last_error[64];
    char        entry_name[32];     /* cJSON 文字列は tool handler 返却時に free
                                     * されるため slot 内に copy して保持 */
    INT         slot_index;         /* self index for task exinf */
} T_WASM_SLOT;

LOCAL IM3Environment  wasm_env = NULL;
LOCAL T_WASM_SLOT     wasm_slots[WASM_MAX_SLOT];

#define W5DBG(fmt, ...)  tm_printf((UB *)(fmt), ##__VA_ARGS__)

LOCAL void slot_reset(T_WASM_SLOT *s)
{
    /* wasm_task が tk_ext_tsk した後の DMT 状態 TCB を確実に解放。
     * 以前は tk_del_tsk せず単に tskid=0 にしていたため TCB leak → 数回実行で
     * tk_cre_tsk が E_LIMIT で失敗していた */
    if (s->tskid > 0) {
        T_RTSK rtsk;
        if (tk_ref_tsk(s->tskid, &rtsk) == E_OK) {
            if (rtsk.tskstat != TTS_DMT) tk_ter_tsk(s->tskid);
            tk_del_tsk(s->tskid);
        }
        s->tskid = 0;
    }
    if (s->runtime != NULL) {
        m3_FreeRuntime(s->runtime);
        s->runtime = NULL;
        s->module  = NULL;
    }
    if (s->bytecode != NULL) {
        Kfree(s->bytecode);
        s->bytecode = NULL;
    }
    s->in_use        = FALSE;
    s->state         = WASM_STATE_EMPTY;
    s->size          = 0;
    s->received      = 0;
    s->memory_kb     = 0;
    s->last_error[0] = '\0';
    memset(s->expected_sha256, 0, 32);
}

LOCAL void slot_set_error(T_WASM_SLOT *s, const char *msg)
{
    UW n = 0;
    while (msg[n] && n < sizeof(s->last_error) - 1) {
        s->last_error[n] = msg[n]; n++;
    }
    s->last_error[n] = '\0';
    s->state = WASM_STATE_ERROR;
}

/*----------------------------------------------------------------------
 * WASM タスク本体 — 指定の export 関数を呼ぶ
 *
 * 引数:
 *   stacd = slot index
 *   exinf = entry name char* (static)
 */
LOCAL void wasm_task(INT stacd, void *exinf)
{
    INT slot = stacd;
    (void)exinf;   /* entry は s->entry_name から読む (cJSON の dangling 回避) */
    T_WASM_SLOT *s = &wasm_slots[slot];
    const char *entry = s->entry_name;

    IM3Function fn = NULL;
    M3Result r = m3_FindFunction(&fn, s->runtime, entry);
    if (r != NULL) {
        slot_set_error(s, r);
        W5DBG("wasm[%d]: entry '%s' not found: %s\n", slot, entry, r);
        goto done;
    }
    W5DBG("wasm[%d]: calling %s...\n", slot, entry);

    r = m3_CallV(fn);   /* 引数なしで呼出 (void(void) を想定) */

    BOOL trapped = FALSE;
    if (r != NULL && r != m3Err_trapExit) {
        slot_set_error(s, r);
        W5DBG("wasm[%d]: trap: %s\n", slot, r);
        trapped = TRUE;
    } else {
        W5DBG("wasm[%d]: returned\n", slot);
    }

done:
    /* 正常終了したら runtime を即 free して heap を返す。
     * trap 後は m3 内部状態 (compile cache / stack frame 等) が中途状態で、
     * m3_FreeRuntime 中に二次 fault を起こす実績があるため解放を保留。
     * heap は食うが、次回 wasm_stop / wasm_begin で slot_reset を通せば
     * 同じく free される */
    if (!trapped && s->runtime != NULL) {
        m3_FreeRuntime(s->runtime);
        s->runtime = NULL;
        s->module  = NULL;
    }
    s->state = WASM_STATE_STOPPED;
    /* HardFault recovery 用フラグ降ろし (正常終了) */
    if (g_autoboot_ram_slot == slot) g_autoboot_ram_slot = -1;
    g_running_wasm_tskid = 0;
    g_running_wasm_slot  = -1;
    /* s->tskid はこのあとも保持。tk_ext_tsk で DMT 状態になる TCB を、
     * slot_reset (wasm_stop / wasm_begin 経由) で tk_del_tsk してもらうため。
     * 以前ここで 0 クリアしていたため TCB が GC されず E_LIMIT になっていた */
    tk_ext_tsk();
}

/*======================================================================
 * 公開 API
 *====================================================================*/

/*----------------------------------------------------------------------
 * HardFault recovery (RP2040): wasm 起因の fault で system hang させず
 * soft reset → 次回起動時に autoboot を disable して自動復帰する
 */
#if defined(CPU_RP2040)
LOCAL void wasm_check_prior_fault(void)
{
    volatile UW *scratch = (volatile UW *)WASM_FAULT_SCRATCH_BASE;
    if (scratch[0] != WASM_FAULT_MAGIC) return;    /* 正常起動 */

    UW  prev_tskid      = scratch[1];
    INT prev_slot       = (INT)scratch[2];
    INT prev_autoboot   = (INT)scratch[3];
    scratch[0] = 0;                                /* magic クリア */

    tm_printf((UB *)"WASM: recovered from hard fault (ram_slot=%d tskid=%u autoboot_was=%d)\n",
        prev_slot, (unsigned)prev_tskid, prev_autoboot);
    /* 犯人候補が autoboot ならそれを disable (brick 防止) */
    if (prev_autoboot >= 0) {
        (void)tk_wasm_autoboot_set(-1);
        tm_printf((UB *)"WASM: autoboot disabled (was flash_slot=%d) - re-enable manually if safe\n",
            prev_autoboot);
    }
}
#endif

EXPORT ER tk_wasm_init(void)
{
    if (wasm_env != NULL) return E_OK;    /* 既初期化 */
    wasm_env = m3_NewEnvironment();
    if (wasm_env == NULL) return E_NOMEM;
    INT i;
    for (i = 0; i < WASM_MAX_SLOT; i++) {
        memset(&wasm_slots[i], 0, sizeof(wasm_slots[i]));
        wasm_slots[i].slot_index = i;
    }
    W5DBG("wasm: runtime initialized (%d slots)\n", WASM_MAX_SLOT);
#if defined(CPU_RP2040)
    wasm_check_prior_fault();
#endif
    return E_OK;
}

/*----------------------------------------------------------------------
 * HardFault_Handler override: wasm task が fault したら task だけ殺して
 * system は走り続ける (reboot しない)。
 *
 * 仕組み (Cortex-M0+):
 *   1. 例外エントリで hw が r0-r3,r12,lr,pc,xpsr を stack に push
 *   2. naked handler で sp を取得し hard_fault_c へ渡す
 *   3. hard_fault_c で wasm 起因と判定したら、stacked pc を wasm_fault_exit
 *      に書き換え
 *   4. 関数 return → 例外 return で stacked pc から再開 → wasm_fault_exit が
 *      task 文脈で動き slot を error にして tk_ext_tsk
 *
 * wasm 以外の fault は従来どおり hang (安全側)
 */
#if defined(CPU_RP2040)

/* Forward decls */
void hard_fault_c(UW *frame);
void wasm_fault_exit(void);

/* naked: hw stacked frame の位置 (SP) をそのまま C 関数に渡す */
EXPORT __attribute__((naked)) void HardFault_Handler(void)
{
    asm volatile (
        "mov    r0, sp      \n"
        "b      hard_fault_c\n"
    );
}

/* frame[0..7] = R0, R1, R2, R3, R12, LR, PC, xPSR (stacked by exception entry) */
void hard_fault_c(UW *frame)
{
    if (g_running_wasm_tskid != 0
        && g_running_wasm_slot >= 0
        && g_running_wasm_slot < WASM_MAX_SLOT) {
        /* stacked PC を wasm_fault_exit に差替え。bit0=1 の Thumb state bit
         * は LSB が 1 の function pointer として自動的に立つ (Thumb シンボル) */
        frame[6] = (UW)wasm_fault_exit;
        /* frame[7] (xPSR) は現行のまま。Thumb bit (bit 24) は元から 1。
         * 関数 return 時の BX LR で EXC_RETURN が使われ例外復帰する */
        return;
    }

    /* 非 wasm の fault は従来動作 */
    tm_printf((UB *)"*** Hard fault ***  ctxtsk:%d (non-wasm)\n",
        g_running_wasm_tskid);
    while (1) { }
}

/* 例外復帰後に wasm task 文脈で実行される。自タスクを終了する */
void wasm_fault_exit(void)
{
    INT slot = g_running_wasm_slot;
    INT ab   = g_autoboot_ram_slot;
    tm_printf((UB *)"WASM: fault recovered — exiting slot %d (autoboot_was=%d)\n",
        slot, ab);

    if (slot >= 0 && slot < WASM_MAX_SLOT) {
        T_WASM_SLOT *s = &wasm_slots[slot];
        slot_set_error(s, "hard fault recovered");
        s->state = WASM_STATE_STOPPED;
        /* runtime はあえて free しない — 内部状態が壊れている可能性あり。
         * 次の wasm_stop / wasm_begin で slot_reset が掃除する */
    }
    g_running_wasm_tskid = 0;
    g_running_wasm_slot  = -1;

    /* autoboot 経由の起動が犯人なら自動 disable (brick 防止) */
    if (ab >= 0 && ab == slot) {
        (void)tk_wasm_autoboot_set(-1);
        tm_printf((UB *)"WASM: autoboot disabled (brick prevention)\n");
    }
    g_autoboot_ram_slot = -1;

    tk_ext_tsk();   /* wasm_task を終了 */
}
#endif

EXPORT ER tk_wasm_begin(INT slot, UW size, const UB sha256[32], UW memory_kb)
{
    if (wasm_env == NULL) return E_OBJ;
    if (slot < 0 || slot >= WASM_MAX_SLOT) return E_PAR;
    if (size == 0 || size > WASM_MAX_MODULE_SIZE) return E_PAR;
    if (sha256 == NULL) return E_PAR;

    T_WASM_SLOT *s = &wasm_slots[slot];
    if (s->in_use) {
        /* 前の module を掃除 (running ならエラー、それ以外は破棄して再開) */
        if (s->state == WASM_STATE_RUNNING) return E_OBJ;
        slot_reset(s);
    }

    s->bytecode = (UB *)Kmalloc(size);
    if (s->bytecode == NULL) return E_NOMEM;

    s->in_use     = TRUE;
    s->state      = WASM_STATE_LOADING;
    s->size       = size;
    s->received   = 0;
    s->memory_kb  = (memory_kb > 0) ? memory_kb : WASM_DEFAULT_MEMORY_KB;
    memcpy(s->expected_sha256, sha256, 32);

    return E_OK;
}

EXPORT ER tk_wasm_chunk(INT slot, UW offset, const UB *data, UW len)
{
    if (slot < 0 || slot >= WASM_MAX_SLOT) return E_PAR;
    T_WASM_SLOT *s = &wasm_slots[slot];
    if (!s->in_use || s->state != WASM_STATE_LOADING) return E_OBJ;
    if (offset + len > s->size) return E_PAR;
    if (data == NULL || len == 0) return E_PAR;

    memcpy(s->bytecode + offset, data, len);
    if (offset + len > s->received) s->received = offset + len;

    if (s->received >= s->size) {
        s->state = WASM_STATE_LOADED;
    }
    return E_OK;
}

EXPORT ER tk_wasm_run(INT slot, const char *entry)
{
    if (wasm_env == NULL) return E_OBJ;
    if (slot < 0 || slot >= WASM_MAX_SLOT) return E_PAR;
    T_WASM_SLOT *s = &wasm_slots[slot];
    if (!s->in_use || s->state != WASM_STATE_LOADED) return E_OBJ;

    /* SHA-256 検証 (ゼロハッシュ = スキップ) */
    BOOL zero_hash = TRUE;
    INT i;
    for (i = 0; i < 32; i++) if (s->expected_sha256[i]) { zero_hash = FALSE; break; }
    if (!zero_hash) {
        UB calc[32];
        sha256_compute(s->bytecode, s->size, calc);
        for (i = 0; i < 32; i++) {
            if (calc[i] != s->expected_sha256[i]) {
                slot_set_error(s, "sha256 mismatch");
                return E_IO;
            }
        }
    }

    /* wasm runtime + module を生成 */
    s->runtime = m3_NewRuntime(wasm_env, s->memory_kb * 1024, (void *)s);
    if (s->runtime == NULL) {
        slot_set_error(s, "NewRuntime failed");
        return E_NOMEM;
    }

    M3Result r = m3_ParseModule(wasm_env, &s->module, s->bytecode, s->size);
    if (r != NULL) {
        slot_set_error(s, r);
        m3_FreeRuntime(s->runtime); s->runtime = NULL;
        return E_IO;
    }

    r = m3_LoadModule(s->runtime, s->module);
    if (r != NULL) {
        slot_set_error(s, r);
        m3_FreeRuntime(s->runtime); s->runtime = NULL;
        return E_IO;
    }

    /* host function 登録 (weak stub なら何もしない) */
    r = tk_wasm_register_host(s->runtime, s->module);
    if (r != NULL) {
        slot_set_error(s, r);
        m3_FreeRuntime(s->runtime); s->runtime = NULL;
        return E_IO;
    }

    /* エントリ名を slot にコピー (cJSON 内部の valuestring が tool handler 返却
     * 直後に freed される対策) */
    {
        const char *src = (entry != NULL && entry[0]) ? entry : "run";
        UW k = 0;
        while (src[k] && k < sizeof(s->entry_name) - 1) {
            s->entry_name[k] = src[k]; k++;
        }
        s->entry_name[k] = '\0';
    }

    /* 専用タスク起動 */
    T_CTSK ctsk;
    ctsk.exinf   = NULL;
    ctsk.tskatr  = TA_HLNG | TA_RNG3;
    ctsk.task    = (FP)wasm_task;
    ctsk.itskpri = WASM_TASK_PRI;
    ctsk.stksz   = WASM_TASK_STKSZ;
    ctsk.bufptr  = NULL;
    ID tid = tk_cre_tsk(&ctsk);
    if (tid < 0) {
        slot_set_error(s, "tk_cre_tsk failed");
        m3_FreeRuntime(s->runtime); s->runtime = NULL;
        return E_LIMIT;
    }
    s->tskid = tid;
    s->state = WASM_STATE_RUNNING;
    /* HardFault recovery 用に現在の wasm 実行情報を記録 */
    g_running_wasm_tskid = tid;
    g_running_wasm_slot  = slot;
    tk_sta_tsk(tid, slot);
    return E_OK;
}

/*----------------------------------------------------------------------
 * 受信済みモジュールのバイト列を引き取る
 *
 * SHA-256 を検証し、bytecode の所有権を呼び出し側へ移して slot を空にする。
 * 呼び出し側は使用後に Kfree すること。専用タスクではなく別の実行形態
 * (キー処理のコールバック等) でモジュールを使うときに用いる。
 */
EXPORT ER tk_wasm_take_bytecode(INT slot, UB **pbuf, UW *psize)
{
    if (pbuf == NULL || psize == NULL) return E_PAR;
    if (slot < 0 || slot >= WASM_MAX_SLOT) return E_PAR;
    T_WASM_SLOT *s = &wasm_slots[slot];
    if (!s->in_use || s->state != WASM_STATE_LOADED) return E_OBJ;

    BOOL zero_hash = TRUE;
    INT i;
    for (i = 0; i < 32; i++) if (s->expected_sha256[i]) { zero_hash = FALSE; break; }
    if (!zero_hash) {
        UB calc[32];
        sha256_compute(s->bytecode, s->size, calc);
        if (memcmp(calc, s->expected_sha256, 32) != 0) {
            slot_set_error(s, "sha256 mismatch");
            return E_IO;
        }
    }

    *pbuf  = s->bytecode;
    *psize = s->size;
    s->bytecode = NULL;
    slot_reset(s);
    return E_OK;
}

EXPORT ER tk_wasm_stop(INT slot)
{
    if (slot < 0 || slot >= WASM_MAX_SLOT) return E_PAR;
    T_WASM_SLOT *s = &wasm_slots[slot];
    if (!s->in_use) return E_OBJ;

    if (s->tskid > 0) {
        /* 実行中 task を強制終了 */
        T_RTSK rtsk;
        if (tk_ref_tsk(s->tskid, &rtsk) == E_OK) {
            if (rtsk.tskstat != TTS_DMT) tk_ter_tsk(s->tskid);
            tk_del_tsk(s->tskid);
        }
        s->tskid = 0;
    }
    slot_reset(s);
    return E_OK;
}

EXPORT ER tk_wasm_abort(INT slot)
{
    if (slot < 0 || slot >= WASM_MAX_SLOT) return E_PAR;
    T_WASM_SLOT *s = &wasm_slots[slot];
    if (!s->in_use) return E_OK;
    if (s->state == WASM_STATE_RUNNING) return E_OBJ;    /* run 中は stop を使え */
    slot_reset(s);
    return E_OK;
}

EXPORT void tk_wasm_get_info(INT slot, T_WASM_SLOT_INFO *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (slot < 0 || slot >= WASM_MAX_SLOT) return;
    T_WASM_SLOT *s = &wasm_slots[slot];
    out->state          = s->state;
    out->expected_size  = s->size;
    out->received_bytes = s->received;
    memcpy(out->expected_sha256, s->expected_sha256, 32);
    out->memory_kb      = s->memory_kb;
    out->tskid          = s->tskid;
    UW n = 0;
    while (s->last_error[n] && n < sizeof(out->last_error) - 1) {
        out->last_error[n] = s->last_error[n]; n++;
    }
    out->last_error[n] = '\0';
}

/*======================================================================
 * Flash 永続化 (RP2040 限定; STM32H5 は E_NOSPT)
 *====================================================================*/
#if defined(CPU_RP2040)

/* slot 先頭 96 バイトに置くヘッダ。内訳: 24 (u32×6) + 32 (sha) + 32 (name) +
 * 4 (crc) + 4 (reserved) = 96 B */
typedef struct __attribute__((packed)) {
    UW  magic;              /* WASM_FLASH_HEADER_MAGIC "WF01" */
    UW  version;            /* 1 */
    UW  size;               /* body bytes (0..WASM_FLASH_MAX_BODY) */
    UW  memory_kb;
    UW  priority;           /* 予約 (現状 WASM_TASK_PRI 固定) */
    UW  flags;              /* WASM_STORE_FLAG_* */
    UB  sha256[32];
    char name[32];
    UW  header_crc32;       /* magic..name までの CRC32 */
    UW  reserved;
} T_WASM_FLASH_HEADER;
_Static_assert(sizeof(T_WASM_FLASH_HEADER) == WASM_FLASH_HEADER_SIZE,
               "T_WASM_FLASH_HEADER must be exactly WASM_FLASH_HEADER_SIZE bytes");

/* FLASH_STORAGE セクタ内 autoboot レコード (32 B) */
typedef struct __attribute__((packed)) {
    UW  magic;              /* WASM_AUTOBOOT_MAGIC "WA01" */
    UW  version;            /* 1 */
    INT boot_slot;          /* -1 = disabled */
    UW  flags;              /* 将来拡張 */
    UW  crc32;              /* magic..flags の CRC32 */
    UW  reserved[3];
} T_WASM_AUTOBOOT_REC;
_Static_assert(sizeof(T_WASM_AUTOBOOT_REC) == WASM_AUTOBOOT_RECORD_SIZE,
               "T_WASM_AUTOBOOT_REC must be exactly 32 bytes");

/* wifi_creds と同じ bitwise CRC32 実装 (テーブル不要、小サイズ優先) */
LOCAL UW wasm_crc32(const UB *data, UW len)
{
    UW crc = 0xFFFFFFFFu;
    UW i;
    for (i = 0; i < len; i++) {
        crc ^= (UW)data[i];
        INT b;
        for (b = 0; b < 8; b++) {
            UW mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

LOCAL UW u_min(UW a, UW b) { return a < b ? a : b; }
LOCAL UW u_max(UW a, UW b) { return a > b ? a : b; }

/*----------------------------------------------------------------------
 * 1 slot (32KB = 8 sector) を 1 sector ずつ書き込む。
 * 各 sector の間で PRIMASK を復帰 → MQTT/USB IRQ に応答機会を与える
 */
LOCAL ER wasm_flash_write_slot(INT flash_slot,
                               const T_WASM_FLASH_HEADER *hdr,
                               const UB *body, UW body_size)
{
    if (flash_slot < 0 || flash_slot >= WASM_FLASH_SLOTS) return E_PAR;
    if (body_size > WASM_FLASH_MAX_BODY) return E_PAR;

    UW slot_base = WASM_FLASH_SLOT_OFFSET(flash_slot);
    UW body_start = WASM_FLASH_HEADER_SIZE;
    UW body_end   = body_start + body_size;

    INT sec;
    for (sec = 0; sec < (INT)(WASM_FLASH_SLOT_SIZE / FLASH_SECTOR_SIZE); sec++) {
        UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
        if (sector == NULL) return E_NOMEM;
        memset(sector, 0xFF, FLASH_SECTOR_SIZE);

        UW sec_off = (UW)sec * FLASH_SECTOR_SIZE;    /* slot 内 offset */
        UW sec_end = sec_off + FLASH_SECTOR_SIZE;

        /* header 部 (slot offset 0..95) と重なる部分 */
        if (sec_off < WASM_FLASH_HEADER_SIZE) {
            UW copy_start = sec_off;
            UW copy_end   = u_min(WASM_FLASH_HEADER_SIZE, sec_end);
            memcpy(sector + (copy_start - sec_off),
                   ((const UB *)hdr) + copy_start,
                   copy_end - copy_start);
        }
        /* body 部 (slot offset 96..96+body_size) と重なる部分 */
        if (body_size > 0) {
            UW ov_start = u_max(sec_off, body_start);
            UW ov_end   = u_min(sec_end, body_end);
            if (ov_start < ov_end) {
                memcpy(sector + (ov_start - sec_off),
                       body + (ov_start - body_start),
                       ov_end - ov_start);
            }
        }

        UINT imask;
        Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(slot_base + sec_off, sector, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask));

        Kfree(sector);
    }
    return E_OK;
}

/* 1 slot 全域 (32KB) を erase のみ (body なし) */
LOCAL ER wasm_flash_erase_slot(INT flash_slot)
{
    if (flash_slot < 0 || flash_slot >= WASM_FLASH_SLOTS) return E_PAR;
    UW slot_base = WASM_FLASH_SLOT_OFFSET(flash_slot);
    INT sec;
    for (sec = 0; sec < (INT)(WASM_FLASH_SLOT_SIZE / FLASH_SECTOR_SIZE); sec++) {
        UINT imask;
        Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(slot_base + (UW)sec * FLASH_SECTOR_SIZE, NULL, 0);
        Asm("msr primask, %0" :: "r"(imask));
    }
    return E_OK;
}

/* slot の生 header を返す (NULL なら無効 slot) */
LOCAL const T_WASM_FLASH_HEADER *wasm_flash_header(INT flash_slot)
{
    if (flash_slot < 0 || flash_slot >= WASM_FLASH_SLOTS) return NULL;
    const T_WASM_FLASH_HEADER *h =
        (const T_WASM_FLASH_HEADER *)WASM_FLASH_SLOT_BASE(flash_slot);
    if (h->magic != WASM_FLASH_HEADER_MAGIC) return NULL;
    if (h->version != 1) return NULL;
    if (h->size > WASM_FLASH_MAX_BODY) return NULL;

    UW crc_len = (UW)offsetof(T_WASM_FLASH_HEADER, header_crc32);
    if (wasm_crc32((const UB *)h, crc_len) != h->header_crc32) return NULL;
    return h;
}

EXPORT ER tk_wasm_store(INT ram_slot, INT flash_slot, const char *name, UW flags)
{
    if (ram_slot < 0 || ram_slot >= WASM_MAX_SLOT) return E_PAR;
    if (flash_slot < 0 || flash_slot >= WASM_FLASH_SLOTS) return E_PAR;
    T_WASM_SLOT *s = &wasm_slots[ram_slot];
    if (!s->in_use) return E_OBJ;
    if (s->size == 0 || s->bytecode == NULL) return E_OBJ;
    if (s->received < s->size) return E_OBJ;      /* 未完成 */
    if (s->size > WASM_FLASH_MAX_BODY) return E_LIMIT;

    T_WASM_FLASH_HEADER hdr;
    memset(&hdr, 0xFF, sizeof(hdr));              /* reserved も 0xFF で埋める */
    hdr.magic     = WASM_FLASH_HEADER_MAGIC;
    hdr.version   = 1;
    hdr.size      = s->size;
    hdr.memory_kb = s->memory_kb;
    hdr.priority  = WASM_TASK_PRI;
    hdr.flags     = flags;
    memcpy(hdr.sha256, s->expected_sha256, 32);
    memset(hdr.name, 0, sizeof(hdr.name));
    if (name != NULL) {
        UW n = 0;
        while (name[n] && n < sizeof(hdr.name) - 1) {
            hdr.name[n] = name[n]; n++;
        }
    }
    UW crc_len = (UW)offsetof(T_WASM_FLASH_HEADER, header_crc32);
    hdr.header_crc32 = wasm_crc32((const UB *)&hdr, crc_len);

    ER er = wasm_flash_write_slot(flash_slot, &hdr, s->bytecode, s->size);
    if (er != E_OK) return er;

    /* 書き戻し検証 (XIP 経由で読んで SHA 再計算) */
    const T_WASM_FLASH_HEADER *verify = wasm_flash_header(flash_slot);
    if (verify == NULL) return E_IO;
    if (verify->size != s->size) return E_IO;
    UB calc[32];
    sha256_compute((const UB *)verify + WASM_FLASH_HEADER_SIZE, verify->size, calc);
    INT i;
    for (i = 0; i < 32; i++) {
        if (calc[i] != verify->sha256[i]) return E_IO;
    }

    if (flags & WASM_STORE_FLAG_AUTO_BOOT) {
        (void)tk_wasm_autoboot_set(flash_slot);
    } else if (tk_wasm_autoboot_get() == flash_slot) {
        (void)tk_wasm_autoboot_set(-1);          /* 上書きで auto_boot 解除 */
    }
    return E_OK;
}

EXPORT ER tk_wasm_load(INT flash_slot, INT ram_slot)
{
    if (ram_slot < 0 || ram_slot >= WASM_MAX_SLOT) return E_PAR;
    const T_WASM_FLASH_HEADER *h = wasm_flash_header(flash_slot);
    if (h == NULL) return E_NOEXS;
    if (h->size == 0 || h->size > WASM_FLASH_MAX_BODY) return E_NOEXS;

    T_WASM_SLOT *s = &wasm_slots[ram_slot];
    if (s->in_use && s->state == WASM_STATE_RUNNING) return E_OBJ;
    if (s->in_use) slot_reset(s);

    s->bytecode = (UB *)Kmalloc(h->size);
    if (s->bytecode == NULL) return E_NOMEM;

    const UB *src = (const UB *)WASM_FLASH_SLOT_BASE(flash_slot) + WASM_FLASH_HEADER_SIZE;
    memcpy(s->bytecode, src, h->size);

    UB calc[32];
    sha256_compute(s->bytecode, h->size, calc);
    INT i;
    for (i = 0; i < 32; i++) {
        if (calc[i] != h->sha256[i]) {
            Kfree(s->bytecode); s->bytecode = NULL;
            return E_IO;
        }
    }

    s->in_use   = TRUE;
    s->state    = WASM_STATE_LOADED;
    s->size     = h->size;
    s->received = h->size;
    s->memory_kb = (h->memory_kb > 0) ? h->memory_kb : WASM_DEFAULT_MEMORY_KB;
    memcpy(s->expected_sha256, h->sha256, 32);
    return E_OK;
}

EXPORT ER tk_wasm_erase(INT flash_slot)
{
    if (flash_slot < 0 || flash_slot >= WASM_FLASH_SLOTS) return E_PAR;
    ER er = wasm_flash_erase_slot(flash_slot);
    if (er != E_OK) return er;
    if (tk_wasm_autoboot_get() == flash_slot) {
        (void)tk_wasm_autoboot_set(-1);
    }
    return E_OK;
}

EXPORT ER tk_wasm_flash_get_info(INT flash_slot, T_WASM_FLASH_INFO *out)
{
    if (out == NULL) return E_PAR;
    if (flash_slot < 0 || flash_slot >= WASM_FLASH_SLOTS) return E_PAR;
    memset(out, 0, sizeof(*out));
    out->flash_slot = flash_slot;
    const T_WASM_FLASH_HEADER *h = wasm_flash_header(flash_slot);
    if (h == NULL) return E_OK;                   /* valid=FALSE で返す */
    out->valid     = TRUE;
    out->size      = h->size;
    out->memory_kb = h->memory_kb;
    out->priority  = h->priority;
    out->flags     = h->flags;
    memcpy(out->sha256, h->sha256, 32);
    memcpy(out->name, h->name, sizeof(out->name));
    out->name[sizeof(out->name) - 1] = '\0';
    return E_OK;
}

/*----------------------------------------------------------------------
 * Autoboot (FLASH_STORAGE セクタ内の 0x2C0 に 32B レコード)
 */
LOCAL const T_WASM_AUTOBOOT_REC *wasm_autoboot_rec(void)
{
    const T_WASM_AUTOBOOT_REC *r =
        (const T_WASM_AUTOBOOT_REC *)(FLASH_STORAGE_BASE + WASM_AUTOBOOT_FLASH_OFFSET);
    if (r->magic != WASM_AUTOBOOT_MAGIC) return NULL;
    if (r->version != 1) return NULL;
    UW crc_len = (UW)offsetof(T_WASM_AUTOBOOT_REC, crc32);
    if (wasm_crc32((const UB *)r, crc_len) != r->crc32) return NULL;
    return r;
}

EXPORT INT tk_wasm_autoboot_get(void)
{
    const T_WASM_AUTOBOOT_REC *r = wasm_autoboot_rec();
    if (r == NULL) return -1;
    if (r->boot_slot < 0 || r->boot_slot >= WASM_FLASH_SLOTS) return -1;
    return r->boot_slot;
}

EXPORT ER tk_wasm_autoboot_set(INT flash_slot)
{
    if (flash_slot >= WASM_FLASH_SLOTS) return E_PAR;
    /* -1 (disabled) は許容 */

    UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
    if (sector == NULL) return E_NOMEM;
    memcpy(sector, (const void *)FLASH_STORAGE_BASE, FLASH_SECTOR_SIZE);

    T_WASM_AUTOBOOT_REC *rec =
        (T_WASM_AUTOBOOT_REC *)(sector + WASM_AUTOBOOT_FLASH_OFFSET);
    memset(rec, 0xFF, sizeof(*rec));
    rec->magic     = WASM_AUTOBOOT_MAGIC;
    rec->version   = 1;
    rec->boot_slot = flash_slot;
    rec->flags     = 0;
    UW crc_len = (UW)offsetof(T_WASM_AUTOBOOT_REC, crc32);
    rec->crc32 = wasm_crc32((const UB *)rec, crc_len);

    UINT imask;
    Asm("mrs %0, primask; cpsid i" : "=r"(imask));
    flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector, FLASH_SECTOR_SIZE);
    Asm("msr primask, %0" :: "r"(imask));
    Kfree(sector);
    return E_OK;
}

EXPORT ER tk_wasm_boot(void)
{
    INT slot = tk_wasm_autoboot_get();
    if (slot < 0) return E_OK;

    /* brick 回避: 先に autoboot を disable。起動成功時のみ再 enable。
     * 失敗ループで電源入切しても 2 回目は autoboot 無効で通常起動する */
    (void)tk_wasm_autoboot_set(-1);

    ER er = tk_wasm_load(slot, 0);
    if (er != E_OK) {
        W5DBG("wasm_boot: load(flash_slot=%d) failed: %d\n", slot, er);
        return er;
    }
    /* HardFault recovery 用: この slot で動く wasm が fault したら
     * autoboot を自動 disable する判断に使う */
    g_autoboot_ram_slot = 0;
    er = tk_wasm_run(0, NULL);
    if (er != E_OK) {
        g_autoboot_ram_slot = -1;
        W5DBG("wasm_boot: run failed: %d\n", er);
        return er;
    }
    (void)tk_wasm_autoboot_set(slot);
    W5DBG("wasm_boot: flash_slot=%d launched\n", slot);
    return E_OK;
}

#else /* !CPU_RP2040 — STM32H5 等は現時点未サポート */

EXPORT ER tk_wasm_store(INT ram_slot, INT flash_slot, const char *name, UW flags)
    { (void)ram_slot; (void)flash_slot; (void)name; (void)flags; return E_NOSPT; }
EXPORT ER tk_wasm_load(INT flash_slot, INT ram_slot)
    { (void)flash_slot; (void)ram_slot; return E_NOSPT; }
EXPORT ER tk_wasm_erase(INT flash_slot)
    { (void)flash_slot; return E_NOSPT; }
EXPORT ER tk_wasm_flash_get_info(INT flash_slot, T_WASM_FLASH_INFO *out)
    { (void)flash_slot; if (out) memset(out, 0, sizeof(*out)); return E_NOSPT; }
EXPORT INT tk_wasm_autoboot_get(void) { return -1; }
EXPORT ER  tk_wasm_autoboot_set(INT flash_slot) { (void)flash_slot; return E_NOSPT; }
EXPORT ER  tk_wasm_boot(void) { return E_OK; }

#endif /* CPU_RP2040 */

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
