/* SPDX-License-Identifier: MIT. Existing-splash decoder; no display init. */
#include "xpl_hw_internal.h"

static enum xpl_hw_status power_state(const struct xpl_hw_display_snapshot *s)
{
    if(!(s->gdsc&0x80000000u)||!(s->ahb_clock&1)||!(s->core_clock&1)||
       (s->ahb_clock&0x80000000u)||(s->core_clock&0x80000000u))return XPL_HW_DISPLAY_OFF;
    if((s->revision&0xffff0000u)!=0x10050000u&&(s->revision&0xffff0000u)!=0x10090000u)return XPL_HW_RESOURCE_UNSUPPORTED;
    return XPL_HW_OK;
}

static enum xpl_hw_status select_ctl(const struct xpl_hw_display_snapshot *s,
    unsigned *ctl,unsigned *intf)
{
    unsigned active=0;
    for(unsigned i=0;i<XPL_HW_CTL_COUNT;i++){
        uint32_t top=s->top[i];unsigned interface=(top>>4)&15;
        /* OUT_SEL0 is NO_INTF, including writeback/default CTLs. It is not a
         * primary DSI scanout candidate even when other TOP fields are nonzero.
         * DSI hardware INTF1/2 are encoded as OUT_SEL2/3 in CTL_TOP. */
        if(!interface)continue;
        if(interface!=2&&interface!=3)return XPL_HW_DISPLAY_TOPOLOGY;
        /* Qualcomm LK emits ancillary0x1f00; the Linux donor emits zero.
         * Accept only these exact known forms, with video/cmd bit17. */
        uint32_t ancillary=top&0x1f00u;
        if((top&~(0x1ff0u|(1u<<17)))||(ancillary&&ancillary!=0x1f00u))return XPL_HW_DISPLAY_TOPOLOGY;
        *ctl=i;*intf=interface;active++;
    }
    if(!active)return XPL_HW_DISPLAY_OFF;
    if(active!=1)return XPL_HW_DISPLAY_TOPOLOGY;
    return XPL_HW_OK;
}

static enum xpl_hw_status topology(const struct xpl_hw_config *c,
    const struct xpl_hw_display_snapshot *s,unsigned *ctl,unsigned *mixer,unsigned *pipe,unsigned *intf)
{
    unsigned staged=0;uint32_t allowed=0;
    enum xpl_hw_status status=select_ctl(s,ctl,intf);
    if(status)return status;
    for(unsigned p=0;p<XPL_HW_PIPE_COUNT;p++)allowed|=7u<<(p*3+(p>=8?2:0));
    for(unsigned m=0;m<XPL_HW_MIXER_COUNT;m++){
        uint32_t layer=s->layer[m];
        if(s->layer_ext[m]||(layer&~allowed)||(layer&&!c->mixer_offsets[m]))return XPL_HW_DISPLAY_TOPOLOGY;
        for(unsigned p=0;p<XPL_HW_PIPE_COUNT;p++)if((layer>>(p*3+(p>=8?2:0)))&7){*mixer=m;*pipe=p;staged++;}
    }
    return staged==1?XPL_HW_OK:XPL_HW_DISPLAY_TOPOLOGY;
}

static enum xpl_hw_status pixel_layout(uint32_t format,uint32_t unpack,struct xpl_hw_surface *out)
{
    unsigned bpp=((format>>9)&3)+1,count=((format>>12)&3)+1,seen=0,shift=0;
    uint32_t masks[4]={0,0,0,0};unsigned bits[4];
    static const unsigned widths[4]={4,5,6,8};
    if((format&~0x000237ffu)||!(format&(1u<<17)))return XPL_HW_DISPLAY_FORMAT;
    /* SRCC3_EN is understood only with a full fourth 8-bit alpha component. */
    if((format&(1u<<8))&&(bpp!=4||count!=4))return XPL_HW_DISPLAY_FORMAT;
    if(bpp==2){if(count!=3||(format&0xff)!=0x16)return XPL_HW_DISPLAY_FORMAT;}
    else if(bpp==3){if(count!=3||(format&0xff)!=0x3f)return XPL_HW_DISPLAY_FORMAT;}
    else if(bpp==4){if(count!=4||(format&0xff)!=0xff)return XPL_HW_DISPLAY_FORMAT;}
    else return XPL_HW_DISPLAY_FORMAT;
    for(unsigned i=0;i<4;i++)bits[i]=widths[(format>>(i*2))&3];
    for(unsigned i=0;i<count;i++){
        unsigned channel=(unpack>>(i*8))&255;
        if(channel>3||(count==3&&channel==3)||(seen&(1u<<channel)))return XPL_HW_DISPLAY_FORMAT;
        seen|=1u<<channel;
        if(shift+bits[channel]>32)return XPL_HW_DISPLAY_FORMAT;
        masks[channel]=((1u<<bits[channel])-1u)<<shift;shift+=bits[channel];
    }
    if(shift!=bpp*8||seen!=(count==4?15u:7u)||(count==3&&(unpack>>24)))return XPL_HW_DISPLAY_FORMAT;
    out->bytes_per_pixel=(uint8_t)bpp;out->red_mask=masks[2];out->green_mask=masks[0];out->blue_mask=masks[1];out->alpha_mask=masks[3];
    return XPL_HW_OK;
}

