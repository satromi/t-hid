/*
 *----------------------------------------------------------------------
 *    MCP Server over MQTT for μT-Kernel 3.0
 *
 *    VM (bytecode virtual machine), Flash operations, Agent
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include "tk_mcp_int.h"
#include "tk_mcp_hal.h"

/*======================================================================
 * Flash 安全操作 — プラットフォーム依存
 *====================================================================*/

#if defined(CPU_RP2040)
/*----------------------------------------------------------------------
 * RP2040: ROM 関数テーブル経由の XIP Flash 操作
 */
typedef void (*rom_void_fn)(void);
typedef void (*rom_flash_erase_fn)(UW offset, UW count, UW block_size, UB block_cmd);
typedef void (*rom_flash_program_fn)(UW offset, const UB *data, UW count);

LOCAL void * __attribute__((section(".data"), noinline, long_call))
rom_func_lookup(UW code)
{
    typedef void *(*rom_table_lookup_fn)(UH *table, UW code);
    UH *func_table = (UH *)(UW)*(UH *)0x00000014;
    rom_table_lookup_fn lookup = (rom_table_lookup_fn)(UW)*(UH *)0x00000018;
    return lookup(func_table, code);
}

void __attribute__((section(".data"), noinline, long_call))
flash_safe_erase_program(UW offset, const UB *data, UW len)
{
    rom_void_fn connect = (rom_void_fn)rom_func_lookup('I' | ('F' << 8));
    rom_void_fn exit_xip = (rom_void_fn)rom_func_lookup('E' | ('X' << 8));
    rom_flash_erase_fn erase = (rom_flash_erase_fn)rom_func_lookup('R' | ('E' << 8));
    rom_flash_program_fn program = (rom_flash_program_fn)rom_func_lookup('R' | ('P' << 8));
    rom_void_fn flush = (rom_void_fn)rom_func_lookup('F' | ('C' << 8));
    rom_void_fn enter_xip = (rom_void_fn)rom_func_lookup('C' | ('X' << 8));

    connect();
    exit_xip();
    erase(offset, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE, 0x20);
    if (data != NULL && len > 0) {
        program(offset, data, len);
    }
    flush();
    enter_xip();
}

#elif defined(MTKBSP_CPU_STM32H5)
/*----------------------------------------------------------------------
 * STM32H5: 内蔵 Flash (device/flash/stm32h5_flash.c)
 *   offset は Flash 先頭からのバイト位置。該当セクタを消去してから書く
 */
#include "../flash/stm32h5_flash.h"

void flash_safe_erase_program(UW offset, const UB *data, UW len)
{
    if (data != NULL && len > 0) {
        h5_flash_write(offset, data, len);
    } else {
        h5_flash_erase(offset, FLASH_SECTOR_SIZE);
    }
}

#else
/* 未知プラットフォーム: 何もしない */
void flash_safe_erase_program(UW offset, const UB *data, UW len)
{
    (void)offset; (void)data; (void)len;
}
#endif

/*======================================================================
 * Bytecode VM — スタックベース仮想マシン
 *
 * AI がバイトコードを生成し、RTOS タスクとして自律実行する。
 * 命令セット (1byte opcode):
 *   00 NOP          04 ADD          08 JMP addr16   0C GPIO_WR (pin,val)
 *   01 PUSH val16   05 SUB          09 JZ  addr16   0D ADC_RD (ch→val)
 *   02 POP          06 GT (a>b→1)   0A JNZ addr16   0E DELAY ms16
 *   03 DUP          07 LT (a<b→1)   0B GPIO_RD(pin) 0F HALT
 *                                                    10 MQTT_PUB (topic in prog)
 *====================================================================*/

T_VM_INST vm_inst[VM_MAX_INST];

/* チャンク受信バッファ */
LOCAL T_VM_CHUNKED vm_chunked = { NULL, 0, 0, FALSE };

