/* SPDX-License-Identifier: MIT */
#include "xpl_sd_powered.h"
#define HC UINT64_C(0xf98a4900)
#define CORE UINT64_C(0xf98a4000)
#define PAD UINT64_C(0xfd512048)
static int rd(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t *v) {
    if(io->read(io->ctx,a,w,v))return XPL_SD_EIO;
    if(w==1)*v&=255u;else if(w==2)*v&=65535u;return 0;
}
static int wr(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t v) {
    return io->write(io->ctx,a,w,v)?XPL_SD_EIO:0;
}
static int update(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t mask,uint32_t v) {
    uint32_t old;int e=rd(io,a,w,&old);return e?e:wr(io,a,w,(old&~mask)|(v&mask));
}
static int poll(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t mask,uint32_t v,
                 uint64_t budget,uint32_t limit,uint32_t *last) {
    uint64_t start=io->now_us(io->ctx);
    for(uint32_t n=0;n<limit;n++) {
        int e=rd(io,a,w,last);if(e)return e;
        if((*last&mask)==v)return 0;
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=budget)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int delay(const xpl_sd_board_io *io,uint32_t limit) {
    uint64_t start=io->now_us(io->ctx);
    for(uint32_t n=0;n<limit;n++) {
        uint64_t now=io->now_us(io->ctx);if(now<start)return XPL_SD_ETIMEOUT;
        if(now-start>=1000)return 0;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int detection_command(const xpl_sd_board_io *io,unsigned cmd,uint32_t arg,uint32_t limit,
                               xpl_sd_powered_diag *d) {
    uint32_t v;int e;
    if((e=poll(io,HC+0x24u,4,3u,0,250000,limit,&v))||
       (e=wr(io,HC+0x30u,4,UINT32_MAX))||(e=wr(io,HC+8u,4,arg))||
       (e=wr(io,HC+0x0eu,2,(cmd<<8)|(cmd==8?0x1au:0))))return e;
    uint64_t start=io->now_us(io->ctx);
    for(uint32_t n=0;n<limit;n++) {
        if((e=rd(io,HC+0x30u,4,&d->last_irq)))return e;
        if(d->last_irq&0xffff8000u)return XPL_SD_EHOST;
        if(d->last_irq&1u) {
            if(cmd==8&&(e=rd(io,HC+0x10u,4,&d->r7)))return e;
            return wr(io,HC+0x30u,4,1u);
        }
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=250000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
int xpl_sd_prepare_legacy_owned(const xpl_sd_board_io *io,uint32_t limit,
                           const xpl_sd_ldo_state *l13,const xpl_sd_ldo_state *l21,
                           unsigned warm_owned,
                           xpl_sd_prepared *p,xpl_sd_powered_diag *d) {
    uint32_t v,old;int e;xpl_sd_board_snapshot b;
    if(!io||!io->read||!io->write||!io->now_us||!p||!d||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *p=(xpl_sd_prepared){0};*d=(xpl_sd_powered_diag){0};d->phase=1;
    if(!xpl_sd_ldo_is_2950(l13)||!xpl_sd_ldo_is_2950(l21))return XPL_SD_EPREP;
    if((e=xpl_sd_board_probe(io,&b)))return e;
    if((b.gcc_apps&0x80000001u)!=1u||(b.gcc_ahb&0x80000001u)!=1u||
       (b.rcgr_cfg&0x371fu)!=0x2017u||b.m!=1u||(b.n&255u)!=252u||(b.d&255u)!=251u)return XPL_SD_EPREP;
    d->verified=XPL_SD_PREP_CLOCKS|XPL_SD_PREP_POWER_2950|XPL_SD_PREP_EXCLUSIVE;
    if((e=rd(io,HC+0x29u,1,&d->power_before)))return e;
    if(d->power_before&&!(warm_owned&&d->power_before==0x0bu))return XPL_SD_EPREP;
    if((e=rd(io,CORE+0xe0u,4,&d->mask_before))||
       (e=rd(io,CORE+0xe8u,4,&d->control_before)))return e;
    d->started=1;d->phase=2;
    if((e=wr(io,CORE+0x78u,4,0))||(e=update(io,CORE,4,0x80u,0x80u))||
       (e=poll(io,CORE,4,0x80u,0,1000,limit,&v))||(e=wr(io,CORE+0x78u,4,0x2001u)))return e;
    d->verified|=XPL_SD_PREP_HC_MODE;
    /* Same pre-IRQ stale-status cleanup as the exact Sony driver. It earns no
     * power-ready flag: actual new BUS_ON must still be observed below. */
    if((e=rd(io,CORE+0xdcu,4,&v))||(e=wr(io,CORE+0xe4u,4,v&15u)))return e;
    old=d->control_before;if(v&3u)old|=1u;if(v&12u)old|=4u;
    if((e=wr(io,CORE+0xe8u,4,old))||
       (e=update(io,HC+0x10cu,4,0x18302u,0x08200u))||
       (e=update(io,HC+0x28u,1,0x26u,0))||
       (e=update(io,HC+0x3eu,2,0x80cfu,0))||
       (e=rd(io,PAD,4,&d->pads_before)))return e;
    d->pads_changed=1;
    if((e=update(io,PAD,4,0xdfffu,0x1f24u))||(e=rd(io,PAD,4,&d->pads_after)))return e;
    if((d->pads_after&0xdfffu)!=0x1f24u)return XPL_SD_EHOST;
    d->verified|=XPL_SD_PREP_PINS|XPL_SD_PREP_LEGACY;d->phase=3;
    if((e=wr(io,CORE+0xe0u,4,15u))||(e=rd(io,HC+0x29u,1,&d->power_after_reset)))return e;
    if(d->power_after_reset==0u) {
        if((e=wr(io,HC+0x29u,1,0x0bu))||
           (e=poll(io,CORE+0xdcu,4,2u,2u,100000,limit,&d->request)))return e;
        if(d->request&5u)return XPL_SD_EPREP;
        d->ack=1u|((d->request&8u)?4u:0);
        if((e=wr(io,CORE+0xe4u,4,d->request&15u))||(e=wr(io,CORE+0xe8u,4,d->ack)))return e;
    }else {
        if(!warm_owned||d->power_before!=0x0bu||d->power_after_reset!=0x0bu)return XPL_SD_EPREP;
        if((e=rd(io,CORE+0xdcu,4,&d->request))||(e=rd(io,HC+0x10cu,4,&d->vendor)))return e;
        if((d->request&5u)||(d->vendor&0x10000u))return XPL_SD_EPREP;
        d->ack=((d->request&2u)?1u:0u)|((d->request&8u)?4u:0u);
        if(d->request&15u) {
            if((e=wr(io,CORE+0xe4u,4,d->request&15u))||(e=wr(io,CORE+0xe8u,4,d->ack)))return e;
        }
        d->warm_reused=1;
    }
    if((e=rd(io,HC+0x10cu,4,&d->vendor)))return e;
    if(d->vendor&0x10000u)return XPL_SD_EHOST;
    d->verified|=XPL_SD_PREP_PWR_HANDSHAKE;d->phase=4;
    if((e=wr(io,HC+0x2cu,2,1u))||(e=poll(io,HC+0x2cu,2,2u,2u,100000,limit,&v))||
       (e=wr(io,HC+0x2cu,2,7u))||(e=delay(io,limit))||
       (e=wr(io,HC+0x38u,4,0))||(e=wr(io,HC+0x34u,4,0xffff8001u))||
       (e=wr(io,HC+0x2eu,1,14u))||(e=detection_command(io,0,0,limit,d))||
       (e=detection_command(io,8,0x1aau,limit,d)))return e;
    if((d->r7&0xfffu)!=0x1aau)return XPL_SD_ECARD;
    d->verified|=XPL_SD_PREP_CARD_PRESENT;d->phase=5;
    p->verified=d->verified;p->card_clock_hz=400000;return 0;
}
int xpl_sd_prepare_legacy(const xpl_sd_board_io *io,uint32_t limit,
    const xpl_sd_ldo_state *l13,const xpl_sd_ldo_state *l21,xpl_sd_prepared *p,xpl_sd_powered_diag *d) {
    return xpl_sd_prepare_legacy_owned(io,limit,l13,l21,0,p,d);
}
int xpl_sd_cleanup_legacy(const xpl_sd_board_io *io,uint32_t limit,xpl_sd_powered_diag *d) {
    (void)limit;int e=0,r;
    if(!io||!d)return XPL_SD_EINVAL;
    if(d->started) {
        /* Stop output before the enclosing HC_MODE/clock and HPM restore.
         * Inherited regulators remain enabled at their original voltage.
         * No new BUS_OFF success is fabricated; the next Linux driver performs
         * its own core reset and regulator/power initialization. */
        r=wr(io,HC+0x2cu,2,1u);if(r&&!e)e=r;
        r=wr(io,HC+0x38u,4,0);if(r&&!e)e=r;
        r=wr(io,CORE+0xe0u,4,d->mask_before);if(r&&!e)e=r;
    }
    if(d->pads_changed) {r=wr(io,PAD,4,d->pads_before);if(r&&!e)e=r;}
    d->cleanup_result=e;return e;
}
