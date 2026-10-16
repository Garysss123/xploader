/* SPDX-License-Identifier: MIT */
#include "xpl_sdhci.h"
#define REG_BLOCK_SIZE 0x04u
#define REG_BLOCK_COUNT 0x06u
#define REG_ARGUMENT 0x08u
#define REG_TRANSFER 0x0cu
#define REG_COMMAND 0x0eu
#define REG_RESPONSE 0x10u
#define REG_BUFFER 0x20u
#define REG_PRESENT 0x24u
#define REG_HOST1 0x28u
#define REG_POWER 0x29u
#define REG_CLOCK 0x2cu
#define REG_TIMEOUT 0x2eu
#define REG_RESET 0x2fu
#define REG_STATUS 0x30u
#define REG_ENABLE 0x34u
#define REG_SIGNAL 0x38u
#define REG_HOST2 0x3eu
#define REG_CAPS 0x40u
#define REG_CAPS1 0x44u
#define REG_VERSION 0xfeu
#define IRQ_CMD 0x1u
#define IRQ_DATA_END 0x2u
#define IRQ_READ_READY 0x20u
#define IRQ_WRITE_READY 0x10u
#define IRQ_ERRORS 0xffff8000u
#define R1_ERRORS 0xfdffe008u
#define CMD_SHORT 0x1au
#define CMD_LONG 0x09u
#define CMD_BUSY 0x1bu
#define CMD_DATA 0x20u
#define OCR_VDD_2950 0x00020000u
#define OP_INIT_US UINT64_C(2000000)
#define OP_READ_US UINT64_C(2000000)
#define WAIT_US UINT64_C(250000)