LOCAL void vm_task(INT stacd, void *exinf)
{
    T_VM_INST *vm = (T_VM_INST *)exinf;
    (void)stacd;

    while (vm->running && vm->pc < vm->prog_len) {
        UB op = vm->prog[vm->pc++];

        switch (op) {
        case OP_NOP:
            break;

        case OP_PUSH:
            if (vm->pc + 1 < vm->prog_len && vm->sp < VM_STACK_SZ) {
                W val = (W)((UH)vm->prog[vm->pc] << 8 | vm->prog[vm->pc + 1]);
                vm->stack[vm->sp++] = val;
                vm->pc += 2;
            } else goto done;
            break;

        case OP_POP:
            if (vm->sp > 0) vm->sp--;
            break;

        case OP_DUP:
            if (vm->sp > 0 && vm->sp < VM_STACK_SZ)
                vm->stack[vm->sp] = vm->stack[vm->sp - 1], vm->sp++;
            break;

        case OP_ADD:
            if (vm->sp >= 2) { vm->sp--; vm->stack[vm->sp-1] += vm->stack[vm->sp]; }
            break;

        case OP_SUB:
            if (vm->sp >= 2) { vm->sp--; vm->stack[vm->sp-1] -= vm->stack[vm->sp]; }
            break;

        case OP_GT:
            if (vm->sp >= 2) { vm->sp--; vm->stack[vm->sp-1] = (vm->stack[vm->sp-1] > vm->stack[vm->sp]) ? 1 : 0; }
            break;

        case OP_LT:
            if (vm->sp >= 2) { vm->sp--; vm->stack[vm->sp-1] = (vm->stack[vm->sp-1] < vm->stack[vm->sp]) ? 1 : 0; }
            break;

        case OP_JMP:
            if (vm->pc + 1 < vm->prog_len) {
                vm->pc = (UH)vm->prog[vm->pc] << 8 | vm->prog[vm->pc + 1];
            } else goto done;
            break;

        case OP_JZ:
            if (vm->pc + 1 < vm->prog_len) {
                UH addr = (UH)vm->prog[vm->pc] << 8 | vm->prog[vm->pc + 1];
                vm->pc += 2;
                if (vm->sp > 0 && vm->stack[--vm->sp] == 0) vm->pc = addr;
            } else goto done;
            break;

        case OP_JNZ:
            if (vm->pc + 1 < vm->prog_len) {
                UH addr = (UH)vm->prog[vm->pc] << 8 | vm->prog[vm->pc + 1];
                vm->pc += 2;
                if (vm->sp > 0 && vm->stack[--vm->sp] != 0) vm->pc = addr;
            } else goto done;
            break;

        case OP_GPIO_RD:
            if (vm->sp >= 1) {
                INT pin = (INT)vm->stack[vm->sp - 1];
                if (pin >= 0 && pin <= 29) {
                    mcp_gpio_set_input(pin);
                    vm->stack[vm->sp - 1] = mcp_gpio_read(pin);
                }
            }
            break;

        case OP_GPIO_WR:
            if (vm->sp >= 2) {
                W val = vm->stack[--vm->sp];
                INT pin = (INT)vm->stack[--vm->sp];
                if (pin >= 0 && pin <= 29) {
                    mcp_gpio_set_output(pin);
                    if (val) mcp_gpio_write_high(pin);
                    else     mcp_gpio_write_low(pin);
                }
            }
            break;

        case OP_ADC_RD:
            if (vm->sp >= 1) {
#if MCP_HAS_ADC
                INT ch = (INT)vm->stack[vm->sp - 1];
                if (ch >= 0 && ch <= MCP_ADC_TEMP_CH) {
                    UW raw = mcp_adc_read_raw(ch);
                    UW mv = (raw * 3300) / 4095;
                    vm->stack[vm->sp - 1] = (W)(270 - (W)(mv - 706) * 10000 / 1721);
                }
#else
                vm->stack[vm->sp - 1] = 0;  /* ADC not supported */
#endif
            }
            break;

        case OP_DELAY:
            if (vm->pc + 1 < vm->prog_len) {
                UH ms = (UH)vm->prog[vm->pc] << 8 | vm->prog[vm->pc + 1];
                vm->pc += 2;
                tk_dly_tsk(ms);
            } else goto done;
            break;

        case OP_HALT:
            goto done;

        case OP_MQTT:
            /* 簡易: スタックトップの値を MQTT で publish */
            if (vm->sp >= 1) {
                char buf[16];
                W v = vm->stack[--vm->sp];
                INT p = 0;
                if (v < 0) { buf[p++] = '-'; v = -v; }
                INT d = 15;
                char tmp[16];
                do { tmp[d--] = '0' + (v % 10); v /= 10; } while (v > 0);
                while (++d < 16) buf[p++] = tmp[d];
                buf[p] = '\0';
                tk_mqtt_publish("utk3/vm", (const UB *)buf, p);
            }
            break;

        default:
            goto done;
        }
    }
done:
    vm->running = FALSE;
    /* プログラムバッファ解放 */
    if (vm->prog != NULL) {
        Kfree(vm->prog);
        vm->prog = NULL;
    }
    tk_ext_tsk();
}

