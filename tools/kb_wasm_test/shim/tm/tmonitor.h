#ifndef SHIM_TM_TMONITOR_H
#define SHIM_TM_TMONITOR_H
#include <stdio.h>
#define tm_printf(fmt, ...) printf((const char *)(fmt), ##__VA_ARGS__)
#endif
