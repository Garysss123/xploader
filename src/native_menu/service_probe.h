/* SPDX-License-Identifier: MIT */
#ifndef XPL_SERVICE_PROBE_H
#define XPL_SERVICE_PROBE_H
#include "../native_hw/xpl_hw.h"
typedef struct {
    uint32_t arbiter,reason,pshold,gdsc,master,sleep,mock,usb_id,usb_control,usb_config,usb_status;
    uint64_t pon[2];
    unsigned reads,commands,usb_reads;
    int pon_result[2],usb_result;
} xpl_service_snapshot;
/* The measured E6653 entry presents either zero or an erased pair. Neither
 * equals the standard download magic. Mixed/other values remain refused;
 * this predicate never clears or writes download state. */
static inline int xpl_service_dload_idle(uint32_t first,uint32_t second) {
    return (!first&&!second)||(first==UINT32_MAX&&second==UINT32_MAX);
}
/* Fixed 8994 observations only. SPMI controller CMD writes request PMIC reads;
 * no PMIC byte/configuration, USB, clock, restart reason or PS_HOLD write. */
int xpl_service_probe(const struct xpl_hw_config *config,const struct xpl_hw_io *io,
                      unsigned poll_limit,xpl_service_snapshot *snapshot);
#endif
