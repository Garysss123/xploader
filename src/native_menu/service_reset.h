/* SPDX-License-Identifier: MIT */
#ifndef XPL_SERVICE_RESET_H
#define XPL_SERVICE_RESET_H
#include "service_probe.h"
enum {XPL_S1_TRIGGERED=1,XPL_S1_ARGUMENT=-1,XPL_S1_IO=-2,XPL_S1_PON=-3,
      XPL_S1_DLOAD=-4,XPL_S1_CHANGED=-5,XPL_S1_RETURNED=-6};
typedef struct {uint32_t old_reason,dload0,dload1,phase,writes;} xpl_service_reset_diag;
/* Reuses already-enabled warm PON only; no PMIC, DLOAD, secure monitor,
 * storage or clock changes. Exact allowed writes: restart reason and PS_HOLD. */
int xpl_service_fastboot_trigger(const struct xpl_hw_config *,const struct xpl_hw_io *,
    xpl_service_snapshot *,xpl_service_reset_diag *);
/* Only after the caller observed that a requested reset did not occur. */
int xpl_service_reset_returned(const struct xpl_hw_io *,xpl_service_reset_diag *);
int xpl_service_restart_trigger(const struct xpl_hw_config *,const struct xpl_hw_io *,xpl_service_snapshot *,xpl_service_reset_diag *);
int xpl_service_restart_returned(const struct xpl_hw_io *,xpl_service_reset_diag *);
#endif
