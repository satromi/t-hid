/*
 * u2f_mcp.c — U2F 認証器の MCP ツール
 *
 *   u2f_status   状態取得 (秘密の値は返さない)
 */

#include <sys/machine.h>
#if defined(USE_U2F) && defined(USE_MCP)

#include "../../device/wiznet/tk_mcp_int.h"
#include "u2f.h"

cJSON *tool_u2f_status(cJSON *args)
{
	cJSON *result = cJSON_CreateObject();
	T_U2F_STATUS st;
	(void)args;

	u2f_get_status(&st);
	cJSON_AddBoolToObject(result, "ready", st.ready);
	cJSON_AddStringToObject(result, "p256", st.uses_pka ? "PKA" : "software");
	cJSON_AddStringToObject(result, "sha256", st.uses_hash ? "HASH" : "software");
	cJSON_AddStringToObject(result, "key_storage", st.uses_saes ? "SAES" : "plaintext");
	cJSON_AddNumberToObject(result, "counter", st.counter);
	cJSON_AddNumberToObject(result, "registrations", st.registrations);
	cJSON_AddNumberToObject(result, "authentications", st.authentications);
	return result;
}

#endif /* USE_U2F && USE_MCP */
