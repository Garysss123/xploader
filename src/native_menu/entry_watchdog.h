/* SPDX-License-Identifier: MIT */
#ifndef XPL_ENTRY_WATCHDOG_H
#define XPL_ENTRY_WATCHDOG_H
#include "../native_hw/xpl_hw.h"

#define XPL_ENTRY_WDT_RESET UINT64_C(0xf9017004)
#define XPL_ENTRY_WDT_ENABLE UINT64_C(0xf9017008)
#define XPL_ENTRY_WDT_BARK UINT64_C(0xf9017010)
#define XPL_ENTRY_WDT_BITE UINT64_C(0xf9017014)
#define XPL_ENTRY_WDT_REASON UINT64_C(0xfe87f65c)
#define XPL_ENTRY_DLOAD0 UINT64_C(0xfe87f000)
#define XPL_ENTRY_DLOAD1 UINT64_C(0xfe87f004)
#define XPL_ENTRY_WDT_HZ 32765u
#define XPL_ENTRY_WDT_FASTBOOT 0x77665500u
#define XPL_ENTRY_WDT_RECORD_BYTES 64u

/* Fixed measured E6653/S1 profile. No SPMI, cookie or storage writes.
 * The caller must have established EL1/MMU-off and its abort guards first.
 * 20s is the previously measured profile; other values need acceptance.
 * Covers execution after begin, not S1 transport/decompression before entry. */
typedef struct {
    uint32_t old_en,old_bark,old_bite,old_reason,cookie0,cookie1;
    uint32_t phase,armed,timeout_seconds,writes;
    int result,restore_result;
} xpl_entry_watchdog;
int xpl_entry_watchdog_arm(const struct xpl_hw_io *,unsigned seconds,xpl_entry_watchdog *);
int xpl_entry_watchdog_restore(const struct xpl_hw_io *,xpl_entry_watchdog *);
int xpl_entry_watchdog_publish(const xpl_entry_watchdog *,volatile uint8_t *,size_t);

/* Native bindings; compiled out of old entry paths when the config is off. */
int xpl_entry_watchdog_begin(void);
int xpl_entry_watchdog_finish(unsigned successful_handoff);
void xpl_entry_watchdog_record(volatile uint8_t *,size_t);
_Noreturn void xpl_entry_watchdog_failstop(void);
#endif
