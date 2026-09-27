/* PC 上の検証用: μT-Kernel API の最小代替 */
#ifndef SHIM_TK_TKERNEL_H
#define SHIM_TK_TKERNEL_H
#include <tk/typedef.h>
typedef struct { W hi; UW lo; } SYSTIM;
typedef struct { INT dummy; } FastLock;
ER   CreateLock(FastLock *lock, CONST UB *name);
void Lock(FastLock *lock);
void Unlock(FastLock *lock);
ER   tk_get_otm(SYSTIM *tim);
void Kfree(void *p);
UW   in_w(UW addr);
void out_w(UW addr, UW val);
#endif
