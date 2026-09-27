/*
 * セキュア側イメージ用: tm_printf の代替
 *   セキュア側にはコンソールがないので、文字列をログ領域にためる。
 *   非セキュア側が u2fs_log_read() で取り出して表示する。
 */
#ifndef SEC_SHIM_TM_TMONITOR_H
#define SEC_SHIM_TM_TMONITOR_H
#include <tk/typedef.h>
INT tm_printf(const UB *format, ...);
#endif
