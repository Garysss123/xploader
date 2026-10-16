/* SPDX-License-Identifier: MIT. Register facts pinned in research/g8-source-001. */
#include "service_probe.h"

static int read32(const struct xpl_hw_io *io,xpl_service_snapshot *s,uint64_t a,uint32_t *v) {
    s->reads++;return io->read32(io->context,a,v)?0:-2;
}
static int pmic_read(const struct xpl_hw_io *io,xpl_service_snapshot *s,
                     unsigned limit,unsigned sid,uint16_t reg,uint8_t *value) {
    uint32_t status,data;
    if((sid!=0&&sid!=2)||!(reg==0x105||reg==0x801||reg==0x805||reg==0x85a||reg==0x85b))return -1;
    /* v1 channel0, EXT_READL=1, one byte. Never write WDATA0. */
    if(!io->write32(io->context,0xfc4cf800u,(1u<<27)|(sid<<20)|((uint32_t)reg<<4)))return -2;
    s->commands++;io->fence(io->context);
    for(unsigned n=0;n<limit;n++) {
        if(read32(io,s,0xfc4cf808u,&status))return -2;
        if(status&14u)return -3;
        if(status&1u) {
            if(read32(io,s,0xfc4cf818u,&data))return -2;
            *value=(uint8_t)data;return 0;
        }
    }
    return -4;
}
int xpl_service_probe(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
                      unsigned limit,xpl_service_snapshot *s) {
    if(!s)return -1;
    *s=(xpl_service_snapshot){.pon_result={-1,-1},.usb_result=-1};
    if(!c||!c->complete||c->spmi.base!=0xfc4cf000u||c->spmi.bytes!=0x1000u||c->spmi_channel||c->spmi_ee||
       !io||!io->read32||!io->write32||!io->fence||!limit||limit>1000)return -1;
    if(read32(io,s,0xfc4cf000u,&s->arbiter))return -2;
    if(!s->arbiter||s->arbiter>=0x20010000u)return -5;
    if(read32(io,s,0xfe87f65cu,&s->reason)||read32(io,s,0xfc4ab000u,&s->pshold))return -2;
    static const uint16_t registers[]={0x105,0x801,0x805,0x85a,0x85b};
    for(unsigned pmic=0;pmic<2;pmic++) {
        s->pon_result[pmic]=0;
        for(unsigned n=0;n<sizeof registers/sizeof registers[0];n++) {
            uint8_t value=0;int e=pmic_read(io,s,limit,pmic?2:0,registers[n],&value);
            if(e){s->pon_result[pmic]=e;break;}
            s->pon[pmic]|=(uint64_t)value<<(n*8u);
        }
    }
    if(read32(io,s,0xfc4003c4u,&s->gdsc)||read32(io,s,0xfc4003c8u,&s->master)||
       read32(io,s,0xfc4003ccu,&s->sleep)||read32(io,s,0xfc4003d0u,&s->mock))return -2;
    /* A gated device MMIO read can hang the bus. Do not touch the DWC3 core
     * unless the inherited power and all three branches report live. */
    if(!(s->gdsc&0x80000000u)||(s->gdsc&1u)||!(s->master&1u)||(s->master&0x80000000u)||
       !(s->sleep&1u)||(s->sleep&0x80000000u)||!(s->mock&1u)||(s->mock&0x80000000u)) {
        s->usb_result=-6;return 0;
    }
    s->usb_result=0;
    const uint64_t addresses[]={0xf920c120u,0xf920c704u,0xf920c700u,0xf920c70cu};
    uint32_t *values[]={&s->usb_id,&s->usb_control,&s->usb_config,&s->usb_status};
    for(unsigned n=0;n<4;n++) {
        s->usb_reads++;if(read32(io,s,addresses[n],values[n])){s->usb_result=-2;return 0;}
    }
    if((s->usb_id&0xffff0000u)!=0x55330000u)s->usb_result=-5;
    return 0;
}