static int measured_prefix_viewport(const struct xpl_hw_config *c,
    const struct xpl_hw_pipe_snapshot *p,struct xpl_hw_surface *v)
{
    /* native-menu-004 measured S1 RGB888 scanout at 0x03400000, while the
     * existing DT reserves splash only from 0x03401000. Do not expand that
     * reservation or write the preceding dfps_data_mem page. This one observed
     * geometry is supported by clipping whole rows, without a source-register
     * change. All other before-splash addresses remain errors. */
    if(p->address!=0x03400000u||c->splash.base!=0x03401000u||c->splash.bytes!=0x02200000u||
       v->width!=1080||v->height!=1920||v->stride_bytes!=3240||v->bytes_per_pixel!=3||
       p->format!=0x2243fu||p->unpack!=0x00020001u)return 0;
    uint64_t prefix=c->splash.base-v->scanout_base_pa;
    uint64_t rows=(prefix+v->stride_bytes-1u)/v->stride_bytes;
    if(prefix!=4096||rows>=v->scanout_height)return 0;
    uint64_t skipped_bytes=rows*v->stride_bytes;
    if(skipped_bytes>=v->scanout_bytes||v->scanout_base_pa>UINT64_MAX-skipped_bytes)return 0;
    v->top_rows_skipped=(uint32_t)rows;
    v->base_pa=v->scanout_base_pa+skipped_bytes;
    v->height=v->scanout_height-v->top_rows_skipped;
    v->buffer_bytes=v->scanout_bytes-skipped_bytes;
    return hw_contains(c->splash,v->base_pa,v->buffer_bytes);
}

enum xpl_hw_status xpl_hw_decode_display(const struct xpl_hw_config *c,
    const struct xpl_hw_display_snapshot *s,const struct xpl_hw_range *excluded,size_t excluded_count,
    struct xpl_hw_surface *out,struct xpl_hw_diag *diag)
{
    enum xpl_hw_status status;struct xpl_hw_surface v;unsigned ctl=0,mixer=0,pipe=0,intf=0,in_ram=0;
    hw_diag_init(diag,XPL_HW_PHASE_SURFACE);if(out)hw_zero(out,sizeof(*out));
    if(!out||!s||!hw_resources_valid(c)||(!excluded&&excluded_count)||excluded_count>64)return hw_fail(diag,XPL_HW_ARGUMENT,0);
    status=power_state(s);if(status)return hw_fail(diag,status,1);
    status=topology(c,s,&ctl,&mixer,&pipe,&intf);if(status)return hw_fail(diag,status,2);
    if(s->ctl!=ctl||s->mixer!=mixer||s->pipe!=pipe||s->intf!=intf)return hw_fail(diag,XPL_HW_DISPLAY_TOPOLOGY,3);
    const struct xpl_hw_pipe_snapshot *p=&s->source;
    if(!p->address||p->xy||p->out_xy||p->opmode||p->secure||p->decimation||
       p->size!=p->image_size||p->size!=p->out_size||p->out_size!=s->mixer_size||
       (p->stride>>16))return hw_fail(diag,XPL_HW_DISPLAY_FORMAT,4);
    hw_zero(&v,sizeof(v));v.width=p->size&65535;v.height=p->size>>16;v.stride_bytes=p->stride;
    status=pixel_layout(p->format,p->unpack,&v);if(status)return hw_fail(diag,status,5);
    if(!v.width||!v.height||v.width>4096||v.height>4096||v.stride_bytes<v.width*v.bytes_per_pixel)return hw_fail(diag,XPL_HW_DISPLAY_FORMAT,6);
    v.base_pa=v.scanout_base_pa=p->address;
    v.scanout_height=v.height;
    v.buffer_bytes=v.scanout_bytes=(uint64_t)v.stride_bytes*v.height;
    /* The whole hardware scanout, including an untouched prefix, must be RAM. */
    for(unsigned i=0;i<c->ram_count;i++)if(hw_contains(c->ram[i],v.scanout_base_pa,v.scanout_bytes))in_ram=1;
    if(!in_ram)return hw_fail(diag,XPL_HW_SURFACE_RANGE,8);
    if(!hw_contains(c->splash,v.base_pa,v.buffer_bytes)&&!measured_prefix_viewport(c,p,&v))return hw_fail(diag,XPL_HW_SURFACE_RANGE,7);
    struct xpl_hw_range surface={v.base_pa,v.buffer_bytes};
    for(unsigned i=0;i<c->reserved_count;i++){
        struct xpl_hw_range r=c->reserved[i];
        if(r.base==c->splash.base&&r.bytes==c->splash.bytes)continue;
        if(hw_overlap(surface,r))return hw_fail(diag,XPL_HW_SURFACE_RANGE,9);
    }
    for(size_t i=0;i<excluded_count;i++){
        if(!hw_range_valid(excluded[i]))return hw_fail(diag,XPL_HW_ARGUMENT,10);
        if(hw_overlap(surface,excluded[i]))return hw_fail(diag,XPL_HW_SURFACE_RANGE,11);
    }
    v.command_mode=!!(s->top[ctl]&(1u<<17));
    if(!v.command_mode&&s->timing_enabled!=1)return hw_fail(diag,XPL_HW_DISPLAY_OFF,12);
    v.ctl_top=s->top[ctl];v.ctl_start_pa=c->mdp.base+c->ctl_offsets[ctl]+0x1c;
    if(!hw_contains(c->mdp,v.ctl_start_pa,4))return hw_fail(diag,XPL_HW_RESOURCE_UNSUPPORTED,13);
    /* MDP1.9 uses shadowed pipe/mixer/interface state. The pinned LK donor's
     * single RGB0/LM0 command path commits0x22048 plus INTF1 bit30. START alone
     * can continue an older latched geometry after a warm firmware transition. */
    if(v.command_mode&&v.ctl_top==0x21f20u&&s->revision==0x10090000u&&ctl==0&&mixer==0&&pipe==3&&intf==2&&
       p->format==0x2243fu&&p->unpack==0x20001u&&v.width==1080&&v.scanout_height==1920&&
       v.stride_bytes==3240) {
        v.ctl_flush_pa=v.ctl_start_pa-4;v.ctl_flush_mask=0x40022048u;
        if(!hw_contains(c->mdp,v.ctl_flush_pa,4))return hw_fail(diag,XPL_HW_RESOURCE_UNSUPPORTED,14);
    }
    v.ready=1;hw_copy(out,&v,sizeof(v));return XPL_HW_OK;
}

