/* SPDX-License-Identifier: MIT */
#include "instruction_cache.h"
static uint64_t control(void) {
    uint64_t value;__asm__ volatile("mrs %0,sctlr_el1":"=r"(value));return value;
}
int xpl_icache_run(unsigned enabled,int (*work)(void *),void *context,xpl_icache_diag *d) {
    uint64_t el;
    if(enabled>1u||!work||!d)return XPL_ICACHE_ARGUMENT;
    *d=(xpl_icache_diag){0};
    __asm__ volatile("mrs %0,CurrentEL":"=r"(el));
    if(el!=4u)return XPL_ICACHE_STATE;
    d->before=control();
    if(d->before&5u)return XPL_ICACHE_STATE;
    if(enabled&&!(d->before&(UINT64_C(1)<<12))) {
        uint64_t value=d->before|(UINT64_C(1)<<12);
        __asm__ volatile("dsb sy; ic iallu; dsb sy; msr sctlr_el1,%0; isb"::"r"(value):"memory");
        d->changed=1;
    }
    d->during=control();
    int result=work(context);
    if(d->changed)
        __asm__ volatile("dsb sy; msr sctlr_el1,%0; isb"::"r"(d->before):"memory");
    d->after=control();d->restored=d->after==d->before;
    return result;
}
