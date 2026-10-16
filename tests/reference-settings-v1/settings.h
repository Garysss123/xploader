/* SPDX-License-Identifier: MIT. Two independent fixed-size SD records. */
#ifndef XPL_SETTINGS_H
#define XPL_SETTINGS_H
#include <stdint.h>
#include <stddef.h>
#define XPL_SETTINGS_RECORD 512u
#define XPL_SETTINGS_PATH "/xploader.cfg"
typedef struct {uint64_t sequence;uint32_t startup,target;char directory[256];} xpl_settings;
void xpl_settings_default(xpl_settings *);
int xpl_settings_decode(const uint8_t *,size_t,xpl_settings *);
int xpl_settings_encode(const xpl_settings *,uint8_t [512]);
/* -1: neither record valid, -2: ambiguous equal generations. No auto boot. */
int xpl_settings_choose(const uint8_t [1024],xpl_settings *,unsigned *slot);
#endif
