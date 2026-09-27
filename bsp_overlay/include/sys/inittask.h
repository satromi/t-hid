/*
 *    micro T-Kernel 3.00.00
 *
 *    Copyright (C) 2006-2019 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *	inittask.h
 *	Initial task definition
 *
 *	BSP overlay: スタックサイズを 1KB → 4KB に拡大
 *	(usermain でネットワーク初期化を実行するため)
 */

#ifndef _INITTASK_DEF_
#define _INITTASK_DEF_

#define INITTASK_EXINF		(0x0)
#define INITTASK_ITSKPRI	(1)
#define INITTASK_STKSZ		(4*1024)
#define INITTASK_DSNAME		"inittsk"

#if USE_IMALLOC

#define INITTASK_TSKATR		(TA_HLNG | TA_RNG0)
#define INITTASK_STACK		(NULL)

#else

#define INITTASK_TSKATR		(TA_HLNG | TA_RNG0 | TA_USERBUF)
#define INITTASK_STACK		init_task_stack

#endif

#endif /* _INITTASK_DEF_ */
