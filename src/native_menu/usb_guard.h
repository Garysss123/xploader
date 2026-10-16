/* SPDX-License-Identifier: MIT */
#ifndef XPL_USB_GUARD_H
#define XPL_USB_GUARD_H
#include "service_probe.h"
typedef struct {uint32_t old_en,old_bark,old_bite,old_reason,phase,writes,armed;int result,restore_result;} xpl_usb_guard;
/* Temporary APPS WDT:20s bite, bootloader reason, warm-PON/no-DLOAD only.
 * Caller feeds through its existing WDT reset while healthy, and restores
 * the snapshot after stopping USB. This does not write firmware/storage. */
int xpl_usb_guard_arm(const struct xpl_hw_config *,const struct xpl_hw_io *,xpl_usb_guard *);
int xpl_usb_guard_disarm(const struct xpl_hw_io *,xpl_usb_guard *);
#endif
