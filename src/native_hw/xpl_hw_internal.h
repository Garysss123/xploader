/* SPDX-License-Identifier: MIT */
#ifndef XPL_HW_INTERNAL_H
#define XPL_HW_INTERNAL_H
#include "xpl_hw.h"

static inline void hw_zero(void *p, size_t n)
{ unsigned char *v=p; while(n--) *v++=0; }
static inline void hw_copy(void *d, const void *s, size_t n)
{ unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; }
static inline int hw_equal_bytes(const void *a,const void *b,size_t n)
{ const unsigned char *x=a,*y=b; while(n--) if(*x++!=*y++) return 0; return 1; }
static inline int hw_range_valid(struct xpl_hw_range r)
{ return r.bytes && r.base<=UINT64_MAX-r.bytes; }
static inline int hw_contains(struct xpl_hw_range r,uint64_t base,uint64_t bytes)
{ return hw_range_valid(r) && bytes && base>=r.base && base-r.base<=r.bytes && bytes<=r.bytes-(base-r.base); }
static inline int hw_overlap(struct xpl_hw_range a,struct xpl_hw_range b)
{ return hw_range_valid(a) && hw_range_valid(b) && a.base<b.base+b.bytes && b.base<a.base+a.bytes; }
static inline enum xpl_hw_status hw_fail(struct xpl_hw_diag *d,enum xpl_hw_status s,uint32_t detail)
{ if(d){d->status=s;d->detail=detail;} return s; }
static inline void hw_diag_init(struct xpl_hw_diag *d,enum xpl_hw_phase phase)
{ if(d){hw_zero(d,sizeof(*d));d->phase=phase;} }
static inline int hw_read(const struct xpl_hw_io *io,uint64_t pa,uint32_t *v,struct xpl_hw_diag *d)
{
    if(d){d->address=pa;d->value=0;d->reads++;}
    if(!io||!io->read32||!v||!io->read32(io->context,pa,v)){hw_fail(d,XPL_HW_MMIO_FAULT,0);return 0;}
    if(d)d->value=*v;
    return 1;
}
static inline int hw_write(const struct xpl_hw_io *io,uint64_t pa,uint32_t v,struct xpl_hw_diag *d)
{
    if(d){d->address=pa;d->value=v;d->writes++;}
    if(!io||!io->write32||!io->write32(io->context,pa,v)){hw_fail(d,XPL_HW_MMIO_FAULT,0);return 0;}
    return 1;
}
static inline int hw_resources_valid(const struct xpl_hw_config *c)
{
    static const uint32_t pipes[10]={0x5000,0x7000,0x9000,0x15000,0x17000,0x19000,0x25000,0x27000,0xb000,0x1b000};
    static const uint32_t mixers[6]={0x45000,0x46000,0x47000,0,0,0x4a000};
    if (!(c && c->complete && c->ram_count && c->ram_count<=XPL_HW_MAX_RAM &&
      c->reserved_count<=XPL_HW_MAX_RESERVED && c->mdp.base==0xfd900000u &&
      c->mdp.bytes>=0x80000u && c->mdp.bytes<=0x100000u &&
      c->gdsc.base==0xfd8c2304u && c->gdsc.bytes==4 &&
      c->clocks.base==0xfd8c0000u && c->clocks.bytes>=0x5200u &&
      c->clocks.bytes<=0x10000u && c->spmi.base==0xfc4cf000u &&
      c->spmi.bytes==0x1000u && c->spmi_channel==0 && c->spmi_ee==0))return 0;
    if(c->watchdog.bytes&&(c->watchdog.base!=0xf9017000u||c->watchdog.bytes!=0x1000u))return 0;
    for(unsigned i=0;i<5;i++)if(c->ctl_offsets[i]!=0x2000+i*0x200)return 0;
    for(unsigned i=0;i<10;i++)if(c->pipe_offsets[i]!=pipes[i])return 0;
    for(unsigned i=0;i<6;i++)if(c->mixer_offsets[i]!=mixers[i])return 0;
    return 1;
}
static inline uint32_t hw_layer_offset(unsigned mixer)
{ return mixer==5 ? 0x24u : mixer*4u; }
#endif
