/* SPDX-License-Identifier: MIT */
#ifndef XPL_USB_MSD_H
#define XPL_USB_MSD_H
#include <stddef.h>
#include <stdint.h>
enum {XPL_MSD_CBW=0,XPL_MSD_IN=1,XPL_MSD_DISCARD_OUT=2,XPL_MSD_CSW=3,XPL_MSD_RESET_REQUIRED=4,XPL_MSD_ZLP=5,XPL_MSD_WRITE_OUT=6};
typedef struct {
    void *context;
    int (*read_sector)(void *,uint64_t,uint8_t [512]);
    int (*write_sector)(void *,uint64_t,const uint8_t [512]);
    uint64_t sectors,lba;
    uint32_t tag,host_bytes,done,remaining,discard,pending,reads,writes,commands;
    uint8_t response[64],response_bytes,status,sense,asc,ascq,phase,prepared,ejected,prevent,sector_stream,opcode;
} xpl_usb_msd;
/* Default read-only. Writes require a separate explicit idle-session grant. */
int xpl_usb_msd_init(xpl_usb_msd *,uint64_t,void *,int (*)(void *,uint64_t,uint8_t [512]));
int xpl_usb_msd_enable_write(xpl_usb_msd *,int (*)(void *,uint64_t,const uint8_t [512]));
void xpl_usb_msd_reset(xpl_usb_msd *);
int xpl_usb_msd_out(xpl_usb_msd *,const uint8_t *,size_t);
int xpl_usb_msd_prepare_in(xpl_usb_msd *,uint8_t [512],size_t *);
int xpl_usb_msd_in_complete(xpl_usb_msd *,size_t);
size_t xpl_usb_msd_out_bytes(const xpl_usb_msd *);
#endif