/*
 * VM プログラムバッファを Kmalloc で確保して hex デコード
 * 戻り値: バッファポインタ (呼び出し元で Kfree する必要なし — VM タスクが解放)
 */
LOCAL UB *vm_alloc_decode(const char *hex, UW *out_len)
{
    UW hex_len = strlen(hex);
    UW byte_len = hex_len / 2;
    if (byte_len == 0) return NULL;

    UB *buf = (UB *)Kmalloc(byte_len);
    if (buf == NULL) return NULL;

    UW i;
    for (i = 0; i < byte_len; i++) {
        buf[i] = (hex_val(hex[i*2]) << 4) | hex_val(hex[i*2+1]);
    }
    *out_len = byte_len;
    return buf;
}

/*
 * VM インスタンスを起動 (バッファは既に確保済み)
 */
LOCAL cJSON *vm_start_instance(UB *prog, UW prog_len)
{
    cJSON *result = cJSON_CreateObject();

    /* 空きスロット */
    INT slot = -1;
    INT i;
    for (i = 0; i < VM_MAX_INST; i++) {
        if (!vm_inst[i].running && vm_inst[i].prog == NULL) { slot = i; break; }
    }
    if (slot < 0) {
        Kfree(prog);
        cJSON_AddStringToObject(result, "error", "max VM instances (4)");
        return result;
    }

    T_VM_INST *vm = &vm_inst[slot];
    vm->prog = prog;
    vm->prog_size = prog_len;
    vm->prog_len = (UH)prog_len;
    vm->sp = 0;
    vm->pc = 0;
    vm->running = TRUE;

    T_CTSK ctsk;
    ctsk.exinf = (void *)vm;
    ctsk.tskatr = TA_HLNG | TA_RNG3;
    ctsk.task = (FP)vm_task;
    ctsk.itskpri = 13;
    ctsk.stksz = 512;
    ctsk.bufptr = NULL;
    vm->tskid = tk_cre_tsk(&ctsk);
    if (vm->tskid < 0) {
        Kfree(vm->prog);
        vm->prog = NULL;
        vm->running = FALSE;
        cJSON_AddStringToObject(result, "error", "task create failed");
        return result;
    }
    tk_sta_tsk(vm->tskid, 0);

    cJSON_AddStringToObject(result, "status", "running");
    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddNumberToObject(result, "task_id", vm->tskid);
    cJSON_AddNumberToObject(result, "prog_bytes", vm->prog_len);

    return result;
}

/*----------------------------------------------------------------------
 * tool_vm_run — hex 文字列 or チャンクモードで VM 実行
 *
 * 通常: {"program":"hex..."} → 即実行
 * チャンク開始: {"action":"begin","size":1024} → バッファ確保
 * チャンク追加: {"action":"chunk","data":"hex..."} → 追記
 * チャンク実行: {"action":"run"} → VM 起動
 */
