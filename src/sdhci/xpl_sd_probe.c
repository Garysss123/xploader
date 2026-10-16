/* SPDX-License-Identifier: MIT */
#include "xpl_sd_probe.h"
#define AHB UINT64_C(0xfc400508)
#define APPS UINT64_C(0xfc400504)
#define HC UINT64_C(0xf98a4900)
#define CORE_MODE UINT64_C(0xf98a4078)
static int read_reg(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t *v) {
    if(io->read(io->ctx,a,w,v))return XPL_SD_EIO;
    if(w==1)*v&=255u;else if(w==2)*v&=65535u;
    return 0;
}
static int wait_on(const xpl_sd_board_io *io,uint32_t limit,uint32_t *last) {
    uint64_t start=io->now_us(io->ctx);int e;
    for(uint32_t i=0;i<limit;i++) {
        if((e=read_reg(io,AHB,4,last)))return e;
        if((*last&0x80000001u)==1u)return 0;
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=100000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
int xpl_sd_interface_probe(const xpl_sd_board_io *io,uint32_t prerequisites,
                           uint32_t limit,xpl_sd_interface_snapshot *s) {
    const uint32_t required=XPL_SD_BOARD_EXCLUSIVE|XPL_SD_BOARD_BUS_SAFE;
    int e=0;uint32_t current=0;
    if(!io||!io->read||!io->write||!io->now_us||!s||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *s=(xpl_sd_interface_snapshot){0};
    if((prerequisites&required)!=required)return XPL_SD_EPREP;
    if((e=read_reg(io,APPS,4,&s->apps_before))||(e=read_reg(io,AHB,4,&s->ahb_before)))return e;
    /* Never inspect a card/core already running or an unrecognized all-ones
     * interface; this scope is specifically the measured gated S1 state. */
    if((s->apps_before&1u)||!(s->apps_before&0x80000000u)||s->apps_before==UINT32_MAX||
       s->ahb_before==UINT32_MAX)return XPL_SD_EPREP;
    if(!(s->ahb_before&1u)) {
        s->enable_attempted=1;
        if(io->write(io->ctx,AHB,4,s->ahb_before|1u)) {e=XPL_SD_EIO;goto restore;}
    }
    if((e=wait_on(io,limit,&s->ahb_live)))goto restore;
#define READ_HC(o,w,m) do {if((e=read_reg(io,HC+(o),w,&s->m)))goto restore;s->host_reads++;} while(0)
    READ_HC(0xfeu,2,version);READ_HC(0x40u,4,caps);READ_HC(0x24u,4,present);
    READ_HC(0x28u,1,host_mode);READ_HC(0x29u,1,power);READ_HC(0x2cu,2,clock);
#undef READ_HC
    if((e=read_reg(io,CORE_MODE,4,&s->core_mode)))goto restore;
    s->host_reads++;
    if(s->version==0xffffu||!s->caps||s->caps==UINT32_MAX||s->core_mode==UINT32_MAX)e=XPL_SD_EHOST;
restore:
    if(s->enable_attempted) {
        int r=read_reg(io,AHB,4,&current);
        if(r) {s->restore_result=r;current=s->ahb_before;}
        if(io->write(io->ctx,AHB,4,(current&~1u)|(s->ahb_before&1u)))s->restore_result=XPL_SD_EIO;
    }
    if(read_reg(io,AHB,4,&s->ahb_after)||read_reg(io,APPS,4,&s->apps_after))s->restore_result=XPL_SD_EIO;
    if(!s->restore_result&&((s->ahb_after^s->ahb_before)&~0x80000000u||s->apps_after!=s->apps_before))
        s->restore_result=XPL_SD_EHOST;
    s->restored=!s->restore_result;
    return e?e:s->restore_result;
}
