/* SPDX-License-Identifier: MIT */
#include "xpl_emmc_probe.h"
#define AHB UINT64_C(0xfc4004c8)
#define APPS UINT64_C(0xfc4004c4)
#define HC UINT64_C(0xf9824900)
static int rd(const xpl_sd_board_io *io,xpl_emmc_snapshot *s,uint64_t a,unsigned w,uint32_t *v) {
    s->reads++;
    if(io->read(io->ctx,a,w,v))return XPL_SD_EIO;
    if(w==1)*v&=255u;else if(w==2)*v&=65535u;
    return 0;
}
static int wr(const xpl_sd_board_io *io,xpl_emmc_snapshot *s,uint32_t v) {
    s->writes++;return io->write(io->ctx,AHB,4,v)?XPL_SD_EIO:0;
}
int xpl_emmc_interface_probe(const xpl_sd_board_io *io,uint32_t pre,uint32_t limit,xpl_emmc_snapshot *s) {
    int e=0;uint32_t current;uint64_t start;
    if(!io||!io->read||!io->write||!io->now_us||!s||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *s=(xpl_emmc_snapshot){0};
    if((pre&3u)!=3u)return XPL_SD_EPREP;
    if((e=rd(io,s,APPS,4,&s->apps_before))||(e=rd(io,s,AHB,4,&s->ahb_before)))return e;
    if(s->apps_before==UINT32_MAX||s->ahb_before==UINT32_MAX)return XPL_SD_EHOST;
    /* Reject contradictory branch state instead of borrowing a clock from
     * a potentially active but inaccessible controller. */
    if(!(s->ahb_before&1u)&&!(s->apps_before&0x80000000u))return XPL_SD_EPREP;
    if(!(s->ahb_before&1u)) {
        s->enable_attempted=1;
        if((e=wr(io,s,s->ahb_before|1u)))goto restore;
    }
    start=io->now_us(io->ctx);
    for(uint32_t i=0;;i++) {
        if((e=rd(io,s,AHB,4,&s->ahb_live)))goto restore;
        if((s->ahb_live&0x80000001u)==1u)break;
        uint64_t now=io->now_us(io->ctx);
        if(i+1u>=limit||now<start||now-start>=100000u){e=XPL_SD_ETIMEOUT;goto restore;}
        if(io->relax)io->relax(io->ctx);
    }
#define READ(a,w,n) do {if((e=rd(io,s,(a),(w),&s->n)))goto restore;} while(0)
    READ(0xfc4004d0u,4,rcgr_cmd);READ(0xfc4004d4u,4,rcgr_cfg);
    READ(0xfc4004d8u,4,m);READ(0xfc4004dcu,4,n);READ(0xfc4004e0u,4,d);
    READ(HC+0xfeu,2,version);READ(HC+0x40u,4,caps);READ(HC+0x44u,4,caps1);
    READ(HC+0x24u,4,present);READ(HC+0x28u,1,host);READ(HC+0x29u,1,power);
    READ(HC+0x2cu,2,clock);READ(HC+0x3eu,2,host2);READ(0xf9824078u,4,core_mode);
#undef READ
    if(s->version==65535u||!s->caps||s->caps==UINT32_MAX||s->core_mode==UINT32_MAX)e=XPL_SD_EHOST;
restore:
    if(s->enable_attempted) {
        int r=rd(io,s,AHB,4,&current);
        if(r){s->restore_result=r;current=s->ahb_before;}
        if(wr(io,s,(current&~1u)|(s->ahb_before&1u)))s->restore_result=XPL_SD_EIO;
    }
    if(rd(io,s,AHB,4,&s->ahb_after)||rd(io,s,APPS,4,&s->apps_after))s->restore_result=XPL_SD_EIO;
    if(!s->restore_result&&(((s->ahb_after^s->ahb_before)&~0x80000000u)||s->apps_after!=s->apps_before))
        s->restore_result=XPL_SD_EHOST;
    s->restored=!s->restore_result;
    return e?e:s->restore_result;
}