cJSON *tool_vm_run(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *action_item = cJSON_GetObjectItem(args, "action");

    /* チャンクモード */
    if (action_item != NULL && cJSON_IsString(action_item)) {
        const char *action = action_item->valuestring;

        if (strcmp(action, "begin") == 0) {
            /* チャンク受信開始 */
            cJSON *size_item = cJSON_GetObjectItem(args, "size");
            if (size_item == NULL || !cJSON_IsNumber(size_item)) {
                cJSON_AddStringToObject(result, "error", "missing size");
                return result;
            }
            UW size = (UW)size_item->valueint;
            if (size == 0 || size > 65536) {
                cJSON_AddStringToObject(result, "error", "size 1-65536");
                return result;
            }
            /* 既存バッファ解放 */
            if (vm_chunked.buf != NULL) { Kfree(vm_chunked.buf); }
            vm_chunked.buf = (UB *)Kmalloc(size);
            if (vm_chunked.buf == NULL) {
                cJSON_AddStringToObject(result, "error", "out of memory");
                return result;
            }
            vm_chunked.total_size = size;
            vm_chunked.received = 0;
            vm_chunked.active = TRUE;
            cJSON_AddStringToObject(result, "status", "ready");
            cJSON_AddNumberToObject(result, "size", size);
            return result;

        } else if (strcmp(action, "chunk") == 0) {
            /* チャンク追加 */
            if (!vm_chunked.active || vm_chunked.buf == NULL) {
                cJSON_AddStringToObject(result, "error", "no active chunked upload (call begin first)");
                return result;
            }
            cJSON *data_item = cJSON_GetObjectItem(args, "data");
            if (data_item == NULL || !cJSON_IsString(data_item)) {
                cJSON_AddStringToObject(result, "error", "missing data");
                return result;
            }
            const char *hex = data_item->valuestring;
            UW byte_len = strlen(hex) / 2;
            if (vm_chunked.received + byte_len > vm_chunked.total_size) {
                cJSON_AddStringToObject(result, "error", "exceeds declared size");
                return result;
            }
            UW i;
            for (i = 0; i < byte_len; i++) {
                vm_chunked.buf[vm_chunked.received + i] =
                    (hex_val(hex[i*2]) << 4) | hex_val(hex[i*2+1]);
            }
            vm_chunked.received += byte_len;
            cJSON_AddStringToObject(result, "status", "ok");
            cJSON_AddNumberToObject(result, "received", vm_chunked.received);
            cJSON_AddNumberToObject(result, "total", vm_chunked.total_size);
            return result;

        } else if (strcmp(action, "run") == 0) {
            /* チャンク完了 → VM 起動 */
            if (!vm_chunked.active || vm_chunked.buf == NULL) {
                cJSON_AddStringToObject(result, "error", "no active upload");
                return result;
            }
            /* バッファの所有権を VM インスタンスに移譲 */
            UB *prog = vm_chunked.buf;
            UW prog_len = vm_chunked.received;
            vm_chunked.buf = NULL;
            vm_chunked.active = FALSE;

            cJSON_Delete(result);
            return vm_start_instance(prog, prog_len);

        } else if (strcmp(action, "cancel") == 0) {
            if (vm_chunked.buf != NULL) { Kfree(vm_chunked.buf); }
            vm_chunked.buf = NULL;
            vm_chunked.active = FALSE;
            cJSON_AddStringToObject(result, "status", "cancelled");
            return result;
        }
    }

    /* 通常モード: {"program":"hex..."} → 即実行 */
    cJSON *prog_item = cJSON_GetObjectItem(args, "program");
    if (prog_item == NULL || !cJSON_IsString(prog_item)) {
        cJSON_AddStringToObject(result, "error", "missing program or action");
        return result;
    }

    UW prog_len;
    UB *prog = vm_alloc_decode(prog_item->valuestring, &prog_len);
    if (prog == NULL) {
        cJSON_AddStringToObject(result, "error", "out of memory");
        return result;
    }

    cJSON_Delete(result);
    return vm_start_instance(prog, prog_len);
}

/*----------------------------------------------------------------------
 * vm_stop — VM インスタンス停止
 */
cJSON *tool_vm_stop(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *slot_item = cJSON_GetObjectItem(args, "slot");
    if (slot_item == NULL || !cJSON_IsNumber(slot_item)) {
        cJSON_AddStringToObject(result, "error", "missing slot");
        return result;
    }

    INT slot = slot_item->valueint;
    INT stopped = 0;

    if (slot == -1) {
        /* 全スロット停止 */
        INT i;
        for (i = 0; i < VM_MAX_INST; i++) {
            if (vm_inst[i].running) {
                vm_inst[i].running = FALSE;
                /* タスク終了待ち (最大 2 秒) */
                INT r;
                for (r = 0; r < 20; r++) {
                    T_RTSK rtsk;
                    if (tk_ref_tsk(vm_inst[i].tskid, &rtsk) != E_OK) break;
                    if (rtsk.tskstat & TTS_DMT) break;
                    tk_dly_tsk(100);
                }
                tk_del_tsk(vm_inst[i].tskid);
                vm_inst[i].tskid = 0;
                if (vm_inst[i].prog) { Kfree(vm_inst[i].prog); vm_inst[i].prog = NULL; }
                stopped++;
            }
        }
    } else if (slot >= 0 && slot < VM_MAX_INST) {
        if (vm_inst[slot].running) {
            vm_inst[slot].running = FALSE;
            INT r;
            for (r = 0; r < 20; r++) {
                T_RTSK rtsk;
                if (tk_ref_tsk(vm_inst[slot].tskid, &rtsk) != E_OK) break;
                if (rtsk.tskstat & TTS_DMT) break;
                tk_dly_tsk(100);
            }
            tk_del_tsk(vm_inst[slot].tskid);
            vm_inst[slot].tskid = 0;
            if (vm_inst[slot].prog) { Kfree(vm_inst[slot].prog); vm_inst[slot].prog = NULL; }
            stopped = 1;
        } else {
            cJSON_AddStringToObject(result, "error", "slot not running");
            return result;
        }
    } else {
        cJSON_AddStringToObject(result, "error", "slot: 0-3 or -1 for all");
        return result;
    }

    cJSON_AddStringToObject(result, "status", "stopped");
    cJSON_AddNumberToObject(result, "stopped_count", stopped);

    return result;
}

