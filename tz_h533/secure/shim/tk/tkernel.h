/*
 * セキュア側イメージ用: μT-Kernel API の最小代替
 */
#ifndef SEC_SHIM_TK_TKERNEL_H
#define SEC_SHIM_TK_TKERNEL_H
#include <tk/typedef.h>

Inline UW   in_w(UW addr)          { return *(volatile UW *)addr; }
Inline void out_w(UW addr, UW val) { *(volatile UW *)addr = val; }

/* 割り込み禁止 (PRIMASK_S はセキュアと非セキュアの両方の割り込みを止める) */
Inline UINT sec_disint(void)
{
	UINT pm;
	__asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r"(pm) :: "memory");
	return pm;
}
Inline void sec_enaint(UINT pm)
{
	__asm__ volatile ("msr primask, %0" :: "r"(pm) : "memory");
}
#define DI(imask)	((imask) = sec_disint())
#define EI(imask)	sec_enaint(imask)

#endif
