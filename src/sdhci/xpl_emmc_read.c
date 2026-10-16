/* SPDX-License-Identifier: MIT */
#include "xpl_emmc_read.h"
#define HC UINT64_C(0xf9824900)
#define R1_ERRORS 0xfdffe088u
static int rd(xpl_emmc_reader *h,uint32_t o,unsigned w,uint32_t *v) {
    if(h->io.read(h->io.ctx,HC+o,w,v))return XPL_SD_EIO;
    if(w==1)*v&=255u;else if(w==2)*v&=65535u;return 0;
}
static int wr(xpl_emmc_reader *h,uint32_t o,unsigned w,uint32_t v) {
    return h->io.write(h->io.ctx,HC+o,w,v)?XPL_SD_EIO:0;
}
static int wait(xpl_emmc_reader *h,uint32_t o,unsigned w,uint32_t mask,uint32_t wanted) {
    uint64_t start=h->io.now_us(h->io.ctx);
    for(uint32_t i=0;i<h->limit;i++) {
        uint32_t v;int e=rd(h,o,w,&v);if(e)return e;
        if(o==0x30u) {
            h->diag->last_irq=v;
            if(v&0xffff8000u)return XPL_SD_EHOST;
        }
        if((v&mask)==wanted)return 0;
        uint64_t now=h->io.now_us(h->io.ctx);
        if(now<start||now-start>=250000u)return XPL_SD_ETIMEOUT;
        if(h->io.relax)h->io.relax(h->io.ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int command(xpl_emmc_reader *h,unsigned cmd,uint32_t arg,unsigned flags) {
    int e;uint32_t r;
    if(!(cmd==13u||cmd==8u||cmd==17u||(h->fresh_started&&
         (cmd==0u||cmd==1u||cmd==2u||cmd==3u||cmd==7u||cmd==9u))))return XPL_SD_EINVAL;
    if((e=wait(h,0x24u,4,3u,0))||(e=wr(h,0x30u,4,UINT32_MAX))||
       (e=wr(h,8u,4,arg)))return e;
    h->diag->last_command=cmd;h->diag->commands++;
    if((e=wr(h,0x0eu,2,(cmd<<8)|flags))||(e=wait(h,0x30u,4,1u,1u))||
       (e=rd(h,0x10u,4,&r))||(e=wr(h,0x30u,4,1u)))return e;
    h->diag->last_response=r;
    if((cmd==3u||cmd==7u||cmd==8u||cmd==13u||cmd==17u)&&(r&R1_ERRORS))return XPL_SD_ECARD;
    if(flags==0x1bu) {
        if((e=wait(h,0x30u,4,2u,2u))||(e=wr(h,0x30u,4,2u))||
           (e=wait(h,0x24u,4,3u,0)))return e;
    }
    return 0;
}
static int data(xpl_emmc_reader *h,unsigned cmd,uint32_t arg,uint8_t *out) {
    int e;uint32_t v;
    if((e=wait(h,0x24u,4,3u,0))||(e=wr(h,4u,2,512))||
       (e=wr(h,6u,2,1))||(e=wr(h,0x0cu,2,0x12u))||
       (e=command(h,cmd,arg,0x3au))||(e=wait(h,0x30u,4,0x20u,0x20u))||
       (e=rd(h,0x24u,4,&v)))return e;
    if(!(v&0x800u))return XPL_SD_EHOST;
    for(unsigned i=0;i<128;i++) {
        if((e=rd(h,0x20u,4,&v)))return e;
        for(unsigned j=0;j<4;j++)out[i*4+j]=(uint8_t)(v>>(8*j));
    }
    if((e=wr(h,0x30u,4,0x20u))||(e=wait(h,0x30u,4,2u,2u))||
       (e=wr(h,0x30u,4,2u)))return e;
    return 0;
}
static int begin(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                  const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d,unsigned borrow) {
    int e;uint32_t v;uint8_t ext[512];
    if(!h||!io||!io->read||!io->write||!io->now_us||!in||!d||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *h=(xpl_emmc_reader){.io=*io,.diag=d,.limit=limit};*d=(xpl_emmc_read_diag){.phase=1};
    if(!in->restored||in->restore_result||(in->ahb_after&0x80000001u)!=1u||
       (in->apps_after&0x80000001u)!=1u||in->clock!=7u||in->host!=0x30u||
       in->host2!=3u||!(in->power&1u)||(in->present&3u))return XPL_SD_EPREP;
    if(borrow==2u) {
        if(((in->rcgr_cfg>>8)&7u)!=5u)return XPL_SD_EPREP;
        if(io->read(io->ctx,0xfc4004c0u,4,&d->bcr)||
           io->read(io->ctx,0xfc401480u,4,&d->pll_vote_before)||
           io->read(io->ctx,0xfc401dc0u,4,&d->pll_mode_before))return XPL_SD_EIO;
        if((d->bcr&1u)||d->pll_vote_before==UINT32_MAX||d->pll_mode_before==UINT32_MAX)return XPL_SD_EPREP;
        d->pll_vote_active=d->pll_vote_before;d->pll_mode_active=d->pll_mode_before;
        if(!(d->pll_mode_before&0x40000000u)) {
            if(!(d->pll_mode_before&0x100000u))return XPL_SD_EPREP;
            h->pll_started=1;
            if(io->write(io->ctx,0xfc401480u,4,d->pll_vote_before|16u))return XPL_SD_EIO;
            uint64_t start=io->now_us(io->ctx);unsigned live=0;
            for(uint32_t i=0;i<limit;i++) {
                if(io->read(io->ctx,0xfc401dc0u,4,&d->pll_mode_active))return XPL_SD_EIO;
                if(d->pll_mode_active&0x40000000u){live=1;break;}
                uint64_t now=io->now_us(io->ctx);
                if(now<start||now-start>=100000u)return XPL_SD_ETIMEOUT;
                if(io->relax)io->relax(io->ctx);
            }
            if(!live)return XPL_SD_ETIMEOUT;
            if(io->read(io->ctx,0xfc401480u,4,&d->pll_vote_active))return XPL_SD_EIO;
        }
    }
    if(borrow) {
        if(io->read(io->ctx,0xf9824078u,4,&d->core_before))return XPL_SD_EIO;
        if(d->core_before!=in->core_mode||d->core_before!=0u)return XPL_SD_EPREP;
        h->core_started=1;
        if(io->write(io->ctx,0xf9824078u,4,d->core_before|1u)||
           io->read(io->ctx,0xf9824078u,4,&d->core_active)||d->core_active!=(d->core_before|1u))return XPL_SD_EIO;
    }
    if((e=rd(h,0x2fu,1,&v)))return e;
    if(v)return XPL_SD_EPREP;
#define SAVE(o,w,n) do {if((e=rd(h,o,w,&d->saved_##n)))return e;} while(0)
    SAVE(0x34u,4,enable);SAVE(0x38u,4,signal);SAVE(8u,4,arg);
    SAVE(0x2eu,1,timeout);SAVE(4u,2,size);SAVE(6u,2,count);SAVE(0x0cu,2,transfer);
    SAVE(0x2cu,2,clock);
#undef SAVE
    h->started=1;d->phase=2;
    if(borrow==3u) {
        if((e=rd(h,0x10cu,4,&d->vendor))||(e=rd(h,0x100u,4,&d->dll_config))||
           (e=rd(h,0x108u,4,&d->dll_status))||
           io->read(io->ctx,0xf9824000u,4,&d->core_power)||
           io->read(io->ctx,0xf9824050u,4,&d->core_version))return XPL_SD_EIO;
        d->phase=5;
        if((e=wr(h,0x2cu,2,1u))||(e=wait(h,0x2cu,2,2u,2u))||
           (e=wr(h,0x2cu,2,7u))||(e=rd(h,0x2cu,2,&d->clock_active))||
           (e=wr(h,0x2fu,1,6u))||(e=wait(h,0x2fu,1,6u,0))||
           (e=rd(h,0x2fu,1,&d->reset_after)))return e;
        d->phase=2;
    }
    if((e=wr(h,0x38u,4,0))||(e=wr(h,0x34u,4,0xffff8023u))||
       (e=wr(h,0x2eu,1,14))||(e=command(h,13u,0x10000u,0x1au)))return e;
    if((d->last_response&0x1f00u)!=0x900u)return XPL_SD_ECARD;
    d->phase=3;
    if((e=data(h,8u,0,ext)))return e;
    d->partition_config=ext[179];d->bus_width=ext[183];d->hs_timing=ext[185];
    d->ext_revision=ext[192];d->data_sector_size=ext[61];
    d->capacity_sectors=(uint32_t)ext[212]|((uint32_t)ext[213]<<8)|((uint32_t)ext[214]<<16)|((uint32_t)ext[215]<<24);
    if((d->partition_config&7u)||d->bus_width!=2u||(d->hs_timing&15u)!=2u||
       d->data_sector_size||d->ext_revision<5u||d->capacity_sectors<=8388608u)return XPL_SD_EUNSUPPORTED;
    h->ready=1;d->phase=4;return 0;
}
int xpl_emmc_reader_begin(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                          const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d) {
    return begin(h,io,in,limit,d,0);
}
int xpl_emmc_reader_begin_hc(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                             const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d) {
    return begin(h,io,in,limit,d,1);
}
int xpl_emmc_reader_begin_hc_pll(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                                 const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d) {
    return begin(h,io,in,limit,d,2);
}
int xpl_emmc_reader_begin_primed(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                                const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d) {
    return begin(h,io,in,limit,d,3);
}
static int full_read(xpl_emmc_reader *h,uint64_t a,uint32_t *v) {
    return h->io.read(h->io.ctx,a,4,v)?XPL_SD_EIO:0;
}
static int full_write(xpl_emmc_reader *h,uint64_t a,uint32_t v) {
    return h->io.write(h->io.ctx,a,4,v)?XPL_SD_EIO:0;
}
static int full_wait(xpl_emmc_reader *h,uint64_t a,uint32_t mask,uint32_t value) {
    uint64_t start=h->io.now_us(h->io.ctx);
    for(uint32_t i=0;i<h->limit;i++) {
        uint32_t v;int e=full_read(h,a,&v);if(e)return e;
        if((v&mask)==value)return 0;
        uint64_t now=h->io.now_us(h->io.ctx);
        if(now<start||now-start>=250000u)return XPL_SD_ETIMEOUT;
        if(h->io.relax)h->io.relax(h->io.ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int settle(xpl_emmc_reader *h) {
    uint64_t start=h->io.now_us(h->io.ctx);
    for(uint32_t i=0;i<h->limit;i++) {
        uint64_t now=h->io.now_us(h->io.ctx);
        if(now<start)return XPL_SD_ETIMEOUT;
        if(now-start>=1000u)return 0;
        if(h->io.relax)h->io.relax(h->io.ctx);
    }
    return XPL_SD_ETIMEOUT;
}
static int clock_commit(xpl_emmc_reader *h,uint32_t cfg,uint32_t m,uint32_t n,uint32_t d) {
    uint32_t v;int e;
    if((e=full_write(h,0xfc4004d8u,m))||(e=full_write(h,0xfc4004dcu,n))||
       (e=full_write(h,0xfc4004e0u,d))||(e=full_write(h,0xfc4004d4u,cfg))||
       (e=full_read(h,0xfc4004d0u,&v))||(e=full_write(h,0xfc4004d0u,v|1u))||
       (e=full_wait(h,0xfc4004d0u,1u,0)))return e;
    return 0;
}
static int acknowledge_power(xpl_emmc_reader *h) {
    uint32_t status,ctl;int e;
    if((e=full_read(h,0xf98240dcu,&status))||(e=full_read(h,0xf98240e8u,&ctl)))return e;
    h->diag->init_power_irq|=status;
    if(status&~15u)return XPL_SD_EHOST;
    if(status&3u)ctl|=1u;
    if(status&12u)ctl|=4u;
    if((e=full_write(h,0xf98240e4u,status))||(e=full_write(h,0xf98240e8u,ctl)))return e;
    return 0;
}
static int long_response(xpl_emmc_reader *h,uint32_t dst[4]) {
    uint32_t r[4];int e;
    for(unsigned i=0;i<4;i++)if((e=rd(h,0x10u+4*i,4,&r[i])))return e;
    for(unsigned i=0;i<4;i++)dst[i]=(r[3-i]<<8)|(i<3?r[2-i]>>24:0);
    if(!(dst[0]|dst[1]|dst[2]|dst[3]))return XPL_SD_ECARD;
    return 0;
}
int xpl_emmc_reader_begin_fresh(xpl_emmc_reader *h,const xpl_sd_board_io *io,
                               const xpl_emmc_snapshot *in,uint32_t limit,xpl_emmc_read_diag *d) {
    uint8_t ext[512];int e;
    if(!h||!io||!io->read||!io->write||!io->now_us||!in||!d||!limit||limit>1000000u)return XPL_SD_EINVAL;
    *h=(xpl_emmc_reader){.io=*io,.diag=d,.limit=limit};*d=(xpl_emmc_read_diag){.phase=10};
    if(!in->restored||in->restore_result||(in->ahb_after&0x80000001u)!=1u||
       (in->apps_after&0x80000001u)!=1u||in->clock!=7u||in->power!=0xbu||
       in->core_mode||(in->present&3u)||(in->rcgr_cmd&1u))return XPL_SD_EPREP;
#define SAVE(o,w,n) do {if((e=rd(h,o,w,&d->saved_##n)))return e;} while(0)
    SAVE(0x34u,4,enable);SAVE(0x38u,4,signal);SAVE(8u,4,arg);
    SAVE(0x2eu,1,timeout);SAVE(4u,2,size);SAVE(6u,2,count);SAVE(0x0cu,2,transfer);
    SAVE(0x2cu,2,clock);SAVE(0x28u,1,host);SAVE(0x29u,1,power);SAVE(0x3eu,2,host2);
    SAVE(0x10cu,4,vendor);SAVE(0x100u,4,dll);
#undef SAVE
    if(d->saved_power!=in->power||d->saved_clock!=in->clock)return XPL_SD_EPREP;
    d->saved_cfg=in->rcgr_cfg;d->saved_m=in->m;d->saved_n=in->n;d->saved_d=in->d;
    if((e=full_read(h,0xf9824078u,&d->core_before))||
       (e=full_read(h,0xfc4004c0u,&d->bcr))||(e=full_read(h,0xf9824000u,&d->core_power)))return e;
    if(d->core_before||d->bcr&1u||d->core_power&0x80u)return XPL_SD_EPREP;
    h->started=1;h->fresh_started=1;h->core_started=1;d->phase=11;
    /* LK's F(400000,cxo,12,1,4), validated on the same GCC's SDCC2.
     * Card clock stays stopped during RCGR commit and core reset. */
    if((e=wr(h,0x2cu,2,0))||
       (e=clock_commit(h,(d->saved_cfg&~0x371fu)|0x2017u,1u,~3u,~4u))||
       (e=full_read(h,0xfc4004d4u,&d->init_cfg)))return e;
    if((d->init_cfg&0x371fu)!=0x2017u)return XPL_SD_EHOST;
    d->phase=12;
    if((e=full_write(h,0xf9824078u,0))||
       (e=full_write(h,0xf9824000u,d->core_power|0x80u))||
       (e=full_wait(h,0xf9824000u,0x80u,0))||
       (e=full_read(h,0xf9824000u,&d->init_core_reset))||
       (e=full_write(h,0xf9824078u,0x2001u))||
       (e=full_read(h,0xf9824078u,&d->core_active)))return e;
    /* This actual controller reads FF_CLK_SW_RST_DIS back as zero. */
    if(!(d->core_active&1u))return XPL_SD_EHOST;
    if((e=wr(h,0x2fu,1,1u))||(e=wait(h,0x2fu,1,1u,0))||
       (e=rd(h,0x2fu,1,&d->reset_after))||(e=acknowledge_power(h)))return e;
    d->phase=13;
    d->vendor=(d->saved_vendor&~0x3c0302u)|0x200u;
    d->dll_config=(d->saved_dll&~0xf0000u)|0x60000000u;
    if((e=wr(h,0x10cu,4,d->vendor))||(e=wr(h,0x100u,4,d->dll_config))||
       (e=wr(h,0x28u,1,0))||(e=wr(h,0x3eu,2,d->saved_host2&8u))||
       (e=wr(h,0x29u,1,d->saved_power))||(e=acknowledge_power(h))||
       (e=wr(h,0x38u,4,0))||(e=wr(h,0x34u,4,0xffff8023u))||
       (e=wr(h,0x2eu,1,14))||(e=wr(h,0x2cu,2,1u))||
       (e=wait(h,0x2cu,2,2u,2u))||(e=wr(h,0x2cu,2,7u))||
       (e=rd(h,0x2cu,2,&d->clock_active))||(e=settle(h)))return e;
    d->phase=14;
    if((e=command(h,0u,0,0))||(e=settle(h)))return e;
    d->card_reinitialized=1;d->phase=15;
    uint64_t start=io->now_us(io->ctx);
    for(unsigned i=0;i<1000u;i++) {
        d->op_cond_tries++;
        if((e=command(h,1u,0x40ff8000u,2u)))return e;
        d->ocr=d->last_response;
        if(d->ocr&0x80000000u)break;
        uint64_t now=io->now_us(io->ctx);
        if(now<start||now-start>=2000000u)return XPL_SD_ETIMEOUT;
        if((e=settle(h)))return e;
    }
    if(!(d->ocr&0x80000000u))return XPL_SD_ETIMEOUT;
    if((d->ocr&0xc0000000u)!=0xc0000000u||!(d->ocr&0xff8000u))return XPL_SD_EUNSUPPORTED;
    d->phase=16;
    if((e=command(h,2u,0,9u))||(e=long_response(h,d->cid))||
       (e=command(h,3u,0x10000u,0x1au))||
       (e=command(h,9u,0x10000u,9u))||(e=long_response(h,d->csd)))return e;
    {
        static const uint32_t unit[4]={100000u,1000000u,10000000u,100000000u};
        static const unsigned value[16]={0,10,12,13,15,20,26,30,35,40,45,52,55,60,70,80};
        unsigned rate=d->csd[0]&255u;
        if(!(rate&128u)&&(rate&7u)<4u)d->max_clock_hz=(uint32_t)((uint64_t)unit[rate&7u]*value[(rate>>3)&15u]/10u);
    }
    d->phase=17;
    if((e=command(h,7u,0x10000u,0x1bu))||
       (e=command(h,13u,0x10000u,0x1au)))return e;
    if((d->last_response&0x1f00u)!=0x900u)return XPL_SD_ECARD;
    d->phase=18;
    if((e=data(h,8u,0,ext)))return e;
    d->partition_config=ext[179];d->bus_width=ext[183];d->hs_timing=ext[185];
    d->ext_revision=ext[192];d->data_sector_size=ext[61];
    d->capacity_sectors=(uint32_t)ext[212]|((uint32_t)ext[213]<<8)|((uint32_t)ext[214]<<16)|((uint32_t)ext[215]<<24);
    if((d->partition_config&7u)||d->bus_width||(d->hs_timing&15u)||
       d->data_sector_size||d->ext_revision<5u||d->capacity_sectors<=8388608u)return XPL_SD_EUNSUPPORTED;
    h->ready=1;d->phase=19;return 0;
}
int xpl_emmc_reader_clock_9m6(xpl_emmc_reader *h) {
    int e;uint32_t cfg;
    if(!h||!h->diag||!h->ready||!h->fresh_started||h->diag->max_clock_hz<9600000u)return XPL_SD_EPREP;
    h->ready=0;
    if((e=wait(h,0x24u,4,3u,0))||(e=wr(h,0x2cu,2,3u))||
       (e=full_read(h,0xfc4004d4u,&cfg))||
       (e=clock_commit(h,(cfg&~0x371fu)|3u,0,0,0))||
       (e=full_read(h,0xfc4004d4u,&h->diag->fast_cfg)))return e;
    if((h->diag->fast_cfg&0x371fu)!=3u)return XPL_SD_EHOST;
    if((e=wr(h,0x2cu,2,1u))||(e=wait(h,0x2cu,2,2u,2u))||
       (e=wr(h,0x2cu,2,7u))||(e=rd(h,0x2cu,2,&h->diag->fast_clock)))return e;
    h->ready=1;return 0;
}
int xpl_emmc_read512(void *ctx,uint64_t first,uint32_t count,void *out) {
    xpl_emmc_reader *h=ctx;uint8_t *dst=out;
    if(!h||!h->ready||!out||!count||count>128u)return XPL_SD_EINVAL;
    if(first>=h->diag->capacity_sectors||count>h->diag->capacity_sectors-first)return XPL_SD_EBOUNDS;
    for(uint32_t i=0;i<count;i++) {
        int e=data(h,17u,(uint32_t)first+i,dst+i*512u);
        if(e){h->ready=0;return e;}h->diag->blocks++;
    }
    return 0;
}
int xpl_emmc_reader_close(xpl_emmc_reader *h) {
    int e=0;uint32_t v;xpl_emmc_read_diag *d;unsigned failed;
    if(!h||!h->diag)return XPL_SD_EINVAL;
    d=h->diag;failed=!h->ready;h->ready=0;
    if(h->started) {
    /* A failed command may leave CMD/DAT inhibit set. Reset only those two
     * host lines, never the card, full controller, clocks or voltage. */
    if((failed&&d->commands)||(d->last_irq&0xffff8000u)) {
        if(wr(h,0x2fu,1,6u)||wait(h,0x2fu,1,6u,0))e=XPL_SD_EIO;
    }
    if(h->fresh_started) {
        if(wr(h,0x2cu,2,0)||wr(h,0x28u,1,d->saved_host)||
           wr(h,0x3eu,2,d->saved_host2)||wr(h,0x29u,1,d->saved_power)||
           wr(h,0x10cu,4,d->saved_vendor)||wr(h,0x100u,4,d->saved_dll)||
           clock_commit(h,d->saved_cfg,d->saved_m,d->saved_n,d->saved_d))e=XPL_SD_EIO;
        if(full_read(h,0xfc4004d4u,&v)||v!=d->saved_cfg)e=XPL_SD_EIO;
        if(acknowledge_power(h))e=XPL_SD_EIO;
    }
#define RESTORE(o,w,n) do {if(wr(h,o,w,d->saved_##n)||rd(h,o,w,&v)||v!=d->saved_##n)e=XPL_SD_EIO;} while(0)
    RESTORE(4u,2,size);RESTORE(6u,2,count);RESTORE(0x0cu,2,transfer);
    RESTORE(8u,4,arg);RESTORE(0x2eu,1,timeout);
    RESTORE(0x2cu,2,clock);
    RESTORE(0x34u,4,enable);RESTORE(0x38u,4,signal);
#undef RESTORE
    }
    if(h->core_started) {
        if(h->io.write(h->io.ctx,0xf9824078u,4,d->core_before)||
           h->io.read(h->io.ctx,0xf9824078u,4,&d->core_after)||d->core_after!=d->core_before)e=XPL_SD_EIO;
    }
    h->core_started=0;
    if(h->pll_started) {
        if(h->io.read(h->io.ctx,0xfc401480u,4,&v)||
           h->io.write(h->io.ctx,0xfc401480u,4,(v&~16u)|(d->pll_vote_before&16u)))e=XPL_SD_EIO;
    }
    if(d->pll_mode_before||h->pll_started) {
        if(h->io.read(h->io.ctx,0xfc401480u,4,&d->pll_vote_after)||
           h->io.read(h->io.ctx,0xfc401dc0u,4,&d->pll_mode_after)||d->pll_vote_after!=d->pll_vote_before)e=XPL_SD_EIO;
    }
    h->pll_started=0;
    h->started=0;h->fresh_started=0;d->restore_result=e;d->restored=!e;return e;
}