static int fault(xpl_sd_host *h,int e){h->last_error=e;h->card_ready=0;return e;}
static int allowed(uint32_t o,unsigned w,int write){
    if(w==4){
        if(write)return o==REG_ARGUMENT||o==REG_STATUS||o==REG_ENABLE||o==REG_SIGNAL||o==REG_BUFFER;
        return o==REG_PRESENT||o==REG_STATUS||o==REG_CAPS||o==REG_CAPS1||
               o==REG_BUFFER||(o>=REG_RESPONSE&&o<REG_BUFFER&&!(o&3));
    }
    if(w==2){
        if(write)return o==REG_BLOCK_SIZE||o==REG_BLOCK_COUNT||o==REG_TRANSFER||o==REG_COMMAND;
        return o==REG_CLOCK||o==REG_HOST2||o==REG_VERSION;
    }
    if(w==1){
        if(write)return o==REG_TIMEOUT||o==REG_RESET;
        return o==REG_HOST1||o==REG_POWER||o==REG_RESET;
    }
    return 0;
}
static int rd(xpl_sd_host *h,uint32_t o,unsigned w,uint32_t *v){
    if(!h||!h->bound||!v||!allowed(o,w,0))return XPL_SD_EINVAL;
    if(h->io.read(h->io.ctx,o,w,v))return fault(h,XPL_SD_EIO);
    if(w==1)*v&=0xffu;else if(w==2)*v&=0xffffu;
    return 0;
}
static int wr(xpl_sd_host *h,uint32_t o,unsigned w,uint32_t v){
    if(!h||!h->bound||!allowed(o,w,1)||(w<4&&v>=(1u<<(8*w))))return XPL_SD_EINVAL;
    if(h->io.write(h->io.ctx,o,w,v))return fault(h,XPL_SD_EIO);
    return 0;
}
static int expired(xpl_sd_host *h,uint64_t start,uint64_t us){
    uint64_t now=h->io.now_us(h->io.ctx);
    return now-start>=us||now-h->operation_start>=h->operation_us;
}
static void relax(xpl_sd_host *h){if(h->io.relax)h->io.relax(h->io.ctx);}
static int wait_reg(xpl_sd_host *h,uint32_t o,unsigned w,uint32_t mask,uint32_t expected){
    uint64_t start=h->io.now_us(h->io.ctx);uint32_t v;unsigned n;int e;
    for(n=0;n<h->poll_limit;n++){
        if((e=rd(h,o,w,&v)))return e;
        if((v&mask)==expected)return 0;
        if(expired(h,start,WAIT_US))break;
        relax(h);
    }
    return fault(h,XPL_SD_ETIMEOUT);
}
static int wait_irq(xpl_sd_host *h,uint32_t flags){
    uint64_t start=h->io.now_us(h->io.ctx);uint32_t v;unsigned n;int e;
    for(n=0;n<h->poll_limit;n++){
        if((e=rd(h,REG_STATUS,4,&v)))return e;
        h->last_interrupt=v;
        if(v&IRQ_ERRORS)return fault(h,XPL_SD_EHOST);
        if((v&flags)==flags)return 0;
        if(expired(h,start,WAIT_US))break;
        relax(h);
    }
    return fault(h,XPL_SD_ETIMEOUT);
}
static int pause_1ms(xpl_sd_host *h){
    uint64_t start=h->io.now_us(h->io.ctx);unsigned n;
    for(n=0;n<h->poll_limit;n++){
        if(h->io.now_us(h->io.ctx)-start>=1000)return 0;
        if(expired(h,h->operation_start,h->operation_us))return fault(h,XPL_SD_ETIMEOUT);
        relax(h);
    }
    return fault(h,XPL_SD_ETIMEOUT);
}
static int command(xpl_sd_host *h,unsigned cmd,uint32_t arg,uint16_t flags,uint32_t *response){
    int e;uint32_t v;
    if(expired(h,h->operation_start,h->operation_us))return fault(h,XPL_SD_ETIMEOUT);
    if((e=wait_reg(h,REG_PRESENT,4,3u,0))||
       (e=wr(h,REG_STATUS,4,0xffffffffu))||
       (e=wr(h,REG_ARGUMENT,4,arg))||
       (e=wr(h,REG_COMMAND,2,(cmd<<8)|flags))||
       (e=wait_irq(h,IRQ_CMD))||
       (e=wr(h,REG_STATUS,4,IRQ_CMD)))return e;
    if(flags&3u){
        if((e=rd(h,REG_RESPONSE,4,&v)))return e;
        h->last_response=v;if(response)*response=v;
    }
    if((flags&3u)==3u){
        if((e=wait_reg(h,REG_PRESENT,4,2u,0))||(e=rd(h,REG_STATUS,4,&v)))return e;
        h->last_interrupt=v;
        if(v&IRQ_ERRORS)return fault(h,XPL_SD_EHOST);
    }
    return 0;
}
static int r1_ok(xpl_sd_host *h,uint32_t r){return r&R1_ERRORS?fault(h,XPL_SD_ECARD):0;}
int xpl_sdhci_bind(xpl_sd_host *h,const xpl_sdhci_io *io,uint64_t base,uint32_t span,uint32_t limit){
    if(!h||!io)return XPL_SD_EINVAL;
    h->bound=0;h->card_ready=0;h->blocks512=0;h->rca=0;h->last_error=0;
    h->last_interrupt=0;h->last_response=0;
    h->data_commands=0;h->multi_commands=0;h->blocks_read=0;h->blocks_written=0;
    if(!io->read||!io->write||!io->now_us||base!=XPL_SDHC2_BASE||span!=XPL_SDHC2_SPAN||
       !limit||limit>1000000u)return XPL_SD_EINVAL;
    h->io=*io;h->poll_limit=limit;h->operation_start=0;h->operation_us=0;h->bound=1;
    return 0;
}
int xpl_sdhci_probe(xpl_sd_host *h,xpl_sdhci_snapshot *s){
    uint32_t v;int e;
    if(!h||!h->bound||!s)return XPL_SD_EINVAL;
#define SNAP(o,w,member) do{if((e=rd(h,(o),(w),&v)))return e;s->member=v;}while(0)
    SNAP(REG_VERSION,2,version);SNAP(REG_PRESENT,4,present_state);
    SNAP(REG_CAPS,4,capabilities);SNAP(REG_CAPS1,4,capabilities1);
    SNAP(REG_CLOCK,2,clock_control);SNAP(REG_HOST2,2,host_control2);
    SNAP(REG_HOST1,1,host_control);SNAP(REG_POWER,1,power_control);SNAP(REG_RESET,1,software_reset);
#undef SNAP
    if(s->version==0xffffu||s->present_state==0xffffffffu||s->capabilities==0xffffffffu)
        return fault(h,XPL_SD_EHOST);
    return 0;
}
int xpl_sd_initialize(xpl_sd_host *h,const xpl_sd_prepared *p){
    xpl_sdhci_snapshot s;uint32_t r=0,csd[4],raw[4],size;unsigned n;int e;
    if(!h||!h->bound||!p)return XPL_SD_EINVAL;
    h->card_ready=0;h->blocks512=0;h->rca=0;
    for(n=0;n<4;n++)h->cid[n]=0;
    h->max_clock_hz=0;
    if((p->verified&XPL_SD_PREP_ALL)!=XPL_SD_PREP_ALL||p->card_clock_hz<100000u||p->card_clock_hz>400000u)
        return fault(h,XPL_SD_EPREP);
    if((e=xpl_sdhci_probe(h,&s)))return e;
    if((s.version&0xffu)<1u||(s.version&0xffu)>4u)return fault(h,XPL_SD_EUNSUPPORTED);
    if((s.clock_control&7u)!=7u||(s.host_control&0x26u)||(s.host_control2&0x80cfu)||
       !(s.power_control&1u)||((s.power_control&0xeu)!=0xau&&(s.power_control&0xeu)!=0xeu)||
       s.software_reset||(s.present_state&3u))return fault(h,XPL_SD_EPREP);
    h->operation_start=h->io.now_us(h->io.ctx);h->operation_us=OP_INIT_US;
    /* CMD/DAT reset only. Never full controller reset or power/clock writes. */
    if((e=wr(h,REG_RESET,1,6u))||(e=wait_reg(h,REG_RESET,1,6u,0))||
       (e=wr(h,REG_SIGNAL,4,0))||(e=wr(h,REG_ENABLE,4,IRQ_ERRORS|IRQ_CMD|IRQ_DATA_END|IRQ_READ_READY))||
       (e=wr(h,REG_TIMEOUT,1,0x0eu))||(e=pause_1ms(h))|| /* >=74 clocks at >=100kHz */
       (e=command(h,0,0,0,0))||
       (e=command(h,8,0x1aau,CMD_SHORT,&r)))return e;
    if((r&0xfffu)!=0x1aau)return fault(h,XPL_SD_EUNSUPPORTED);
    for(n=0;n<1000;n++){
        if((e=command(h,55,0,CMD_SHORT,&r))||(e=r1_ok(h,r)))return e;
        if(!(r&0x20u))return fault(h,XPL_SD_EUNSUPPORTED);
        if((e=command(h,41,0x40000000u|OCR_VDD_2950,2u,&r)))return e;
        if(r&0x80000000u)break;
        if((e=pause_1ms(h)))return e;
    }
    if(n==1000)return fault(h,XPL_SD_ETIMEOUT);
    if((r&0xc0000000u)!=0xc0000000u||!(r&OCR_VDD_2950))return fault(h,XPL_SD_EUNSUPPORTED);
    if((e=command(h,2,0,CMD_LONG,0)))return e;
    for(n=0;n<4;n++)if((e=rd(h,REG_RESPONSE+4*n,4,&raw[n])))return e;
    for(n=0;n<4;n++)h->cid[n]=(raw[3-n]<<8)|(n<3?raw[2-n]>>24:0);
    if((e=command(h,3,0,CMD_SHORT,&r)))return e;
    if(!(r>>16)||(r&0xe000u))return fault(h,XPL_SD_ECARD);
    h->rca=(uint16_t)(r>>16);
    if((e=command(h,9,(uint32_t)h->rca<<16,CMD_LONG,0)))return e;
    for(n=0;n<4;n++)if((e=rd(h,REG_RESPONSE+4*n,4,&raw[n])))return e;
    for(n=0;n<4;n++)csd[n]=(raw[3-n]<<8)|(n<3?raw[2-n]>>24:0);
    if((csd[0]>>30)!=1u)return fault(h,XPL_SD_EUNSUPPORTED);
    { /* CSD TRAN_SPEED[103:96]; units times the specification's value/10. */
        static const uint32_t unit[4]={100000,1000000,10000000,100000000};
        static const unsigned value[16]={0,10,12,13,15,20,25,30,35,40,45,50,55,60,70,80};
        unsigned rate=csd[0]&255u;
        if(!(rate&128u)&&(rate&7u)<4u)h->max_clock_hz=(uint32_t)((uint64_t)unit[rate&7u]*value[(rate>>3)&15u]/10u);
    }
    size=((csd[1]&0x3fu)<<16)|(csd[2]>>16);
    h->blocks512=((uint64_t)size+1u)*1024u;
    if((e=command(h,7,(uint32_t)h->rca<<16,CMD_BUSY,&r))||(e=r1_ok(h,r))||
       (e=command(h,13,(uint32_t)h->rca<<16,CMD_SHORT,&r))||(e=r1_ok(h,r)))return e;
    if((r&0x1f00u)!=0x900u)return fault(h,XPL_SD_ECARD); /* TRAN + READY */
    h->last_error=0;h->card_ready=1;return 0;
}
int xpl_sd_read512(void *ctx,uint64_t lba,uint32_t count,void *out){
    xpl_sd_host *h=ctx;uint8_t *dst=out;uint32_t r,v;unsigned block,word;int e;
    if(!h||!h->bound||!h->card_ready||!out||!count||count>XPL_SD_MAX_READ_BLOCKS)return XPL_SD_EINVAL;
    if(lba>=h->blocks512||count>h->blocks512-lba||lba+count>UINT64_C(0x100000000))return XPL_SD_EBOUNDS;
    h->operation_start=h->io.now_us(h->io.ctx);h->operation_us=OP_READ_US;
    for(block=0;block<count;block++){
        h->data_commands++;
        if((e=wait_reg(h,REG_PRESENT,4,3u,0))||
           (e=wr(h,REG_BLOCK_SIZE,2,512))||(e=wr(h,REG_BLOCK_COUNT,2,1))||
           (e=wr(h,REG_TRANSFER,2,0x12u))|| /* READ + block-count-enable, DMA=0 */
           (e=command(h,17,(uint32_t)(lba+block),CMD_SHORT|CMD_DATA,&r))||
           (e=r1_ok(h,r))||(e=wait_irq(h,IRQ_READ_READY))||
           (e=rd(h,REG_PRESENT,4,&v)))return e;
        if(!(v&0x800u))return fault(h,XPL_SD_EHOST);
        for(word=0;word<128;word++){
            if((e=rd(h,REG_BUFFER,4,&v)))return e;
            *dst++=(uint8_t)v;*dst++=(uint8_t)(v>>8);*dst++=(uint8_t)(v>>16);*dst++=(uint8_t)(v>>24);
        }
        if((e=wr(h,REG_STATUS,4,IRQ_READ_READY))||(e=wait_irq(h,IRQ_DATA_END))||
           (e=wr(h,REG_STATUS,4,IRQ_DATA_END)))return e;
        h->blocks_read++;
    }
    h->last_error=0;return 0;
}
int xpl_sd_read512_multiblock(void *ctx,uint64_t lba,uint32_t count,void *out){
    xpl_sd_host *h=ctx;uint8_t *dst=out;uint32_t r,v;unsigned block,word;int e;
    if(!h||!h->bound||!h->card_ready||!out||!count||count>XPL_SD_MAX_READ_BLOCKS)return XPL_SD_EINVAL;
    if(lba>=h->blocks512||count>h->blocks512-lba||lba+count>UINT64_C(0x100000000))return XPL_SD_EBOUNDS;
    if(count==1)return xpl_sd_read512(ctx,lba,count,out);
    h->operation_start=h->io.now_us(h->io.ctx);h->operation_us=OP_READ_US;
    if((e=wait_reg(h,REG_PRESENT,4,3u,0))||
       (e=wr(h,REG_BLOCK_SIZE,2,512))||(e=wr(h,REG_BLOCK_COUNT,2,count))||
       (e=wr(h,REG_TRANSFER,2,0x36u)))return e; /* count, read, multi, AutoCMD12; DMA=0 */
    h->data_commands++;h->multi_commands++;
    if((e=command(h,18,(uint32_t)lba,CMD_SHORT|CMD_DATA,&r))||(e=r1_ok(h,r)))return e;
    for(block=0;block<count;block++){
        if((e=wait_irq(h,IRQ_READ_READY))||(e=rd(h,REG_PRESENT,4,&v)))return e;
        if(!(v&0x800u))return fault(h,XPL_SD_EHOST);
        for(word=0;word<128;word++){
            if((e=rd(h,REG_BUFFER,4,&v)))return e;
            *dst++=(uint8_t)v;*dst++=(uint8_t)(v>>8);*dst++=(uint8_t)(v>>16);*dst++=(uint8_t)(v>>24);
        }
        if((e=wr(h,REG_STATUS,4,IRQ_READ_READY)))return e;
        h->blocks_read++;
    }
    if((e=wait_irq(h,IRQ_DATA_END))||(e=wr(h,REG_STATUS,4,IRQ_DATA_END))||
       (e=wait_reg(h,REG_PRESENT,4,3u,0)))return e;
    h->last_error=0;return 0;
}
int xpl_sd_write512(void *ctx,uint64_t lba,uint32_t count,const void *data){
    xpl_sd_host *h=ctx;const uint8_t *src=data;uint32_t response,state;int e;
    if(!h||!h->bound||!h->card_ready||!data||!count||count>XPL_SD_MAX_READ_BLOCKS)return XPL_SD_EINVAL;
    if(lba>=h->blocks512||count>h->blocks512-lba||lba+count>UINT64_C(0x100000000))return XPL_SD_EBOUNDS;
    h->operation_start=h->io.now_us(h->io.ctx);h->operation_us=OP_READ_US;
    if((e=wr(h,REG_ENABLE,4,IRQ_ERRORS|IRQ_CMD|IRQ_DATA_END|IRQ_READ_READY|IRQ_WRITE_READY)))return e;
    for(unsigned block=0;block<count;block++){
        h->data_commands++;
        if((e=wait_reg(h,REG_PRESENT,4,3u,0))||
           (e=wr(h,REG_BLOCK_SIZE,2,512))||(e=wr(h,REG_BLOCK_COUNT,2,1))||
           (e=wr(h,REG_TRANSFER,2,2u))|| /* block-count-enable, PIO write */
           (e=command(h,24,(uint32_t)(lba+block),CMD_SHORT|CMD_DATA,&response))||
           (e=r1_ok(h,response))||(e=wait_irq(h,IRQ_WRITE_READY))||
           (e=rd(h,REG_PRESENT,4,&state)))return e;
        if(!(state&0x400u))return fault(h,XPL_SD_EHOST);
        for(unsigned word=0;word<128;word++,src+=4){
            uint32_t value=(uint32_t)src[0]|(uint32_t)src[1]<<8|(uint32_t)src[2]<<16|(uint32_t)src[3]<<24;
            if((e=wr(h,REG_BUFFER,4,value)))return e;
        }
        if((e=wr(h,REG_STATUS,4,IRQ_WRITE_READY))||(e=wait_irq(h,IRQ_DATA_END))||
           (e=wr(h,REG_STATUS,4,IRQ_DATA_END))||(e=wait_reg(h,REG_PRESENT,4,3u,0))||
           (e=command(h,13,(uint32_t)h->rca<<16,CMD_SHORT,&response))||(e=r1_ok(h,response)))return e;
        if((response&0x1f00u)!=0x900u)return fault(h,XPL_SD_ECARD);
        h->blocks_written++;
    }
    h->last_error=0;return 0;
}
