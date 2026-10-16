/* SPDX-License-Identifier: MIT */
#ifndef XPL_USB_CONTROL_H
#define XPL_USB_CONTROL_H
#include <stddef.h>
#include <stdint.h>
typedef struct {uint8_t configured,address;char serial[25];uint8_t mass_storage,halted,writable;} xpl_usb_control;
enum {XPL_USB_CONTROL_STALL=-1,XPL_USB_CONTROL_IN=1,XPL_USB_CONTROL_ACK=0};
int xpl_usb_control_request(xpl_usb_control *,const uint8_t setup[8],uint8_t *out,size_t capacity,size_t *bytes);
#endif
