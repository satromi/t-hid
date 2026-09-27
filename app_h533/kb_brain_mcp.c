/*
 * kb_brain_mcp.c — キーボードブレインの MCP ツール
 *
 *   kb_wasm_attach {slot}  アップロード済み wasm をキー処理にする
 *   kb_wasm_detach         組み込みキーマップに戻す
 *   kb_wasm_status         状態取得
 */

#include <sys/machine.h>
#if defined(KB_BRAIN) && defined(USE_WASM)

#include "../device/wiznet/tk_mcp_int.h"
#include "kb_brain.h"
#include "kb_wasm.h"

cJSON *tool_kb_wasm_attach(cJSON *args)
{
	cJSON *result = cJSON_CreateObject();
	cJSON *slot = cJSON_GetObjectItem(args, "slot");
	T_KB_WASM_STATUS st;
	ER err;

	if (!cJSON_IsNumber(slot)) {
		cJSON_AddStringToObject(result, "error", "slot is required");
		return result;
	}

	err = kb_wasm_attach(slot->valueint);
	kb_wasm_get_status(&st);
	cJSON_AddBoolToObject(result, "ok", err == E_OK);
	cJSON_AddNumberToObject(result, "er", err);
	if (err != E_OK) {
		cJSON_AddStringToObject(result, "error",
			st.last_error[0] ? st.last_error :
			(err == E_OBJ) ? "slot is not in loaded state" : "attach failed");
		return result;
	}
	cJSON_AddNumberToObject(result, "module_size", st.module_size);

	/* save=true なら Flash に保存し、次回起動時も自動で取り付ける */
	if (cJSON_IsTrue(cJSON_GetObjectItem(args, "save"))) {
		err = kb_wasm_save();
		cJSON_AddBoolToObject(result, "saved", err == E_OK);
	}
	return result;
}

cJSON *tool_kb_wasm_detach(cJSON *args)
{
	cJSON *result = cJSON_CreateObject();
	cJSON_AddBoolToObject(result, "ok", kb_wasm_detach() == E_OK);

	/* erase=true なら保存済みモジュールも消し、次回起動を組み込みキーマップにする */
	if (cJSON_IsTrue(cJSON_GetObjectItem(args, "erase"))) {
		cJSON_AddBoolToObject(result, "erased", kb_wasm_erase_saved() == E_OK);
	}
	return result;
}

cJSON *tool_kb_wasm_status(cJSON *args)
{
	cJSON *result = cJSON_CreateObject();
	T_KB_WASM_STATUS st;
	(void)args;

	kb_wasm_get_status(&st);
	cJSON_AddStringToObject(result, "key_processor", st.active ? "wasm" : "builtin");
	cJSON_AddNumberToObject(result, "module_size", st.module_size);
	cJSON_AddNumberToObject(result, "saved_size", kb_wasm_saved_size());
	cJSON_AddNumberToObject(result, "scan_count", st.scan_count);
	cJSON_AddNumberToObject(result, "max_scan_us", st.max_scan_us);
	cJSON_AddNumberToObject(result, "layers", st.layers);
	if (st.last_error[0]) cJSON_AddStringToObject(result, "last_error", st.last_error);
	cJSON_AddBoolToObject(result, "left_connected", kb_brain_left_connected());
	cJSON_AddBoolToObject(result, "right_connected", kb_brain_right_connected());
	return result;
}

#endif /* KB_BRAIN && USE_WASM */
