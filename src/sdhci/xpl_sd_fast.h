/* SPDX-License-Identifier: MIT */
#ifndef XPL_SD_FAST_H
#define XPL_SD_FAST_H
#include "xpl_sd_board.h"
typedef struct {uint32_t cfg,m,n,d,clock;unsigned phase;} xpl_sd_fast_diag;
/* Default-speed one-bit SD at XO19.2MHz/2=9.6MHz, with no new PLL, voltage,
 * UHS, four-bit or high-speed negotiation. Requires selected ready card whose
 * actual CSD advertises >=9.6MHz and an earned active/HPM board. Enclosing core
 * scope restores clocks on return even on failure. Output stops during update. */
int xpl_sd_clock_9m6(const xpl_sd_board_io *io,uint32_t limit,uint32_t card_max_hz,
                    xpl_sd_fast_diag *diag);
#endif
