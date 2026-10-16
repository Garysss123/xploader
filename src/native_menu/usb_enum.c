/* SPDX-License-Identifier: MIT. Polled MSM8994 DWC3 EP0, pinned g8-source-001/003. */
#include "usb_enum.h"
#include "usb_control.h"
#include <stddef.h>
#define CORE 0xf9200000u
#define GCTL 0xc110u
#define DCFG 0xc700u
#define DCTL 0xc704u
#define DSTS 0xc70cu
#define RUN (1u<<31)
typedef struct {volatile uint32_t lo,hi,size,control;} trb;
typedef struct {volatile uint32_t events[1024];trb transfers[4];uint8_t setup[8],control[512],bulk_out[512],bulk_in[512];} usb_memory;
_Static_assert(offsetof(usb_memory,transfers)==4096,"TRB alignment");
_Static_assert(sizeof(usb_memory)<8192,"DMA workspace bound");
typedef struct {
    const xpl_sd_board_io *io;xpl_usb_enum_diag *d;usb_memory *memory;uint64_t pa;
    xpl_usb_control control;uint32_t ring,resource[4],aux[8];unsigned aux_count,powered,core_touched;
    unsigned state,status_ep,status_type,active[4],bulk_ready,bulk_in_bytes;xpl_usb_msd *medium;
} usb;
static void fence(void){
#ifdef __aarch64__
    __asm__ volatile("dsb sy":::"memory");
#else
    __asm__ volatile("":::"memory");
#endif
}
static int rd(usb *u,uint64_t a,uint32_t *v){return u->io->read(u->io->ctx,a,4,v)?-2:0;}
static int wr(usb *u,uint64_t a,uint32_t v){int e=u->io->write(u->io->ctx,a,4,v)?-2:0;fence();return e;}
static int wait_mask(usb *u,uint64_t a,uint32_t mask,uint32_t target,uint64_t limit){
    uint64_t begin=u->io->now_us(u->io->ctx);if(begin==UINT64_MAX)return -3;
    for(unsigned n=0;n<100000;n++){
        uint32_t value;if(rd(u,a,&value))return -2;if((value&mask)==target)return 0;
        uint64_t now=u->io->now_us(u->io->ctx);if(now==UINT64_MAX||now<begin)return -3;
        if(now-begin>limit)return -4;if(u->io->relax)u->io->relax(u->io->ctx);
    }return -4;
}
static int delay(usb *u,uint64_t length){
    uint64_t begin=u->io->now_us(u->io->ctx);if(begin==UINT64_MAX)return -3;
    for(unsigned n=0;n<10000000;n++){
        uint64_t now=u->io->now_us(u->io->ctx);if(now==UINT64_MAX||now<begin)return -3;
        if(now-begin>=length)return 0;if(u->io->relax)u->io->relax(u->io->ctx);
    }return -4;
}
static int rmw(usb *u,uint64_t a,uint32_t clear,uint32_t set){uint32_t v;if(rd(u,a,&v))return -2;return wr(u,a,(v&~clear)|set);}
static int command(usb *u,unsigned ep,uint32_t op,uint32_t p0,uint32_t p1,uint32_t p2){
    if(ep>3)return -1;uint64_t a=CORE+0xc800u+ep*16u;uint32_t status;
    u->d->last_command=(ep<<16)|op;
    if(wait_mask(u,a+12,1u<<10,0,20000)||wr(u,a,p2)||wr(u,a+4,p1)||wr(u,a+8,p0)||wr(u,a+12,op|(1u<<10))||
       wait_mask(u,a+12,1u<<10,0,20000)||rd(u,a+12,&status))return -5;
    u->d->last_command_value=status;if(status&0xf000u)return -6;
    if((op&15u)==6u)u->resource[ep]=(status>>16)&127u;return 0;
}
static int transfer(usb *u,unsigned ep,unsigned type,uint32_t offset,unsigned bytes){
    if(ep>3||offset>8192||bytes>512||bytes>8192-offset)return -1;
    trb *t=&u->memory->transfers[ep];t->lo=(uint32_t)(u->pa+offset);t->hi=0;t->size=bytes;
    t->control=1u|2u|(type<<4)|(1u<<10)|(1u<<11);fence();
    int e=command(u,ep,6,0,(uint32_t)(u->pa+offsetof(usb_memory,transfers)+ep*sizeof(trb)),0);
    if(!e)u->active[ep]=1;return e;
}
static int setup_arm(usb *u){u->state=0;return transfer(u,0,2,(uint32_t)offsetof(usb_memory,setup),8);}
static int bulk_kick(usb *u){
    if(!u->medium||!u->bulk_ready||!u->control.configured)return 0;
    unsigned phase=u->medium->phase;
    if((phase==XPL_MSD_CBW||phase==XPL_MSD_DISCARD_OUT||phase==XPL_MSD_WRITE_OUT)&&!u->active[2]&&!(u->control.halted&1u))
        return transfer(u,2,1,(uint32_t)offsetof(usb_memory,bulk_out),512);
    if((phase==XPL_MSD_IN||phase==XPL_MSD_CSW||phase==XPL_MSD_ZLP)&&!u->active[3]&&!(u->control.halted&2u)){
        size_t bytes=0;if(xpl_usb_msd_prepare_in(u->medium,u->memory->bulk_in,&bytes)||bytes>512)return -32;
        u->bulk_in_bytes=(unsigned)bytes;return transfer(u,3,1,(uint32_t)offsetof(usb_memory,bulk_in),(unsigned)bytes);
    }return 0;
}
static int bulk_reset(usb *u){
    for(unsigned ep=2;ep<4;ep++)if(u->active[ep]){(void)command(u,ep,8u|(1u<<11)|(u->resource[ep]<<16),0,0,0);u->active[ep]=0;}
    xpl_usb_msd_reset(u->medium);return 0;
}
static int bulk_configure(usb *u){
    if(!u->control.configured){u->bulk_ready=0;return bulk_reset(u);}
    uint32_t parameters;if(rd(u,CORE+0xc15cu,&parameters))return -2;
    u->d->fifo_ram=parameters&0xffffu;
    if(u->d->fifo_ram<768u)return -42;
    /* Fixed two-IN-endpoint allocation, in hardware RAM1 words. EP0 has no
     * active IN transfer while handling SET_CONFIGURATION. */
    u->d->fifo0=256u;u->d->fifo1=(256u<<16)|512u;
    if(wr(u,CORE+0xc300u,u->d->fifo0)||wr(u,CORE+0xc304u,u->d->fifo1))return -2;
    if(command(u,0,9u|(2u<<16),0,0,0))return -33;
    for(unsigned ep=2;ep<4;ep++){
        uint32_t p0=(512u<<3)|(2u<<1)|((ep==3?1u:0u)<<17);
        if(command(u,ep,1,p0,(ep<<25)|(1u<<8)|(1u<<10),0)||command(u,ep,2,1,0,0))return -34;
    }
    if(wr(u,CORE+0xc720u,15))return -2;u->bulk_ready=1;xpl_usb_msd_reset(u->medium);return bulk_kick(u);
}
static int setup_done(usb *u){
    uint8_t q[8];for(unsigned n=0;n<8;n++)q[n]=u->memory->setup[n];
    u->d->last_setup=0;for(unsigned n=0;n<8;n++)u->d->last_setup|=(uint64_t)q[n]<<(n*8u);
    u->d->setup_history[u->d->setups%4u]=u->d->last_setup;u->d->setups++;size_t bytes=0;
    int r=xpl_usb_control_request(&u->control,q,u->memory->control,sizeof u->memory->control,&bytes);
    if(r<0){if(command(u,0,4,0,0,0))return -7;return setup_arm(u);}
    unsigned requested=(unsigned)q[6]|(unsigned)q[7]<<8;
    /* DWC3 latches DEVADDR for the control status transaction. Match the
     * earned kernel's ep0_set_address ordering, before queuing status IN. */
    if(q[0]==0&&q[1]==5){
        if(rmw(u,CORE+DCFG,0x7fu<<3,(uint32_t)u->control.address<<3)||rd(u,CORE+DCFG,&u->d->address_register))return -2;
        u->d->address=u->control.address;
    }
    if(u->medium){
        if(q[0]==0&&q[1]==9){if(bulk_configure(u))return -35;}
        else if(q[0]==0x21&&q[1]==0xff){if(bulk_reset(u)||bulk_kick(u))return -36;}
        else if(q[0]==2&&q[1]==1){unsigned ep=q[4]==1?2u:3u;if(command(u,ep,5,0,0,0)||bulk_kick(u))return -37;}
    }
    if(r==XPL_USB_CONTROL_IN&&requested){
        u->state=1;u->status_ep=0;u->status_type=4;
        return transfer(u,1,5,(uint32_t)offsetof(usb_memory,control),(unsigned)bytes);
    }
    u->state=2;u->status_ep=1;u->status_type=3;return 0;
}
static int endpoint_event(usb *u,uint32_t event){
    unsigned ep=(event>>1)&31u,type=(event>>6)&15u,status=(event>>12)&15u;
    if(ep>3)return 0;
    if(ep>=2){
        if(!u->medium||!u->bulk_ready||type!=1||!u->active[ep])return 0;
        u->active[ep]=0;fence();trb *t=&u->memory->transfers[ep];
        unsigned requested=ep==2?512u:u->bulk_in_bytes,remaining=t->size&0xffffffu;
        if((t->control&1u)||remaining>requested||(status&1u))return -38;
        int e=ep==2?xpl_usb_msd_out(u->medium,u->memory->bulk_out,requested-remaining):xpl_usb_msd_in_complete(u->medium,requested-remaining);
        if(e&&u->medium->phase!=XPL_MSD_RESET_REQUIRED)return -39;
        u->d->media_commands=u->medium->commands;u->d->media_reads=u->medium->reads;u->d->media_writes=u->medium->writes;
        u->d->media_phase=u->medium->phase;u->d->media_ejected=u->medium->ejected;
        if(u->medium->phase==XPL_MSD_RESET_REQUIRED){
            u->control.halted=3;if(command(u,2,4,0,0,0)||command(u,3,4,0,0,0))return -40;return 0;
        }
        return bulk_kick(u);
    }
    if(type==1){
        u->active[ep]=0;fence();
        u->d->last_trb_size=u->memory->transfers[ep].size;u->d->last_trb_control=u->memory->transfers[ep].control;
        if(u->memory->transfers[ep].control&1u)return -21;
        if(u->state==0&&ep==0)return setup_done(u);
        if(u->state==1&&ep==1){u->state=2;return 0;}
        if(u->state==3&&ep==u->status_ep){
            u->d->address=u->control.address;u->d->configured=u->control.configured;return setup_arm(u);
        }
    }else if(type==3&&status==2u&&u->state!=0&&ep==u->status_ep&&u->state!=3){
        u->state=3;return transfer(u,ep,u->status_type,(uint32_t)offsetof(usb_memory,control),0);
    }return 0;
}
static int device_event(usb *u,uint32_t event){
    unsigned type=(event>>8)&15u;
    if(type==1){
        u->d->resets++;u->control.address=u->control.configured=0;
        if(u->medium){u->bulk_ready=0;u->control.halted=0;(void)bulk_reset(u);if(wr(u,CORE+0xc720u,3))return -2;}
        if(rmw(u,CORE+DCFG,0x7fu<<3,0))return -2;
        if(u->state!=0){
            for(unsigned ep=0;ep<2;ep++)if(u->active[ep]){
                (void)command(u,ep,8u|(1u<<11)|(u->resource[ep]<<16),0,0,0);u->active[ep]=0;
            }
            return setup_arm(u);
        }
    }else if(type==2){uint32_t value;u->d->connects++;if(rd(u,CORE+DSTS,&value))return -2;u->d->speed=value&7u;if(u->medium&&u->d->speed!=0)return -41;}
    return 0;
}
static const uint64_t auxiliary[8]={0xfc401a84u,0xfc4004acu,0xfc4004a8u,0xfd4ab000u,
    0xf92f8808u,0xf92f8810u,0xf92f8814u,0xf92f88ecu};