static int read_pipe(const struct xpl_hw_io *io,uint64_t base,struct xpl_hw_pipe_snapshot *p,struct xpl_hw_diag *d)
{
    static const uint32_t offset[12]={0,4,8,12,16,20,36,48,52,56,112,180};
    uint32_t values[12];
    for(unsigned i=0;i<12;i++)if(!hw_read(io,base+offset[i],&values[i],d))return 0;
    p->size=values[0];p->image_size=values[1];p->xy=values[2];p->out_size=values[3];p->out_xy=values[4];
    p->address=values[5];p->stride=values[6];p->format=values[7];p->unpack=values[8];p->opmode=values[9];p->secure=values[10];p->decimation=values[11];
    return 1;
}

enum xpl_hw_status xpl_hw_display_discover(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
    const struct xpl_hw_range *excluded,size_t excluded_count,struct xpl_hw_surface *out,
    struct xpl_hw_display_snapshot *s,struct xpl_hw_diag *diag)
{
    unsigned ctl=0,mixer=0,pipe=0,intf=0;uint32_t again;enum xpl_hw_status status;
    struct xpl_hw_pipe_snapshot second;struct xpl_hw_diag decoded;
    hw_diag_init(diag,XPL_HW_PHASE_POWER);if(out)hw_zero(out,sizeof(*out));if(s)hw_zero(s,sizeof(*s));
    if(!out||!s||!hw_resources_valid(c)||!io||!io->read32||(!excluded&&excluded_count)||excluded_count>64)return hw_fail(diag,XPL_HW_ARGUMENT,0);
    if(!hw_read(io,c->gdsc.base,&s->gdsc,diag))return XPL_HW_MMIO_FAULT;
    if(!(s->gdsc&0x80000000u))return hw_fail(diag,XPL_HW_DISPLAY_OFF,1);
    if(!hw_read(io,c->clocks.base+0x2308,&s->ahb_clock,diag)||!hw_read(io,c->clocks.base+0x231c,&s->core_clock,diag))return XPL_HW_MMIO_FAULT;
    if(!(s->ahb_clock&1)||!(s->core_clock&1)||(s->ahb_clock&0x80000000u)||(s->core_clock&0x80000000u))return hw_fail(diag,XPL_HW_DISPLAY_OFF,2);
    if(!hw_read(io,c->mdp.base+0x1000,&s->revision,diag))return XPL_HW_MMIO_FAULT;
    status=power_state(s);if(status)return hw_fail(diag,status,3);
    if(diag)diag->phase=XPL_HW_PHASE_TOPOLOGY;
    for(unsigned i=0;i<XPL_HW_CTL_COUNT;i++){
        uint64_t address=c->mdp.base+c->ctl_offsets[i]+0x14;
        if(!hw_contains(c->mdp,address,4))return hw_fail(diag,XPL_HW_RESOURCE_UNSUPPORTED,4);
        if(!hw_read(io,address,&s->top[i],diag))return XPL_HW_MMIO_FAULT;
    }
    status=select_ctl(s,&ctl,&intf);
    if(status)return hw_fail(diag,status,status==XPL_HW_DISPLAY_OFF?5:6);
    for(unsigned m=0;m<XPL_HW_MIXER_COUNT;m++){
        uint64_t address=c->mdp.base+c->ctl_offsets[ctl]+hw_layer_offset(m);
        if(!hw_read(io,address,&s->layer[m],diag)||!hw_read(io,address+0x40,&s->layer_ext[m],diag))return XPL_HW_MMIO_FAULT;
    }
    status=topology(c,s,&ctl,&mixer,&pipe,&intf);if(status)return hw_fail(diag,status,7);
    s->ctl=ctl;s->mixer=mixer;s->pipe=pipe;s->intf=intf;
    uint64_t pipe_pa=c->mdp.base+c->pipe_offsets[pipe],mixer_pa=c->mdp.base+c->mixer_offsets[mixer];
    if(!hw_contains(c->mdp,pipe_pa,184)||!hw_contains(c->mdp,mixer_pa,8))return hw_fail(diag,XPL_HW_RESOURCE_UNSUPPORTED,8);
    if(!read_pipe(io,pipe_pa,&s->source,diag)||!hw_read(io,mixer_pa+4,&s->mixer_size,diag))return XPL_HW_MMIO_FAULT;
    if(!(s->top[ctl]&(1u<<17))){
        uint64_t timing_pa=c->mdp.base+0x6b000u+(uint64_t)(intf-1u)*0x800u;
        if(!hw_read(io,timing_pa,&s->timing_enabled,diag))return XPL_HW_MMIO_FAULT;
    }
    if(!read_pipe(io,pipe_pa,&second,diag)||!hw_read(io,c->mdp.base+c->ctl_offsets[ctl]+0x14,&again,diag))return XPL_HW_MMIO_FAULT;
    if(!hw_equal_bytes(&s->source,&second,sizeof(second))||again!=s->top[ctl])return hw_fail(diag,XPL_HW_CHANGED,9);
    status=xpl_hw_decode_display(c,s,excluded,excluded_count,out,&decoded);
    if(status&&diag){diag->status=status;diag->phase=decoded.phase;diag->detail=decoded.detail;}
    return status;
}

