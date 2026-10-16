/* SPDX-License-Identifier: MIT. Register/frequency facts from pinned MSM8994 LK. */
#include "usb_power_probe.h"
#define LIMIT 50000u
enum {GDSC,IFACE,MASTER,SLEEP,MOCK,MCMD,MCFG,M,N,D,UCMD,UCFG};
static const uint64_t addresses[XPL_USB_POWER_REGS]={
    0xfc4003c4u,0xfc4003fcu,0xfc4003c8u,0xfc4003ccu,0xfc4003d0u,
    0xfc4003d4u,0xfc4003d8u,0xfc4003dcu,0xfc4003e0u,0xfc4003e4u,0xfc4003e8u,0xfc4003ecu};
static int read32(const xpl_sd_board_io *io,xpl_usb_power_diag *d,uint64_t a,uint32_t *v){
    d->reads++;return io->read(io->ctx,a,4,v)?-2:0;
}
static int write32(const xpl_sd_board_io *io,xpl_usb_power_diag *d,unsigned index,uint32_t value){
    if(index>=XPL_USB_POWER_REGS)return -1;
    d->writes++;return io->write(io->ctx,addresses[index],4,value)?-2:0;
}
static int wait_bits(const xpl_sd_board_io *io,xpl_usb_power_diag *d,unsigned index,uint32_t mask,uint32_t expected){
    uint64_t begin=io->now_us(io->ctx);if(begin==UINT64_MAX)return -3;
    for(unsigned n=0;n<LIMIT;n++){
        uint32_t value;if(read32(io,d,addresses[index],&value))return -2;
        if((value&mask)==expected)return 0;
        uint64_t now=io->now_us(io->ctx);if(now==UINT64_MAX||now<begin)return -3;
        if(now-begin>20000u)return -4;
        if(io->relax)io->relax(io->ctx);
    }return -4;
}
static int update(const xpl_sd_board_io *io,xpl_usb_power_diag *d,unsigned index){
    if(write32(io,d,index,d->before[index]|1u))return -2;
    return wait_bits(io,d,index,1u,0);
}
static uint32_t mask(unsigned index){return index<=MOCK?1u:index==MCMD||index==UCMD?3u:UINT32_MAX;}
static uint64_t hash(const uint32_t *values){
    uint64_t h=UINT64_C(0xcbf29ce484222325);
    for(unsigned i=0;i<XPL_USB_POWER_REGS;i++)for(unsigned b=0;b<4;b++){
        h^=(values[i]&mask(i))>>(b*8u)&255u;h*=UINT64_C(0x100000001b3);
    }return h;
}
static int power_transaction(const xpl_sd_board_io *io,xpl_usb_power_diag *d,int keep){
    if(!d)return -1;*d=(xpl_usb_power_diag){.result=-1};
    if(!io||!io->read||!io->write||!io->now_us)return -1;
    for(unsigned i=0;i<XPL_USB_POWER_REGS;i++)if(read32(io,d,addresses[i],&d->before[i]))return d->result=-2;
    d->before_hash=hash(d->before);d->phase=1;
    if(read32(io,d,0xfc400000u,&d->pll_mode)||read32(io,d,0xfc400004u,&d->pll_l)||
       read32(io,d,0xfc400008u,&d->pll_alpha)||read32(io,d,0xfc40000cu,&d->pll_alpha_hi)||
       read32(io,d,0xfc4003c0u,&d->bcr))return d->result=-2;
    /* Do not alter an active controller, a busy root, shared PLL or reset. */
    if(!(d->before[GDSC]&1u)||(d->before[GDSC]&0x80000000u)||d->bcr&1u||
       (d->before[MCMD]&1u)||(d->before[UCMD]&1u))return d->result=-5;
    if(!(d->pll_mode&0x80000000u)||d->pll_l!=31u||d->pll_alpha||d->pll_alpha_hi!=0x40u)return d->result=-6;
    int e;d->phase=2;
    if((e=write32(io,d,GDSC,d->before[GDSC]&~1u))||(e=wait_bits(io,d,GDSC,0x80000000u,0x80000000u)))goto cleanup;
    d->phase=3;
    if((e=write32(io,d,M,5u))||(e=write32(io,d,N,0xffecu))||(e=write32(io,d,D,0xffe7u))||
       (e=write32(io,d,MCFG,(d->before[MCFG]&~0x371fu)|0x2101u))||(e=update(io,d,MCMD))||
       (e=write32(io,d,UCFG,(d->before[UCFG]&~0x371fu)|0x113u))||(e=update(io,d,UCMD)))goto cleanup;
    d->phase=4;
    for(unsigned i=IFACE;i<=MOCK;i++){
        if((e=write32(io,d,i,d->before[i]|1u))||(e=wait_bits(io,d,i,0x80000000u,0)))goto cleanup;
    }
    d->phase=5;
    if((e=read32(io,d,0xf920c120u,&d->id))||(e=read32(io,d,0xf920c704u,&d->dctl))||
       (e=read32(io,d,0xf920c70cu,&d->dsts)))goto cleanup;
    e=(d->id&0xffff0000u)==0x55330000u?0:-7;
cleanup:
    d->result=e;
    if(!e&&keep)return 0;
    return xpl_usb_power_release(io,d);
}
int xpl_usb_power_release(const xpl_sd_board_io *io,xpl_usb_power_diag *d){
    if(!io||!io->read||!io->write||!io->now_us||!d||d->phase<2||d->restore_mask==0xfffu)return -1;
    /* Close the clocks before restoring roots; never restore by changing PLL. */
    for(unsigned i=MOCK;i>=IFACE;i--)if(write32(io,d,i,d->before[i]))d->cleanup_result=-2;
    for(unsigned i=MCFG;i<=D;i++)if(write32(io,d,i,d->before[i]))d->cleanup_result=-2;
    if(update(io,d,MCMD))d->cleanup_result=-2;
    if(write32(io,d,UCFG,d->before[UCFG])||update(io,d,UCMD))d->cleanup_result=-2;
    if(write32(io,d,GDSC,d->before[GDSC])||wait_bits(io,d,GDSC,0x80000000u,0))d->cleanup_result=-2;
    for(unsigned i=0;i<XPL_USB_POWER_REGS;i++){
        if(read32(io,d,addresses[i],&d->after[i])){d->cleanup_result=-2;continue;}
        if((d->before[i]&mask(i))==(d->after[i]&mask(i)))d->restore_mask|=1u<<i;
    }
    d->after_hash=hash(d->after);
    if(d->restore_mask!=0xfffu)d->cleanup_result=-8;
    return d->cleanup_result?d->cleanup_result:d->result;
}
int xpl_usb_power_probe(const xpl_sd_board_io *io,xpl_usb_power_diag *d){return power_transaction(io,d,0);}
int xpl_usb_power_acquire(const xpl_sd_board_io *io,xpl_usb_power_diag *d){return power_transaction(io,d,1);}