static int start(usb *u){
    int e=xpl_usb_power_acquire(u->io,&u->d->power);if(e)return e;u->powered=1;u->d->phase=2;
    for(unsigned n=0;n<4;n++){if(rd(u,auxiliary[n],&u->aux[n]))return -2;u->aux_count++;}
    if(wr(u,auxiliary[0],u->aux[0]|1u)||wait_mask(u,auxiliary[0],1u<<31,0,20000)||
       wr(u,auxiliary[1],u->aux[1]|1u)||wait_mask(u,auxiliary[1],1u<<31,0,20000))return -8;
    for(unsigned n=4;n<8;n++){if(rd(u,auxiliary[n],&u->aux[n]))return -2;u->aux_count++;}
    if(rd(u,0xf92f8800u,&u->d->phy_version)||u->d->phy_version<0x10020061u)return -9;
    u->d->phase=3;u->core_touched=1;
    if(rmw(u,CORE+GCTL,0,1u<<11)||rmw(u,CORE+0xc200u,0,1u<<31)||rmw(u,CORE+0xc2c0u,0,1u<<31))return -2;
    if(wr(u,auxiliary[3],u->aux[3]|1u)||wr(u,auxiliary[2],u->aux[2]|1u)||delay(u,1100)||
       wr(u,auxiliary[2],u->aux[2]&~1u))return -10;
    if(wr(u,auxiliary[5],u->aux[5]|(1u<<21)|(1u<<26)|2u)||delay(u,2100)||
       wr(u,auxiliary[7],((u->aux[7]|0xdc88u)&~0x70u))||
       wr(u,auxiliary[6],(u->aux[6]&~0x03ffffffu)|0x00fbdfa7u))return -11;
    if(rmw(u,auxiliary[5],0,(1u<<13)|(1u<<20)|(1u<<28)))return -2;
    /* HS-only UTMI clock mux; no SS-PHY programming or regulator writes. */
    if(rmw(u,auxiliary[4],2u,1u<<8)||delay(u,2)||rmw(u,auxiliary[4],0,9u)||delay(u,2)||
       rmw(u,auxiliary[4],1u<<8,0)||rmw(u,CORE+0xc200u,(1u<<31)|(1u<<6),0)||
       rmw(u,CORE+0xc2c0u,1u<<31,1u<<17)||delay(u,100)||rmw(u,CORE+GCTL,1u<<11,0)||delay(u,100))return -12;
    u->d->phase=4;
    if(rmw(u,CORE+DCTL,RUN,1u<<30)||wait_mask(u,CORE+DCTL,1u<<30,0,200000))return -13;
    if(rmw(u,CORE+GCTL,(3u<<12)|(0x1fffu<<19)|(1u<<10)|1u,(2u<<12)|(2u<<19)|(1u<<16))||
       rmw(u,CORE+0xc200u,(1u<<3)|(1u<<6)|(15u<<10),9u<<10)||
       rmw(u,CORE+DCFG,(7u<<0)|(127u<<3),0))return -14;
    uint32_t count;if(rd(u,CORE+0xc40cu,&count))return -2;
    if(wr(u,CORE+0xc400u,(uint32_t)u->pa)||wr(u,CORE+0xc404u,0)||
       wr(u,CORE+0xc408u,0x80001000u)||wr(u,CORE+0xc40cu,count&0xffffu)||wr(u,CORE+0xc708u,7))return -15;
    u->d->phase=5;
    if(command(u,0,9,0,0,0))return -16;
    for(unsigned ep=0;ep<2;ep++)if(command(u,ep,1,64u<<3,(ep<<25)|(1u<<8)|(1u<<10),0)||command(u,ep,2,1,0,0))return -17;
    if(wr(u,CORE+0xc720u,3)||setup_arm(u)||rmw(u,CORE+DCTL,0,RUN))return -18;
    u->d->phase=6;return 0;
}
static int usb_probe(const xpl_sd_board_io *io,void *dma,uint64_t pa,size_t capacity,
    const char *serial,xpl_usb_msd *medium,xpl_usb_enum_diag *d){
    if(!d)return -1;*d=(xpl_usb_enum_diag){.result=-1};
    if(!io||!io->read||!io->write||!io->now_us||!dma||((uintptr_t)dma&4095u)||pa!=0x1a000000u||capacity<8192||!serial)return -1;
    usb u={.io=io,.d=d,.memory=dma,.pa=pa,.medium=medium};u.control.mass_storage=medium?1:0;u.control.writable=medium&&medium->write_sector?1:0;
    unsigned n=0;while(n<sizeof u.control.serial&&serial[n]){u.control.serial[n]=serial[n];n++;}
    if(n==sizeof u.control.serial)return -1;
    for(size_t at=0;at<sizeof(usb_memory);at++)((volatile uint8_t *)dma)[at]=0;fence();
    uint64_t begin=io->now_us(io->ctx);if(begin==UINT64_MAX)return -3;
    int result=start(&u);if(result)goto cleanup;
    for(unsigned loops=0;loops<100000000u;loops++){
        uint32_t count;if(rd(&u,CORE+0xc40cu,&count)){result=-2;break;}
        count&=0xffffu;if(count>4096u||(count&3u)){result=-19;break;}
        while(count){
            fence();uint32_t event=u.memory->events[u.ring>>2];u.ring=(u.ring+4u)&4095u;
            if(wr(&u,CORE+0xc40cu,4)){result=-2;goto cleanup;}
            d->last_event=event;d->events++;count-=4;
            result=event&1u?device_event(&u,event):endpoint_event(&u,event);
            if(result)goto cleanup;
        }
        uint64_t now=io->now_us(io->ctx);if(now==UINT64_MAX||now<begin){result=-3;break;}
        d->elapsed_us=now-begin;if(now-begin>=(medium?180000000u:20000000u))break;
        if(medium&&medium->ejected&&medium->phase==XPL_MSD_CBW)break;
        if(io->relax)io->relax(io->ctx);
    }
cleanup:
    d->result=result;
    if(u.core_touched){
        (void)rd(&u,CORE+DCTL,&d->dctl);(void)rd(&u,CORE+DSTS,&d->dsts);
        (void)rd(&u,auxiliary[5],&d->phy_ctrl);(void)rd(&u,auxiliary[7],&d->phy_common);
        if(rmw(&u,CORE+DCTL,RUN,0)||wait_mask(&u,CORE+DSTS,1u<<22,1u<<22,20000)){
            d->cleanup_errors|=1u;
            if(wr(&u,0xfc4003c0u,d->power.bcr|1u))d->cleanup_errors|=2u;
        }
    }
    for(unsigned i=u.aux_count;i>0;i--)if(wr(&u,auxiliary[i-1],u.aux[i-1]))d->cleanup_errors|=4u;
    if(u.powered&&xpl_usb_power_release(io,&d->power))d->cleanup_errors|=8u;
    if(u.core_touched&&wr(&u,0xfc4003c0u,d->power.bcr))d->cleanup_errors|=16u;
    return d->cleanup_errors?-20:d->result;
}
int xpl_usb_enum_probe(const xpl_sd_board_io *io,void *dma,uint64_t pa,size_t capacity,const char *serial,xpl_usb_enum_diag *d){
    return usb_probe(io,dma,pa,capacity,serial,0,d);
}
int xpl_usb_msd_probe(const xpl_sd_board_io *io,void *dma,uint64_t pa,size_t capacity,const char *serial,xpl_usb_msd *medium,xpl_usb_enum_diag *d){
    if(!medium)return -1;return usb_probe(io,dma,pa,capacity,serial,medium,d);
}
