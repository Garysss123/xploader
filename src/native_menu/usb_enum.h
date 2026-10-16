/* SPDX-License-Identifier: MIT */
#ifndef XPL_USB_ENUM_H
#define XPL_USB_ENUM_H
#include "usb_power_probe.h"
#include "usb_msd.h"
typedef struct {
    xpl_usb_power_diag power;
    uint32_t phase,events,setups,resets,connects,last_event,last_command,last_command_value;
    uint32_t phy_version,phy_ctrl,phy_common,dctl,dsts,configured,address,speed,cleanup_errors;
    uint64_t last_setup,elapsed_us;
    uint64_t setup_history[4];
    uint32_t address_register,last_trb_size,last_trb_control;
    uint32_t media_commands,media_reads,media_writes,media_ejected,media_phase;
    uint32_t fifo_ram,fifo0,fifo1;
    int result;
} xpl_usb_enum_diag;
/* Finite descriptor-only vendor device. Caller owns a verified temporary
 * watchdog and disjoint identity-mapped DMA RAM. No SD/block endpoints. */
int xpl_usb_enum_probe(const xpl_sd_board_io *,void *dma,uint64_t dma_pa,size_t dma_bytes,
    const char *serial,xpl_usb_enum_diag *);
int xpl_usb_msd_probe(const xpl_sd_board_io *,void *,uint64_t,size_t,const char *,xpl_usb_msd *,xpl_usb_enum_diag *);
#endif