/*----------------------------------------------------------------------
 * vm_store — VM プログラムを Flash に保存/ロード (起動時自動実行)
 *
 * Flash レイアウト (末尾 4KB セクタ):
 *   [0x00-0x03] マジック "VM01"
 *   [0x04-0x05] プログラム長 (16bit LE)
 *   [0x06-...]  バイトコード
 */
cJSON *tool_vm_store(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *action_item = cJSON_GetObjectItem(args, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        cJSON_AddStringToObject(result, "error", "missing action");
        return result;
    }

    const char *action = action_item->valuestring;

    if (strcmp(action, "store") == 0) {
        cJSON *prog_item = cJSON_GetObjectItem(args, "program");
        if (prog_item == NULL || !cJSON_IsString(prog_item)) {
            cJSON_AddStringToObject(result, "error", "missing program");
            return result;
        }
        const char *hex = prog_item->valuestring;
        UH byte_len = (UH)(strlen(hex) / 2);
        /* Flash ストレージ最大: offset 6 から API キー領域 (0x100) まで = 250 bytes
         * それ以上はチャンクモードで RAM に直接ロードすること */
        if (byte_len > (APIKEY_FLASH_OFFSET - 6)) {
            cJSON_AddStringToObject(result, "error", "too large for flash (max 250, use chunked vm_run)");
            return result;
        }

        UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
        if (sector == NULL) {
            cJSON_AddStringToObject(result, "error", "out of memory");
            return result;
        }
        memset(sector, 0xFF, FLASH_SECTOR_SIZE);
        sector[0] = 'V'; sector[1] = 'M'; sector[2] = '0'; sector[3] = '1';
        sector[4] = (UB)(byte_len & 0xFF);
        sector[5] = (UB)(byte_len >> 8);
        UH i;
        for (i = 0; i < byte_len; i++) {
            sector[6 + i] = (hex_val(hex[i*2]) << 4) | hex_val(hex[i*2+1]);
        }

        { UINT imask; Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask)); }
        Kfree(sector);

        cJSON_AddStringToObject(result, "status", "stored");
        cJSON_AddNumberToObject(result, "bytes", byte_len);

    } else if (strcmp(action, "load") == 0) {
        /* Flash から読み込んで VM 実行 */
        const UB *flash = (const UB *)FLASH_STORAGE_BASE;
        if (flash[0] != 'V' || flash[1] != 'M' || flash[2] != '0' || flash[3] != '1') {
            cJSON_AddStringToObject(result, "error", "no stored program");
            return result;
        }
        UH plen = (UH)flash[4] | ((UH)flash[5] << 8);
        if (plen == 0 || plen > (APIKEY_FLASH_OFFSET - 6)) {
            cJSON_AddStringToObject(result, "error", "invalid program length");
            return result;
        }
        /* Flash から Kmalloc バッファにコピーして VM 起動 */
        UB *prog = (UB *)Kmalloc(plen);
        if (prog == NULL) {
            cJSON_AddStringToObject(result, "error", "out of memory");
            return result;
        }
        memcpy(prog, flash + 6, plen);

        cJSON_Delete(result);
        result = vm_start_instance(prog, plen);

    } else if (strcmp(action, "clear") == 0) {
        { UINT imask; Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, NULL, 0);
        Asm("msr primask, %0" :: "r"(imask)); }
        cJSON_AddStringToObject(result, "status", "cleared");

    } else if (strcmp(action, "status") == 0) {
        const UB *flash = (const UB *)FLASH_STORAGE_BASE;
        BOOL has_prog = (flash[0] == 'V' && flash[1] == 'M' &&
                         flash[2] == '0' && flash[3] == '1');
        cJSON_AddBoolToObject(result, "stored", has_prog);
        if (has_prog) {
            UH plen = (UH)flash[4] | ((UH)flash[5] << 8);
            cJSON_AddNumberToObject(result, "bytes", plen);
        }
    } else {
        cJSON_AddStringToObject(result, "error", "action: store/load/clear/status");
    }

    return result;
}

/*----------------------------------------------------------------------
 * agent_start — 自律復旧エージェント
 *
 * 定期的にタスク状態とネットワークを監視。
 * 異常検出時にログ出力 + 自動復旧を試みる。
 */
