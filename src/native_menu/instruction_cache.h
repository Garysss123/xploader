/* SPDX-License-Identifier: MIT */
#ifndef XPL_INSTRUCTION_CACHE_H
#define XPL_INSTRUCTION_CACHE_H
#include <stdint.h>
enum { XPL_ICACHE_ARGUMENT=-1, XPL_ICACHE_STATE=-2 };
typedef struct {
    uint64_t before, during, after;
    unsigned changed, restored;
} xpl_icache_diag;
/* EL1 with MMU/data cache off. Only SCTLR.I is changed, for this callback;
 * exact incoming SCTLR is restored on every returning callback outcome. */
int xpl_icache_run(unsigned enabled,int (*work)(void *),void *context,xpl_icache_diag *diag);
#endif
