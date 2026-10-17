/* SPDX-License-Identifier: MIT */
#include "entry_watchdog.h"
#include "entry.h"
#include "native_menu_config.h"
#ifndef XPL_ENTRY_WDT_SECONDS
#define XPL_ENTRY_WDT_SECONDS 20
#endif
static xpl_entry_watchdog entry_wdt_state;
volatile uint32_t xpl_entry_wdt_ready;
static int read32(void *u,uint64_t a,uint32_t *v){(void)u;return !xpl_guard_read32((uintptr_t)a,v);}
static int write32(void *u,uint64_t a,uint32_t v){(void)u;return !xpl_guard_write32((uintptr_t)a,v);}
static void barrier(void *u){(void)u;__asm__ volatile("dsb sy":::"memory");}
static const struct xpl_hw_io io={0,read32,write32,barrier};
int xpl_entry_watchdog_begin(void){
    int result=xpl_entry_watchdog_arm(&io,XPL_ENTRY_WDT_SECONDS,&entry_wdt_state);
    if(!result)xpl_entry_wdt_ready=1;
    return result;
}
int xpl_entry_watchdog_finish(unsigned successful_handoff){
    /* A returned error keeps the deadline; the carrier fallback tightens it.
     * Only a committed payload may inherit the original WDT/reason state. */
    if(!successful_handoff)return 0;
    if(entry_wdt_state.phase!=3||entry_wdt_state.armed!=1)return -1;
    return xpl_entry_watchdog_restore(&io,&entry_wdt_state);
}
void xpl_entry_watchdog_record(volatile uint8_t *p,size_t size){(void)xpl_entry_watchdog_publish(&entry_wdt_state,p,size);}
_Noreturn void xpl_entry_watchdog_failstop(void){
    xpl_entry_watchdog retry;
    for(unsigned n=0;n<3;n++)if(!xpl_entry_watchdog_arm(&io,20,&retry))break;
    /* No feeding and no return to a partially copied kernel. Hardware failure
     * to arm is not claimed recoverable; first acceptance requires an operator. */
    for(;;)__asm__ volatile("wfe");
}
