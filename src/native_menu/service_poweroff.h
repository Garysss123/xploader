/* SPDX-License-Identifier: MIT. E6653 PM8994/PMI8994 shutdown transaction. */
#ifndef XPL_SERVICE_POWEROFF_H
#define XPL_SERVICE_POWEROFF_H
#include "service_probe.h"
typedef int (*xpl_power_wait_fn)(void *,unsigned microseconds);
typedef struct {uint8_t control[2],enable[2];unsigned changed,phase,commands,pmic_writes;int rollback;uint32_t dload0,dload1;} xpl_poweroff_diag;
enum {XPL_POWER_TRIGGERED=1,XPL_POWER_ARGUMENT=-1,XPL_POWER_IO=-2,XPL_POWER_PON=-3,XPL_POWER_CHANGED=-4,XPL_POWER_DELAY=-5,XPL_POWER_ROLLBACK=-6};
/* Reference: measured kernel ced7acdc qpnp_pon_reset_config(), shutdown0x04,
 * disable reset ->500us ->type ->enable for SID0 and2; then PS_HOLD only.
 * No filesystem, clock, DLOAD, TA, GPT or secure-monitor operations. */
int xpl_service_poweroff_trigger(const struct xpl_hw_config *,const struct xpl_hw_io *,xpl_power_wait_fn,void *,xpl_poweroff_diag *);
/* Only after the caller observed that requested power-off did not occur. */
int xpl_service_poweroff_returned(const struct xpl_hw_io *,xpl_power_wait_fn,void *,xpl_poweroff_diag *);
#endif
