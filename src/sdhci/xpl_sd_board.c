/* SPDX-License-Identifier: MIT
 * Verified GCC SDCC2/pad subset. Full power/core preparation is not implemented.
 */
#include "xpl_sd_board.h"
#define GCC_APPS UINT64_C(0xfc400504)
#define GCC_AHB  UINT64_C(0xfc400508)
#define GCC_CMD  UINT64_C(0xfc400510)
#define GCC_CFG  UINT64_C(0xfc400514)
#define GCC_M    UINT64_C(0xfc400518)
#define GCC_N    UINT64_C(0xfc40051c)
#define GCC_D    UINT64_C(0xfc400520)
#define SDC2_PAD UINT64_C(0xfd512048)
#define HC_CLOCK UINT64_C(0xf98a492c)
#define HC_PRESENT UINT64_C(0xf98a4924)
#define SPMI_CORE UINT64_C(0xfc4cf000)
#define SPMI_CMD UINT64_C(0xfc4cf800)
#define SPMI_STATUS UINT64_C(0xfc4cf808)
#define SPMI_RDATA UINT64_C(0xfc4cf818)
#define CFG_MASK 0x371fu
#define CFG_400K 0x2017u
#define PAD_MASK 0xdfffu
#define PAD_ACTIVE 0x1f24u
static int valid(const xpl_sd_board_io *io){return io&&io->read&&io->write&&io->now_us;}
static int allowed(uint64_t a,unsigned w,int write){
    if(w==2)return a==HC_CLOCK;
    if(w!=4)return 0;
    if(a==SPMI_CMD)return write;
    if(a==SPMI_CORE||a==SPMI_STATUS||a==SPMI_RDATA)return !write;
    if(!write&&a==HC_PRESENT)return 1;
    return a==GCC_APPS||a==GCC_AHB||a==GCC_CMD||a==GCC_CFG||a==GCC_M||a==GCC_N||a==GCC_D||a==SDC2_PAD;
}
static int read_reg(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t *v){
    if(!allowed(a,w,0)||io->read(io->ctx,a,w,v))return XPL_SD_EIO;
    if(w==2)*v&=0xffffu;
    return 0;
}
static int write_reg(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t v){
    if(!allowed(a,w,1)||io->write(io->ctx,a,w,v))return XPL_SD_EIO;
    return 0;
}
static int update(const xpl_sd_board_io *io,uint64_t a,unsigned w,uint32_t mask,uint32_t value){
    uint32_t v;int e;
    if((e=read_reg(io,a,w,&v)))return e;
    return write_reg(io,a,w,(v&~mask)|(value&mask));
}
static int poll(const xpl_sd_board_io *io,uint64_t a,uint32_t mask,uint32_t value,uint32_t limit){
    uint64_t start=io->now_us(io->ctx);uint32_t n,v;int e;
    for(n=0;n<limit;n++){
        if((e=read_reg(io,a,4,&v)))return e;
        if((v&mask)==value)return 0;
        if(io->now_us(io->ctx)-start>=100000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
int xpl_sd_board_probe(const xpl_sd_board_io *io,xpl_sd_board_snapshot *s){
    int e;if(!valid(io)||!s)return XPL_SD_EINVAL;
#define R(a,m) do{if((e=read_reg(io,a,4,&s->m)))return e;}while(0)
    R(GCC_APPS,gcc_apps);R(GCC_AHB,gcc_ahb);R(GCC_CMD,rcgr_cmd);R(GCC_CFG,rcgr_cfg);
    R(GCC_M,m);R(GCC_N,n);R(GCC_D,d);R(SDC2_PAD,pads);
#undef R
    if(s->gcc_apps==0xffffffffu||s->gcc_ahb==0xffffffffu||s->rcgr_cmd==0xffffffffu)return XPL_SD_EHOST;
    return 0;
}
static int pmic_read(const xpl_sd_board_io *io,unsigned sid,uint16_t addr,
                     uint32_t limit,uint8_t *out){
    uint64_t start=io->now_us(io->ctx);uint32_t v,n;int e;
    /* Only these thirteen PMIC addresses can be requested by this module. */
    if(!((sid==0&&addr==0x105)||(sid==1&&((addr&0xff00u)==0x4c00u||(addr&0xff00u)==0x5400u)&&
         ((addr&255u)==4||(addr&255u)==5||(addr&255u)==0x40||(addr&255u)==0x41||
          (addr&255u)==0x45||(addr&255u)==0x46))))return XPL_SD_EINVAL;
    if((e=write_reg(io,SPMI_CMD,4,(1u<<27)|(sid<<20)|((uint32_t)addr<<4))))return e;
    for(n=0;n<limit;n++){
        if((e=read_reg(io,SPMI_STATUS,4,&v)))return e;
        if(v&14u)return XPL_SD_EHOST;
        if(v&1u){if((e=read_reg(io,SPMI_RDATA,4,&v)))return e;*out=(uint8_t)v;return 0;}
        if(io->now_us(io->ctx)-start>=10000)return XPL_SD_ETIMEOUT;
        if(io->relax)io->relax(io->ctx);
    }
    return XPL_SD_ETIMEOUT;
}
int xpl_sd_board_pmic_snapshot(const xpl_sd_board_io *io,uint64_t base,uint32_t bytes,
                              uint32_t channel,uint32_t ee,uint32_t limit,
                              xpl_sd_ldo_state *l13,xpl_sd_ldo_state *l21){
    uint32_t version;uint8_t subtype;unsigned i;int e;
    if(!valid(io)||!l13||!l21||l13==l21||base!=SPMI_CORE||bytes!=0x1000||channel||ee||!limit||limit>1000)
        return XPL_SD_EINVAL;
    if((e=read_reg(io,SPMI_CORE,4,&version)))return e;
    if(!version||version>=0x20010000u)return XPL_SD_EUNSUPPORTED;
    if((e=pmic_read(io,0,0x105,limit,&subtype)))return e;
    if(subtype!=9)return XPL_SD_EUNSUPPORTED;
    for(i=0;i<2;i++){
        xpl_sd_ldo_state *s=i?l21:l13;uint16_t b=(uint16_t)(i?0x5400:0x4c00);
        s->pmic_subtype=subtype;
        if((e=pmic_read(io,1,b+4,limit,&s->type))||(e=pmic_read(io,1,b+5,limit,&s->subtype))||
           (e=pmic_read(io,1,b+0x40,limit,&s->range))||(e=pmic_read(io,1,b+0x41,limit,&s->selector))||
           (e=pmic_read(io,1,b+0x45,limit,&s->mode))||(e=pmic_read(io,1,b+0x46,limit,&s->enable)))return e;
    }
    return 0;
}
int xpl_sd_board_clock_pads(const xpl_sd_board_io *io,uint32_t prerequisites,uint32_t limit,uint32_t *completed){
    xpl_sd_board_snapshot s;uint32_t v;int e;
    if(completed)*completed=0;
    if(!valid(io)||!completed||!limit||limit>1000000u)return XPL_SD_EINVAL;
    if((prerequisites&XPL_SD_BOARD_CLOCK_PAD_PREREQS)!=XPL_SD_BOARD_CLOCK_PAD_PREREQS)return XPL_SD_EPREP;
    /* Host is already accessible by contract; refuse a live command/data path. */
    if((e=read_reg(io,HC_PRESENT,4,&v)))return e;
    if(v&3u)return XPL_SD_EPREP;
    if((e=update(io,HC_CLOCK,2,4u,0))||
       (e=update(io,GCC_AHB,4,1u,1u))||(e=poll(io,GCC_AHB,0x80000000u,0,limit))||
       (e=update(io,GCC_APPS,4,1u,1u))||(e=poll(io,GCC_APPS,0x80000000u,0,limit)))return e;
    /* LK F(400000,cxo,12,1,4): M=1,N=~3,D=~4, source0,HID23,dual-edge. */
    if((e=write_reg(io,GCC_M,4,1u))||(e=write_reg(io,GCC_N,4,~3u))||
       (e=write_reg(io,GCC_D,4,~4u))||(e=update(io,GCC_CFG,4,CFG_MASK,CFG_400K))||
       (e=update(io,GCC_CMD,4,1u,1u))||(e=poll(io,GCC_CMD,1u,0,limit))||
       (e=update(io,SDC2_PAD,4,PAD_MASK,PAD_ACTIVE))||(e=xpl_sd_board_probe(io,&s)))return e;
    if(!(s.gcc_apps&1)||!(s.gcc_ahb&1)||(s.gcc_apps&0x80000000u)||(s.gcc_ahb&0x80000000u)||
       (s.rcgr_cmd&1)||(s.rcgr_cfg&CFG_MASK)!=CFG_400K||s.m!=1||
       (s.n&0xffu)!=0xfcu||(s.d&0xffu)!=0xfbu||(s.pads&PAD_MASK)!=PAD_ACTIVE)return XPL_SD_EHOST;
    *completed=XPL_SD_PREP_CLOCKS|XPL_SD_PREP_PINS;return 0;
}
static int pldo(const xpl_sd_ldo_state *s){
    return s&&s->pmic_subtype==9&&s->type==4&&
        ((s->subtype>=8&&s->subtype<=12)||(s->subtype>=0x28&&s->subtype<=0x2c));
}
int xpl_sd_ldo_is_2950(const xpl_sd_ldo_state *s){
    return pldo(s)&&s->range==3&&s->selector==58&&s->mode==0x80&&s->enable==0x80;
}
int xpl_sd_ldo_plan_2950(unsigned ldo,const xpl_sd_ldo_state *s,xpl_sd_pmic_write out[4]){
    uint16_t base;
    if(!out||!s||(ldo!=13&&ldo!=21))return XPL_SD_EINVAL;
    if(!pldo(s))return XPL_SD_EUNSUPPORTED;
    /* Range changes on live PLDO can cause voltage spikes; do not plan them. */
    if(s->enable||s->mode&0x3fu)return XPL_SD_EPREP;
    base=(uint16_t)(ldo==13?0x4c00:0x5400);
    out[0]=(xpl_sd_pmic_write){1,0x80,(uint16_t)(base+0x45)};
    out[1]=(xpl_sd_pmic_write){1,3,(uint16_t)(base+0x40)};
    out[2]=(xpl_sd_pmic_write){1,58,(uint16_t)(base+0x41)};
    out[3]=(xpl_sd_pmic_write){1,0x80,(uint16_t)(base+0x46)};
    return 0;
}
