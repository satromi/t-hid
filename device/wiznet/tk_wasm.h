/*
 *----------------------------------------------------------------------
 *    tk_wasm — WebAssembly (wasm3) ランタイム for μT-Kernel 3.0
 *
 *    Bytecode VM (tk_mcp_vm.c) を置き換える、汎用 WASM 実行機構。
 *    MCP 経由で .wasm モジュールを注入 → 専用タスクで実行。
 *
 *    クライアント側 (PC) で clang -target=wasm32 等で C/Rust を wasm に
 *    コンパイルし、MQTT チャンクで device に送信する。host function
 *    (gpio/adc/mqtt 等) は tk_wasm_host.c で登録する。
 *----------------------------------------------------------------------
 */

#ifndef __TK_WASM_H__
#define __TK_WASM_H__

#include <tk/tkernel.h>

#define WASM_MAX_SLOT            4       /* 同時ロード可能モジュール数 */
#define WASM_MAX_MODULE_SIZE     (32*1024)  /* 1 モジュール最大 32KB */
#define WASM_DEFAULT_MEMORY_KB   16      /* wasm linear memory 初期値 */
#define WASM_TASK_STKSZ          8192    /* wasm タスクのスタック (host function 内の
                                          * lwip/MQTT フレームも同居するため余裕を取る) */
#define WASM_TASK_PRI            13      /* MCP(10)より低く poll(15)より高い */

/*----------------------------------------------------------------------
 * Flash 永続化領域 (RP2040 のみ有効)
 *
 *   2MB Flash 末尾レイアウト:
 *     0x1DF000-0x1FEFFF  128KB  wasm slot 本体 (4 slot × 32KB)
 *     0x1FF000-0x1FFFFF    4KB  FLASH_STORAGE (既存; autoboot metadata も同居)
 */
#define WASM_FLASH_SLOTS            4
#define WASM_FLASH_SLOT_SIZE        (32 * 1024)
#define WASM_FLASH_BASE_OFFSET      0x1DF000
#define WASM_FLASH_SLOT_OFFSET(n)   (WASM_FLASH_BASE_OFFSET + (UW)(n) * WASM_FLASH_SLOT_SIZE)
#define WASM_FLASH_SLOT_BASE(n)     (0x10000000u + WASM_FLASH_SLOT_OFFSET(n))

#define WASM_FLASH_HEADER_MAGIC     0x31304657  /* "WF01" */
#define WASM_FLASH_HEADER_SIZE      96
#define WASM_FLASH_MAX_BODY         (WASM_FLASH_SLOT_SIZE - WASM_FLASH_HEADER_SIZE)

/* tk_wasm_store flags */
#define WASM_STORE_FLAG_AUTO_BOOT   (1u << 0)

typedef enum {
    WASM_STATE_EMPTY = 0,
    WASM_STATE_LOADING,     /* begin 済み、chunk 受信中 */
    WASM_STATE_LOADED,      /* 全 byte 受信、未検証 */
    WASM_STATE_RUNNING,     /* 実行中 */
    WASM_STATE_STOPPED,     /* 終了 (正常/エラー後) */
    WASM_STATE_ERROR,       /* 不正な state */
} T_WASM_STATE;

typedef struct {
    UB  state;                  /* T_WASM_STATE */
    UW  expected_size;
    UW  received_bytes;
    UB  expected_sha256[32];
    UW  memory_kb;
    ID  tskid;                  /* 0 なら非実行 */
    char last_error[64];        /* m3 エラー文字列 (最大 63 chars) */
} T_WASM_SLOT_INFO;

/* ランタイム全体の初期化 (システム起動時に 1 度だけ) */
ER   tk_wasm_init(void);

/* チャンク受信開始: scratch バッファを確保、metadata 初期化 */
ER   tk_wasm_begin(INT slot, UW size, const UB sha256[32], UW memory_kb);

/* chunk 追加 (offset 位置に len バイトを書込) */
ER   tk_wasm_chunk(INT slot, UW offset, const UB *data, UW len);

/* 実行開始: SHA-256 検証 → parse → load → task 起動 */
ER   tk_wasm_run(INT slot, const char *entry);

/* 強制終了: task 削除、runtime 解放、buffer free */
ER   tk_wasm_stop(INT slot);

/* 中断: LOADING 中の scratch を破棄 */
ER   tk_wasm_abort(INT slot);

/* slot 情報取得 */
void tk_wasm_get_info(INT slot, T_WASM_SLOT_INFO *out);

/* 受信済み (LOADED) モジュールを SHA-256 検証して引き取る。slot は空になり、
 * *pbuf の解放 (Kfree) は呼び出し側が行う */
ER   tk_wasm_take_bytecode(INT slot, UB **pbuf, UW *psize);

/*----------------------------------------------------------------------
 * Flash 永続化 API (RP2040 のみ実体あり。他ターゲットでは E_NOSPT)
 */
typedef struct {
    INT  flash_slot;
    BOOL valid;
    UW   size;              /* body bytes (0 = empty) */
    UW   memory_kb;
    UW   priority;
    UW   flags;
    UB   sha256[32];
    char name[32];
} T_WASM_FLASH_INFO;

/* RAM slot → Flash slot: 当該 RAM slot (LOADED/RUNNING/STOPPED いずれでも可) の
 * bytecode/SHA/memory_kb を flash_slot へ永続化。flags に
 * WASM_STORE_FLAG_AUTO_BOOT を付けると電源投入時に自動起動される */
ER   tk_wasm_store(INT ram_slot, INT flash_slot, const char *name, UW flags);

/* Flash slot → RAM slot: 復元 (SHA-256 検証済。以降 tk_wasm_run で起動可) */
ER   tk_wasm_load (INT flash_slot, INT ram_slot);

/* Flash slot を 0xFF 埋め (autoboot が当該 slot なら自動 disable) */
ER   tk_wasm_erase(INT flash_slot);

/* Flash slot 情報取得 (info tool 用) */
ER   tk_wasm_flash_get_info(INT flash_slot, T_WASM_FLASH_INFO *out);

/*----------------------------------------------------------------------
 * Autoboot (起動時に Flash slot 1 つを自動 load+run)
 *
 * tk_wasm_boot は init 時に呼ぶ。brick 回避のため、実行前に autoboot 設定を
 * 一旦 disable → 成功時に再 enable する。失敗すれば次回起動で skip される
 */
INT  tk_wasm_autoboot_get(void);            /* -1 = disabled / unset */
ER   tk_wasm_autoboot_set(INT flash_slot);  /* -1 で無効化 */
ER   tk_wasm_boot(void);

#endif /* __TK_WASM_H__ */
