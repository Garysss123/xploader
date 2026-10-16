/* SPDX-License-Identifier: MIT */
#include "xpl_sd_core_probe.h"
#define APPS UINT64_C(0xfc400504)
#define AHB UINT64_C(0xfc400508)
#define CMD UINT64_C(0xfc400510)
#define CFG UINT64_C(0xfc400514)
#define M UINT64_C(0xfc400518)
#define N UINT64_C(0xfc40051c)
#define D UINT64_C(0xfc400520)
#define MODE UINT64_C(0xf98a4078)
#define HC UINT64_C(0xf98a4900)
#define CFG_MASK 0x371fu
static int rd(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t *v) {
    if(io->read(io->ctx,a,w,v))return XPL_SD_EIO;
    if(w==1)*v&=255u;else if(w==2)*v&=65535u;return 0;
}
static int wr(const xpl_sd_board_io *io,uint64_t a,uint32_t v,xpl_sd_core_snapshot *s) {
    s->writes++;return io->write(io->ctx,a,4,v)?XPL_SD_EIO:0;
}
static int bit(const xpl_sd_board_io *io,uint64_t a,uint32_t mask,uint32_t v,xpl_sd_core_snapshot *s) {
    uint32_t old;int e=rd(io,a,4,&old);return e?e:wr(io,a,(old&~mask)|(v&mask),s);
}
static int poll(const xpl_sd_board_io *io,uint64_t a,uint32_t mask,uint32_t v,uint32_t limit) {
    uint64_t start=io->now_us(io->ctx);uint32_t data;int e;
    for(uint32_t i=0;i<limit;i++) {
        if((e=rd(io,a,4,&data)))return e;
        if((data&mask)==v)return 0;
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=100000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int root(const xpl_sd_board_io *io,uint32_t m,uint32_t n,uint32_t d,uint32_t cfg,
                 uint32_t limit,xpl_sd_core_snapshot *s) {
    int e;
    if((e=wr(io,M,m,s))||(e=wr(io,N,n,s))||(e=wr(io,D,d,s))||
       (e=bit(io,CFG,CFG_MASK,cfg,s))||(e=bit(io,CMD,1u,1u,s)))return e;
    return poll(io,CMD,1u,0,limit);
}
static void remember(int e,int *first) {if(e&&!*first)*first=e;}
int xpl_sd_with_core_clock(const xpl_sd_board_io *io,uint32_t prerequisites,
                            uint32_t limit,int (*operation)(void *),void *context,
                            xpl_sd_core_snapshot *s) {
    const uint32_t needed=XPL_SD_BOARD_EXCLUSIVE|XPL_SD_BOARD_BUS_SAFE|
        XPL_SD_BOARD_XO_19M2|XPL_SD_BOARD_HC_ACCESSIBLE;
    int e=0;unsigned ahb_attempted=0,apps_attempted=0,root_attempted=0,mode_attempted=0;
    if(!io||!io->read||!io->write||!io->now_us||!s||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *s=(xpl_sd_core_snapshot){0};
    if((prerequisites&needed)!=needed)return XPL_SD_EPREP;
    s->phase=1;
    if((e=xpl_sd_board_probe(io,&s->before)))return e;
    if((s->before.gcc_apps&1u)||!(s->before.gcc_apps&0x80000000u)||s->before.rcgr_cmd&1u)return XPL_SD_EPREP;
    if(!(s->before.gcc_ahb&1u)) {
        ahb_attempted=1;if((e=bit(io,AHB,1u,1u,s)))goto restore;
    }
    if((e=poll(io,AHB,0x80000001u,1u,limit)))goto restore;
    if((e=rd(io,MODE,4,&s->mode_before))||(e=rd(io,HC+0x29u,1,&s->power_before))||
       (e=rd(io,HC+0x2cu,2,&s->clock_before)))goto restore;
    if(s->mode_before||(s->power_before&&!(s->power_before==0x0bu&&
       (prerequisites&XPL_SD_BOARD_NATIVE_WARM_POWER)))||(s->clock_before&4u)) {e=XPL_SD_EPREP;goto restore;}
    s->phase=2;apps_attempted=1;
    if((e=bit(io,APPS,1u,1u,s))||(e=poll(io,APPS,0x80000001u,1u,limit)))goto restore;
    /* Existing standard card-clock output is zero. Only the internal source
     * changes: LK8994 F(400000,cxo,12,1,4), HID23/source0/dual-edge. */
    s->phase=3;root_attempted=1;
    if((e=root(io,1u,~3u,~4u,0x2017u,limit,s)))goto restore;
    s->phase=4;mode_attempted=1;
    if((e=wr(io,MODE,s->mode_before|1u,s)))goto restore;
    if((e=rd(io,MODE,4,&s->mode_active))||(e=rd(io,HC+0xfeu,2,&s->version))||
       (e=rd(io,HC+0x40u,4,&s->caps))||(e=rd(io,HC+0x2cu,2,&s->clock_active))||
       (e=rd(io,UINT64_C(0xf98a40dc),4,&s->power_status))||
       (e=xpl_sd_board_probe(io,&s->active)))goto restore;
    s->phase=5;
    if(s->mode_active!=1u||(s->clock_active&4u)||s->version==0xffffu||!s->caps||s->caps==UINT32_MAX||
       (s->active.rcgr_cfg&CFG_MASK)!=0x2017u||s->active.m!=1u||(s->active.n&255u)!=252u||
       (s->active.d&255u)!=251u||s->active.pads!=s->before.pads)e=XPL_SD_EHOST;
    if(!e&&operation) {
        e=operation(context);
        uint32_t stopped;
        if(rd(io,HC+0x2cu,2,&stopped)||(stopped&4u)) {if(!e)e=XPL_SD_EHOST;}
    }
restore:
    /* Each restore is attempted independently; preserve the first failure.
     * No misleading restoration success if an uncertain store occurred. */
    if(mode_attempted) {
        remember(wr(io,MODE,s->mode_before,s),&s->restore_result);
        remember(rd(io,MODE,4,&s->mode_after),&s->restore_result);
        if(s->mode_after!=s->mode_before)remember(XPL_SD_EHOST,&s->restore_result);
    }
    if(root_attempted)remember(root(io,s->before.m,s->before.n,s->before.d,s->before.rcgr_cfg,limit,s),&s->restore_result);
    if(apps_attempted)remember(bit(io,APPS,1u,s->before.gcc_apps,s),&s->restore_result);
    if(ahb_attempted)remember(bit(io,AHB,1u,s->before.gcc_ahb,s),&s->restore_result);
    remember(xpl_sd_board_probe(io,&s->after),&s->restore_result);
    if(!s->restore_result&&(s->after.gcc_apps!=s->before.gcc_apps||s->after.gcc_ahb!=s->before.gcc_ahb||
       (s->after.rcgr_cfg&CFG_MASK)!=(s->before.rcgr_cfg&CFG_MASK)||s->after.m!=s->before.m||
       s->after.n!=s->before.n||s->after.d!=s->before.d||s->after.pads!=s->before.pads||s->after.rcgr_cmd&1u))
        s->restore_result=XPL_SD_EHOST;
    s->restored=!s->restore_result;
    return e?e:s->restore_result;
}
int xpl_sd_core_clock_probe(const xpl_sd_board_io *io,uint32_t prerequisites,
                             uint32_t limit,xpl_sd_core_snapshot *s) {
    return xpl_sd_with_core_clock(io,prerequisites,limit,0,0,s);
}