LOCAL ID agent_tskid = 0;
LOCAL volatile BOOL agent_running = FALSE;
LOCAL UW agent_interval = 5000;

LOCAL void agent_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    W5DBG("AGENT: started (interval=%dms)\n", agent_interval);

    while (agent_running) {
        tk_dly_tsk(agent_interval);
        if (!agent_running) break;

        /* 1. ネットワークリンク確認 */
#if defined(WIFI_CYW43)
        if (tk_wifi_get_state() != TK_WIFI_ST_READY) {
            W5DBG("AGENT: WiFi link DOWN detected\n");
        }
#else
        if (wizphy_getphylink() == PHY_LINK_ON) {
            /* リンク UP — 正常 */
        } else {
            W5DBG("AGENT: link DOWN detected\n");
        }
#endif

        /* 2. MQTT 接続確認 */
        if (!tk_mqtt_is_connected()) {
            W5DBG("AGENT: MQTT disconnected\n");
            /* MQTT 再接続はアプリケーション側で対応 */
        }

        /* 3. 全タスクの状態確認 */
        {
            ID id;
            for (id = 1; id <= 32; id++) {
                T_RTSK rtsk;
                if (tk_ref_tsk(id, &rtsk) != E_OK) continue;
                if (rtsk.tskstat == 0) continue;
                /* DORMANT タスクを検出 (意図しない終了の可能性) */
                if ((rtsk.tskstat & TTS_DMT) && id != agent_tskid) {
                    W5DBG("AGENT: task %d DORMANT\n", id);
                }
            }
        }

        /* 4. ヒープ確認 */
        {
            void *p = Kmalloc(256);
            if (p) {
                Kfree(p);
            } else {
                W5DBG("AGENT: heap low\n");
            }
        }

        /* 5. VM インスタンス状態 (DORMANT 確認後にクリーンアップ) */
        {
            INT i;
            for (i = 0; i < VM_MAX_INST; i++) {
                if (vm_inst[i].tskid > 0 && !vm_inst[i].running) {
                    T_RTSK rtsk;
                    if (tk_ref_tsk(vm_inst[i].tskid, &rtsk) == E_OK &&
                        (rtsk.tskstat & TTS_DMT)) {
                        tk_del_tsk(vm_inst[i].tskid);
                        vm_inst[i].tskid = 0;
                        if (vm_inst[i].prog) { Kfree(vm_inst[i].prog); vm_inst[i].prog = NULL; }
                        W5DBG("AGENT: cleaned VM slot %d\n", i);
                    }
                }
            }
        }
    }

    W5DBG("AGENT: stopped\n");
    tk_ext_tsk();
}

cJSON *tool_agent_start(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *action_item = cJSON_GetObjectItem(args, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        cJSON_AddStringToObject(result, "error", "missing action");
        return result;
    }

    const char *action = action_item->valuestring;

    if (strcmp(action, "start") == 0) {
        if (agent_running) {
            cJSON_AddStringToObject(result, "status", "already running");
            return result;
        }
        cJSON *intv = cJSON_GetObjectItem(args, "interval_ms");
        agent_interval = (intv && cJSON_IsNumber(intv)) ? (UW)intv->valueint : 5000;
        agent_running = TRUE;

        T_CTSK ctsk;
        ctsk.exinf = NULL;
        ctsk.tskatr = TA_HLNG | TA_RNG3;
        ctsk.task = (FP)agent_task;
        ctsk.itskpri = 15;
        ctsk.stksz = 1024;
        ctsk.bufptr = NULL;
        agent_tskid = tk_cre_tsk(&ctsk);
        if (agent_tskid < 0) {
            agent_running = FALSE;
            cJSON_AddStringToObject(result, "error", "task create failed");
            return result;
        }
        tk_sta_tsk(agent_tskid, 0);
        cJSON_AddStringToObject(result, "status", "started");
        cJSON_AddNumberToObject(result, "task_id", agent_tskid);

    } else if (strcmp(action, "stop") == 0) {
        agent_running = FALSE;
        cJSON_AddStringToObject(result, "status", "stopping");

    } else if (strcmp(action, "status") == 0) {
        cJSON_AddBoolToObject(result, "running", agent_running);
        if (agent_running) {
            cJSON_AddNumberToObject(result, "interval_ms", agent_interval);
        }
    } else {
        cJSON_AddStringToObject(result, "error", "action: start/stop/status");
    }

    return result;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
