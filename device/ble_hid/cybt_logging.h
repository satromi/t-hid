/*
 * cybt_logging.h -- CYW43 BT shared bus logging macros
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43_PRINTF (tm_printf にリダイレクト済み) を使用。
 */

#ifndef CYBT_LOGGING_H
#define CYBT_LOGGING_H

#include "cyw43_configport.h"

#define cybt_error(...)    CYW43_PRINTF(__VA_ARGS__)

#ifndef NDEBUG
#define cybt_info(...)     CYW43_PRINTF(__VA_ARGS__)
#else
#define cybt_info(...)     (void)0
#endif

/* デバッグログは通常無効 (有効にするとログ量が膨大) */
#if 0
#define cybt_debug(...)    CYW43_PRINTF(__VA_ARGS__)
#else
#define cybt_debug(...)    (void)0
#endif

#endif /* CYBT_LOGGING_H */
