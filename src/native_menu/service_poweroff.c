/* SPDX-License-Identifier: MIT */
#include "service_poweroff.h"
#define CMD 0xfc4cf800u
#define STATUS 0xfc4cf808u
#define WDATA 0xfc4cf810u
#define RDATA 0xfc4cf818u
#define PSHOLD 0xfc4ab000u
static int byte_io(const struct xpl_hw_io *io,unsigned sid,unsigned reg,uint8_t *value,unsigned write,xpl_poweroff_diag *d){
    uint32_t status,data;
    if((sid!=0&&sid!=2)||(reg!=0x85a&&reg!=0x85b))return XPL_POWER_ARGUMENT;
    if(write&&!io->write32(io->context,WDATA,*value))return XPL_POWER_IO;
    if(!io->write32(io->context,CMD,(write?0u:1u<<27)|(sid<<20)|(reg<<4)))return XPL_POWER_IO;
    d->commands++;io->fence(io->context);
    for(unsigned n=0;n<1000;n++){
        if(!io->read32(io->context,STATUS,&status))return XPL_POWER_IO;
        if(status&14u)return XPL_POWER_IO;
        if(status&1u){
            if(write){d->pmic_writes++;return 0;}
            if(!io->read32(io->context,RDATA,&data))return XPL_POWER_IO;*value=(uint8_t)data;return 0;
        }
    }
    return XPL_POWER_IO;
}
static int set_byte(const struct xpl_hw_io *io,unsigned sid,unsigned reg,uint8_t value,xpl_poweroff_diag *d){
    uint8_t actual;if(byte_io(io,sid,reg,&value,1,d)||byte_io(io,sid,reg,&actual,0,d))return XPL_POWER_IO;
    return actual==value?0:XPL_POWER_CHANGED;
}
static int configure(const struct xpl_hw_io *io,xpl_power_wait_fn wait,void *context,unsigned index,uint8_t control,uint8_t enabled,xpl_poweroff_diag *d){
    unsigned sid=index?2u:0u;
    if(set_byte(io,sid,0x85b,(uint8_t)(enabled&0x7fu),d))return XPL_POWER_IO;
    if(wait(context,500))return XPL_POWER_DELAY;
    if(set_byte(io,sid,0x85a,control,d)||set_byte(io,sid,0x85b,enabled,d))return XPL_POWER_IO;
    return 0;
}
static int restore(const struct xpl_hw_io *io,xpl_power_wait_fn wait,void *context,xpl_poweroff_diag *d){
    int result=0;
    for(unsigned n=0;n<2;n++)if(d->changed&(1u<<n))if(configure(io,wait,context,n,d->control[n],d->enable[n],d))result=XPL_POWER_ROLLBACK;
    d->rollback=result;return result;
}
int xpl_service_poweroff_trigger(const struct xpl_hw_config *c,const struct xpl_hw_io *io,xpl_power_wait_fn wait,void *context,xpl_poweroff_diag *d){
    xpl_service_snapshot s;
    if(!d)return XPL_POWER_ARGUMENT;*d=(xpl_poweroff_diag){0};
    if(!io||!io->read32||!io->write32||!io->fence||!wait||xpl_service_probe(c,io,1000,&s))return XPL_POWER_ARGUMENT;
    if(s.pshold!=1)return XPL_POWER_CHANGED;
    for(unsigned n=0;n<2;n++){
        uint32_t subtype=(uint32_t)s.pon[n]&255u,rev=(uint32_t)(s.pon[n]>>8)&255u;
        d->control[n]=(uint8_t)(s.pon[n]>>24);d->enable[n]=(uint8_t)(s.pon[n]>>32);
        if(s.pon_result[n]||subtype!=(n?10u:9u)||!rev||(d->control[n]&15u)!=1u||!(d->enable[n]&128u))return XPL_POWER_PON;
    }
    if(!io->read32(io->context,0xfe87f000u,&d->dload0)||!io->read32(io->context,0xfe87f004u,&d->dload1))return XPL_POWER_IO;
    if(!xpl_service_dload_idle(d->dload0,d->dload1))return XPL_POWER_CHANGED;
    /* Validate the architectural timer before the first PMIC modification. */
    if(wait(context,1))return XPL_POWER_DELAY;
    d->phase=1;
    for(unsigned n=0;n<2;n++){
        d->changed|=1u<<n;
        int e=configure(io,wait,context,n,(uint8_t)((d->control[n]&0xf0u)|4u),d->enable[n],d);
        if(e){int r=restore(io,wait,context,d);return r?r:e;}
    }
    d->phase=2;
    if(!io->write32(io->context,PSHOLD,0)){int r=restore(io,wait,context,d);return r?r:XPL_POWER_IO;}
    io->fence(io->context);d->phase=3;return XPL_POWER_TRIGGERED;
}
int xpl_service_poweroff_returned(const struct xpl_hw_io *io,xpl_power_wait_fn wait,void *context,xpl_poweroff_diag *d){
    if(!io||!wait||!d||d->phase!=3)return XPL_POWER_ARGUMENT;
    for(unsigned n=0;n<2;n++){
        uint8_t actual;if(byte_io(io,n?2:0,0x85a,&actual,0,d)||actual!=(uint8_t)((d->control[n]&0xf0u)|4u))return XPL_POWER_CHANGED;
    }
    int r=restore(io,wait,context,d);if(!r)d->phase=4;return r;
}