enum xpl_hw_status xpl_hw_present(const struct xpl_hw_config *c,const struct xpl_hw_io *io,
    const struct xpl_hw_surface *surface,struct xpl_hw_diag *diag)
{
    uint32_t top;unsigned valid=0;
    hw_diag_init(diag,XPL_HW_PHASE_PRESENT);
    if(!hw_resources_valid(c)||!surface||!surface->ready||!io||!io->read32||!io->fence)return hw_fail(diag,XPL_HW_ARGUMENT,0);
    for(unsigned i=0;i<XPL_HW_CTL_COUNT;i++)if(surface->ctl_start_pa==c->mdp.base+c->ctl_offsets[i]+0x1c)valid=1;
    if(!valid||!hw_contains(c->splash,surface->base_pa,surface->buffer_bytes)||surface->command_mode!=!!(surface->ctl_top&(1u<<17)))return hw_fail(diag,XPL_HW_ARGUMENT,1);
    if(!hw_read(io,surface->ctl_start_pa-8,&top,diag))return XPL_HW_MMIO_FAULT;
    if(top!=surface->ctl_top)return hw_fail(diag,XPL_HW_CHANGED,2);
    if(surface->ctl_flush_mask && (surface->ctl_flush_mask!=0x40022048u||
       surface->ctl_flush_pa!=surface->ctl_start_pa-4||surface->ctl_top!=0x21f20u||
       !surface->command_mode||!hw_contains(c->mdp,surface->ctl_flush_pa,4)))
        return hw_fail(diag,XPL_HW_ARGUMENT,3);
    io->fence(io->context);
    if(surface->ctl_flush_mask&&!hw_write(io,surface->ctl_flush_pa,surface->ctl_flush_mask,diag))
        return XPL_HW_MMIO_FAULT;
    if(surface->command_mode&&!hw_write(io,surface->ctl_start_pa,1,diag))return XPL_HW_MMIO_FAULT;
    return XPL_HW_OK;
}
