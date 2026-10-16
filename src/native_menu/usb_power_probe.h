/* SPDX-License-Identifier: MIT */
#ifndef XPL_USB_POWER_PROBE_H
#define XPL_USB_POWER_PROBE_H
#include "../sdhci/xpl_sd_board.h"
#define XPL_USB_POWER_REGS 12u
typedef struct {
    uint32_t before[XPL_USB_POWER_REGS],after[XPL_USB_POWER_REGS];
    uint32_t pll_mode,pll_l,pll_alpha,pll_alpha_hi,bcr,id,dctl,dsts;
    uint32_t phase,reads,writes,restore_mask;
    uint64_t before_hash,after_hash;
    int result,cleanup_result;
} xpl_usb_power_diag;
/* Scoped digital USB GCC clocks/GDSC only, then read core identity and restore.
 * No PLL, PHY, PMIC, USB controller, DMA, storage or endpoint writes. */
int xpl_usb_power_probe(const xpl_sd_board_io *,xpl_usb_power_diag *);
/* The owner must stop all USB DMA before release. Failure acquires no lease. */
int xpl_usb_power_acquire(const xpl_sd_board_io *,xpl_usb_power_diag *);
int xpl_usb_power_release(const xpl_sd_board_io *,xpl_usb_power_diag *);
#endif
