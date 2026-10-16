/* SPDX-License-Identifier: MIT. Bounded, read-only flattened-device-tree resolver. */
#include "xpl_hw_internal.h"

#define DT_MAX (2u*1024u*1024u)
#define DT_DEPTH 32u
#define DT_TOKENS 131072u
struct dt { const unsigned char *p; uint32_t total,so,ss,no,ns,ro; };
struct prop { const unsigned char *p; uint32_t n; unsigned exists; };
struct node {
    uint32_t offset,parent;
    uint8_t parent_ac,parent_sc,ac,sc;
    unsigned memory_container,reserved_container;
};
struct collect { struct xpl_hw_config *c; uint32_t mdp_node,fb_node,splash_handle,gdsc_handle; unsigned found; };

static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t be64(const unsigned char *p)
{ return (uint64_t)be32(p)<<32|be32(p+4); }
static int span(uint32_t off,uint32_t n,uint32_t total)
{ return off<=total && n<=total-off; }
static int str_eq(const unsigned char *p,uint32_t n,const char *s)
{
    uint32_t i=0;
    while(i<n && s[i] && p[i]==(unsigned char)s[i])i++;
    return i<n && p[i]==0 && s[i]==0;
}
static int bounded_z(const unsigned char *p,uint32_t n,uint32_t *length)
{ for(uint32_t i=0;i<n;i++)if(!p[i]){*length=i;return 1;}return 0; }

static enum xpl_hw_status validate(const void *bytes,size_t available,struct dt *d)
{
    uint32_t pos,depth=0,count=0,name_len;unsigned root=0,finished=0,child[DT_DEPTH];
    if(!bytes||available<40)return XPL_HW_FDT_INVALID;
    d->p=bytes;d->total=be32(d->p+4);d->so=be32(d->p+8);d->no=be32(d->p+12);
    d->ro=be32(d->p+16);d->ns=be32(d->p+32);d->ss=be32(d->p+36);
    if(be32(d->p)!=0xd00dfeedu||d->total<40||d->total>available||d->total>DT_MAX||
       be32(d->p+20)!=17||be32(d->p+24)>17||d->so<40||d->no<40||d->ro<40||
       (d->so&3)||(d->ro&7)||!span(d->so,d->ss,d->total)||!span(d->no,d->ns,d->total)||
       (d->so<d->no+d->ns&&d->no<d->so+d->ss))return XPL_HW_FDT_INVALID;
    pos=d->ro;
    while(span(pos,16,d->total)&&pos+16<=d->so&&pos+16<=d->no){
        uint64_t a=be64(d->p+pos),n=be64(d->p+pos+8);pos+=16;
        if(!a&&!n)break;
        if(!n||a>UINT64_MAX-n)return XPL_HW_FDT_INVALID;
    }
    if(pos<d->ro+16||!span(pos-16,16,d->total)||be64(d->p+pos-16)||be64(d->p+pos-8))return XPL_HW_FDT_INVALID;
    pos=d->so;
    while(span(pos,4,d->so+d->ss)&&++count<=DT_TOKENS){
        uint32_t token=be32(d->p+pos);pos+=4;
        if(token==1){
            if(finished||depth==DT_DEPTH)return XPL_HW_FDT_LIMIT;
            if(!bounded_z(d->p+pos,d->so+d->ss-pos,&name_len)||name_len>255)return XPL_HW_FDT_INVALID;
            if(!depth){if(root||name_len)return XPL_HW_FDT_INVALID;root=1;}
            else child[depth-1]=1;
            child[depth++]=0;pos=(pos+name_len+4)&~3u;
            if(pos>d->so+d->ss)return XPL_HW_FDT_INVALID;
        }else if(token==2){if(!depth)return XPL_HW_FDT_INVALID;if(!--depth)finished=1;}
        else if(token==3){
            uint32_t n,no;
            if(!depth||child[depth-1]||!span(pos,8,d->so+d->ss))return XPL_HW_FDT_INVALID;
            n=be32(d->p+pos);no=be32(d->p+pos+4);pos+=8;
            if(!span(pos,n,d->so+d->ss)||no>=d->ns||!bounded_z(d->p+d->no+no,d->ns-no,&name_len))return XPL_HW_FDT_INVALID;
            pos=(pos+n+3)&~3u;if(pos>d->so+d->ss)return XPL_HW_FDT_INVALID;
        }else if(token==4)continue;
        else if(token==9)return root&&finished&&!depth&&pos==d->so+d->ss?XPL_HW_OK:XPL_HW_FDT_INVALID;
        else return XPL_HW_FDT_INVALID;
    }
    return count>DT_TOKENS?XPL_HW_FDT_LIMIT:XPL_HW_FDT_INVALID;
}

static enum xpl_hw_status property(const struct dt *d,uint32_t node,const char *key,struct prop *out)
{
    uint32_t pos=node+4,n;
    hw_zero(out,sizeof(*out));
    if(!bounded_z(d->p+pos,d->so+d->ss-pos,&n))return XPL_HW_FDT_INVALID;
    pos=(pos+n+4)&~3u;
    while(pos<d->so+d->ss){
        uint32_t token=be32(d->p+pos);pos+=4;
        if(token==4)continue;
        if(token!=3)break;
        uint32_t len=be32(d->p+pos),name=be32(d->p+pos+4);pos+=8;
        if(str_eq(d->p+d->no+name,d->ns-name,key)){
            if(out->exists)return XPL_HW_FDT_INVALID;
            out->p=d->p+pos;out->n=len;out->exists=1;
        }
        pos=(pos+len+3)&~3u;
    }
    return XPL_HW_OK;
}
static enum xpl_hw_status u32prop(const struct dt *d,uint32_t node,const char *key,uint32_t *value,unsigned required)
{
    struct prop p;enum xpl_hw_status s=property(d,node,key,&p);if(s)return s;
    if(!p.exists)return required?XPL_HW_RESOURCE_MISSING:XPL_HW_OK;
    if(p.n!=4)return XPL_HW_FDT_INVALID;
    *value=be32(p.p);return XPL_HW_OK;
}
static int has_compat(const struct dt *d,uint32_t node,const char *s)
{
    struct prop p;uint32_t off=0,n;
    if(property(d,node,"compatible",&p)||!p.exists)return 0;
    while(off<p.n){if(!bounded_z(p.p+off,p.n-off,&n))return 0;if(str_eq(p.p+off,p.n-off,s))return 1;off+=n+1;}
    return 0;
}
static int node_name(const struct dt *d,uint32_t node,const char *s)
{ return str_eq(d->p+node+4,d->so+d->ss-node-4,s); }
static enum xpl_hw_status reg_at(const struct dt *d,const struct node *n,unsigned index,struct xpl_hw_range *r)
{
    struct prop p;uint32_t cells=n->parent_ac+n->parent_sc,stride;
    enum xpl_hw_status s=property(d,n->offset,"reg",&p);if(s)return s;
    if(!p.exists)return XPL_HW_RESOURCE_MISSING;
    if(!n->parent_ac||!n->parent_sc||n->parent_ac>2||n->parent_sc>2)return XPL_HW_FDT_INVALID;
    stride=cells*4;if(p.n%stride||index>=p.n/stride)return XPL_HW_FDT_INVALID;
    const unsigned char *v=p.p+index*stride;
    r->base=n->parent_ac==2?be64(v):be32(v);v+=n->parent_ac*4;
    r->bytes=n->parent_sc==2?be64(v):be32(v);
    if(r->base>UINT64_MAX-r->bytes)return XPL_HW_FDT_INVALID;
    return XPL_HW_OK;
}
static enum xpl_hw_status named_reg(const struct dt *d,const struct node *n,const char *name,struct xpl_hw_range *r)
{
    struct prop p;uint32_t off=0,len;unsigned index=0,found=0;
    enum xpl_hw_status s=property(d,n->offset,"reg-names",&p);if(s)return s;
    if(!p.exists)return XPL_HW_RESOURCE_MISSING;
    while(off<p.n){
        if(!bounded_z(p.p+off,p.n-off,&len))return XPL_HW_FDT_INVALID;
        if(str_eq(p.p+off,p.n-off,name)){if(found)return XPL_HW_FDT_INVALID;s=reg_at(d,n,index,r);if(s)return s;found=1;}
        off+=len+1;index++;
    }
    return found?XPL_HW_OK:XPL_HW_RESOURCE_MISSING;
}
static enum xpl_hw_status add_range(struct xpl_hw_range *list,unsigned *count,unsigned limit,struct xpl_hw_range r,unsigned ram)
{
    if(!r.bytes)return XPL_HW_OK;
    if(!hw_range_valid(r))return XPL_HW_FDT_INVALID;
    for(unsigned i=0;i<*count;i++){
        if(list[i].base==r.base&&list[i].bytes==r.bytes)return XPL_HW_OK;
        if(ram&&hw_overlap(list[i],r))return XPL_HW_FDT_INVALID;
    }
    if(*count==limit)return XPL_HW_FDT_LIMIT;
    list[(*count)++]=r;return XPL_HW_OK;
}
static enum xpl_hw_status all_regs(const struct dt *d,const struct node *n,struct xpl_hw_range *list,unsigned *count,unsigned max,unsigned ram,unsigned skip_zero_base)
{
    struct prop p;enum xpl_hw_status s=property(d,n->offset,"reg",&p);if(s)return s;
    if(!p.exists)return XPL_HW_RESOURCE_MISSING;
    if(!n->parent_ac||!n->parent_sc||n->parent_ac>2||n->parent_sc>2)return XPL_HW_FDT_INVALID;
    unsigned stride=4*(n->parent_ac+n->parent_sc);
    if(p.n%stride)return XPL_HW_FDT_INVALID;
    for(unsigned i=0;i<p.n/stride;i++){
        struct xpl_hw_range r;s=reg_at(d,n,i,&r);if(s)return s;
        if(skip_zero_base&&!r.base)continue;
        s=add_range(list,count,max,r,ram);if(s)return s;
    }
    return XPL_HW_OK;
}
static enum xpl_hw_status offset_list(const struct dt *d,uint32_t node,const char *key,uint32_t *out,const uint32_t *expected,unsigned count)
{
    struct prop p;enum xpl_hw_status s=property(d,node,key,&p);if(s)return s;
    if(!p.exists)return XPL_HW_RESOURCE_MISSING;
    if(p.n!=count*4)return XPL_HW_RESOURCE_UNSUPPORTED;
    for(unsigned i=0;i<count;i++){out[i]=be32(p.p+i*4);if(out[i]!=expected[i])return XPL_HW_RESOURCE_UNSUPPORTED;}
    return XPL_HW_OK;
}
static enum xpl_hw_status identity_parent(const struct dt *d,const struct node *n)
{
    struct prop p;enum xpl_hw_status s;
    if(!n->parent||!node_name(d,n->parent,"soc"))return XPL_HW_RESOURCE_UNSUPPORTED;
    s=property(d,n->parent,"ranges",&p);if(s)return s;
    if(!p.exists)return XPL_HW_RESOURCE_UNSUPPORTED;
    if(!p.n)return XPL_HW_OK;
    /* Captured Sumire /soc: one 32-bit child address, 64-bit root address,
     * 32-bit size, mapping [0,0xffffffff) identically. */
    if(p.n==16&&n->parent_ac==1&&n->parent_sc==1&&be32(p.p)==0&&
       be64(p.p+4)==0&&be32(p.p+12)==UINT32_MAX)return XPL_HW_OK;
    return XPL_HW_RESOURCE_UNSUPPORTED;
}

typedef enum xpl_hw_status (*visit_fn)(const struct dt *,const struct node *,const struct node *,void *);
static enum xpl_hw_status walk(const struct dt *d,visit_fn visit,void *context)
{
    struct node stack[DT_DEPTH];unsigned depth=0;uint32_t pos=d->so;
    while(pos<d->so+d->ss){
        uint32_t start=pos,token=be32(d->p+pos),len;pos+=4;
        if(token==1){
            struct node *n=&stack[depth];struct prop p;enum xpl_hw_status s;uint32_t value;
            hw_zero(n,sizeof(*n));n->offset=start;n->parent=depth?stack[depth-1].offset:0;
            n->parent_ac=depth?stack[depth-1].ac:2;n->parent_sc=depth?stack[depth-1].sc:1;
            value=2;s=u32prop(d,start,"#address-cells",&value,0);if(s||value>2)return s?s:XPL_HW_FDT_INVALID;n->ac=(uint8_t)value;
            value=1;s=u32prop(d,start,"#size-cells",&value,0);if(s||value>2)return s?s:XPL_HW_FDT_INVALID;n->sc=(uint8_t)value;
            s=property(d,start,"device_type",&p);if(s)return s;
            n->memory_container=p.exists&&str_eq(p.p,p.n,"memory");
            n->reserved_container=depth==1&&node_name(d,start,"reserved-memory");
            s=visit(d,n,depth?&stack[depth-1]:0,context);if(s)return s;
            if(!bounded_z(d->p+pos,d->so+d->ss-pos,&len))return XPL_HW_FDT_INVALID;
            pos=(pos+len+4)&~3u;depth++;
        }else if(token==2)depth--;
        else if(token==3){len=be32(d->p+pos);pos=(pos+8+len+3)&~3u;}
        else if(token==9)break;
    }
    return XPL_HW_OK;
}

static enum xpl_hw_status collect_node(const struct dt *d,const struct node *n,const struct node *parent,void *context)
{
    struct collect *x=context;struct xpl_hw_config *c=x->c;struct prop p,q;enum xpl_hw_status s;
    if(n->memory_container){s=all_regs(d,n,c->ram,&c->ram_count,XPL_HW_MAX_RAM,1,0);if(s)return s;}
    if(parent&&(parent->memory_container||parent->reserved_container)){
        s=property(d,n->offset,"reg",&p);if(s)return s;
        if(p.exists){
            struct prop legacy;s=property(d,n->offset,"linux,reserve-contiguous-region",&legacy);if(s)return s;
            /* Sony legacy CMA uses base0 as "allocate later", not a physical
             * reservation at address0. Preserve explicit FDT reservation-map
             * and reserved-memory reg entries at address0 normally. */
            s=all_regs(d,n,c->reserved,&c->reserved_count,XPL_HW_MAX_RESERVED,0,
                       parent->memory_container&&legacy.exists);if(s)return s;
        }
    }
    if(has_compat(d,n->offset,"qcom,mdss_mdp")){
        static const uint32_t ctl[5]={0x2000,0x2200,0x2400,0x2600,0x2800};
        static const uint32_t vig[4]={0x5000,0x7000,0x9000,0xb000};
        static const uint32_t rgb[4]={0x15000,0x17000,0x19000,0x1b000};
        static const uint32_t dma[2]={0x25000,0x27000};
        static const uint32_t mixer[4]={0x45000,0x46000,0x47000,0x4a000};uint32_t v[4];
        if(x->found&1)return XPL_HW_FDT_INVALID;
        x->found|=1;x->mdp_node=n->offset;
        s=identity_parent(d,n);if(s)return s;
        s=named_reg(d,n,"mdp_phys",&c->mdp);if(s)return s;
        s=u32prop(d,n->offset,"vdd-supply",&x->gdsc_handle,1);if(s)return s;
        s=offset_list(d,n->offset,"qcom,mdss-ctl-off",c->ctl_offsets,ctl,5);if(s)return s;
        s=offset_list(d,n->offset,"qcom,mdss-pipe-vig-off",v,vig,4);if(s)return s;
        for(unsigned i=0;i<3;i++)c->pipe_offsets[i]=v[i];
        c->pipe_offsets[8]=v[3];
        s=offset_list(d,n->offset,"qcom,mdss-pipe-rgb-off",v,rgb,4);if(s)return s;
        for(unsigned i=0;i<3;i++)c->pipe_offsets[3+i]=v[i];
        c->pipe_offsets[9]=v[3];
        s=offset_list(d,n->offset,"qcom,mdss-pipe-dma-off",v,dma,2);if(s)return s;
        c->pipe_offsets[6]=v[0];c->pipe_offsets[7]=v[1];
        s=offset_list(d,n->offset,"qcom,mdss-mixer-intf-off",v,mixer,4);if(s)return s;
        c->mixer_offsets[0]=v[0];c->mixer_offsets[1]=v[1];c->mixer_offsets[2]=v[2];c->mixer_offsets[5]=v[3];
    }
    if(n->parent==x->mdp_node&&has_compat(d,n->offset,"qcom,mdss-fb")){
        uint32_t index=UINT32_MAX;s=u32prop(d,n->offset,"cell-index",&index,1);if(s)return s;
        if(index==0){if(x->fb_node)return XPL_HW_FDT_INVALID;x->fb_node=n->offset;}
    }
    if(n->parent==x->fb_node&&node_name(d,n->offset,"qcom,cont-splash-memory")){
        if(x->splash_handle)return XPL_HW_FDT_INVALID;
        s=u32prop(d,n->offset,"linux,contiguous-region",&x->splash_handle,1);if(s)return s;
    }
    if(has_compat(d,n->offset,"qcom,mmsscc-8994v2")){
        if(x->found&2)return XPL_HW_FDT_INVALID;
        x->found|=2;
        s=identity_parent(d,n);if(s)return s;s=named_reg(d,n,"cc_base",&c->clocks);if(s)return s;
    }
    if(has_compat(d,n->offset,"qcom,spmi-pmic-arb")){
        if(x->found&4)return XPL_HW_FDT_INVALID;
        x->found|=4;
        s=identity_parent(d,n);if(s)return s;s=named_reg(d,n,"core",&c->spmi);if(s)return s;
        s=u32prop(d,n->offset,"qcom,pmic-arb-channel",&c->spmi_channel,1);if(s)return s;
        s=u32prop(d,n->offset,"qcom,pmic-arb-ee",&c->spmi_ee,1);if(s)return s;
    }
    if(has_compat(d,n->offset,"qcom,msm-watchdog")){
        if(x->found&32)return XPL_HW_FDT_INVALID;
        x->found|=32;
        s=identity_parent(d,n);if(s)return s;
        s=reg_at(d,n,0,&c->watchdog);if(s)return s;
        if(c->watchdog.base!=0xf9017000u||c->watchdog.bytes!=0x1000u)return XPL_HW_RESOURCE_UNSUPPORTED;
    }
    /* Reject inconsistent aliases on any node, including ignored nodes. */
    s=property(d,n->offset,"phandle",&p);if(s)return s;s=property(d,n->offset,"linux,phandle",&q);if(s)return s;
    if((p.exists&&p.n!=4)||(q.exists&&q.n!=4)||(p.exists&&q.exists&&be32(p.p)!=be32(q.p)))return XPL_HW_FDT_INVALID;
    return XPL_HW_OK;
}

static enum xpl_hw_status resolve_handles(const struct dt *d,const struct node *n,const struct node *parent,void *context)
{
    struct collect *x=context;struct prop p;uint32_t handle=0;enum xpl_hw_status s;
    (void)parent;s=property(d,n->offset,"phandle",&p);if(s)return s;
    if(!p.exists){s=property(d,n->offset,"linux,phandle",&p);if(s)return s;}
    if(p.exists)handle=be32(p.p);
    if(!handle)return XPL_HW_OK;
    if(handle==x->splash_handle){
        if(x->found&8)return XPL_HW_FDT_INVALID;
        x->found|=8;
        s=reg_at(d,n,0,&x->c->splash);if(s)return s;
    }
    if(handle==x->gdsc_handle){
        if(x->found&16)return XPL_HW_FDT_INVALID;
        x->found|=16;
        if(!has_compat(d,n->offset,"qcom,gdsc"))return XPL_HW_RESOURCE_UNSUPPORTED;
        s=identity_parent(d,n);if(s)return s;s=reg_at(d,n,0,&x->c->gdsc);if(s)return s;
    }
    return XPL_HW_OK;
}

enum xpl_hw_status xpl_hw_resolve_fdt(const void *dtb,size_t available,struct xpl_hw_config *out,struct xpl_hw_diag *diag)
{
    struct dt d;struct collect x;enum xpl_hw_status s;unsigned splash_reserved=0,splash_ram=0;
    hw_diag_init(diag,XPL_HW_PHASE_FDT);if(!out)return hw_fail(diag,XPL_HW_ARGUMENT,0);hw_zero(out,sizeof(*out));
    s=validate(dtb,available,&d);if(s)return hw_fail(diag,s,1);
    hw_zero(&x,sizeof(x));x.c=out;
    for(uint32_t p=d.ro;be64(d.p+p)||be64(d.p+p+8);p+=16){
        struct xpl_hw_range r={be64(d.p+p),be64(d.p+p+8)};
        s=add_range(out->reserved,&out->reserved_count,XPL_HW_MAX_RESERVED,r,0);if(s)return hw_fail(diag,s,2);
    }
    s=walk(&d,collect_node,&x);if(s)return hw_fail(diag,s,3);
    if((x.found&7)!=7||!x.splash_handle||!x.gdsc_handle)return hw_fail(diag,XPL_HW_RESOURCE_MISSING,x.found);
    s=walk(&d,resolve_handles,&x);if(s)return hw_fail(diag,s,4);
    if((x.found&31)!=31)return hw_fail(diag,XPL_HW_RESOURCE_MISSING,x.found);
    if(!out->ram_count)return hw_fail(diag,XPL_HW_NO_RAM,0);
    out->complete=1;
    if(!hw_resources_valid(out)){out->complete=0;return hw_fail(diag,XPL_HW_RESOURCE_UNSUPPORTED,5);}
    for(unsigned i=0;i<out->ram_count;i++)if(hw_contains(out->ram[i],out->splash.base,out->splash.bytes))splash_ram=1;
    for(unsigned i=0;i<out->reserved_count;i++)if(out->reserved[i].base==out->splash.base&&out->reserved[i].bytes==out->splash.bytes)splash_reserved=1;
    if(!splash_ram||!splash_reserved){out->complete=0;return hw_fail(diag,XPL_HW_SURFACE_RANGE,6);}
    return XPL_HW_OK;
}

const char *xpl_hw_status_string(enum xpl_hw_status s)
{
    switch(s){
    case XPL_HW_OK:return "OK";case XPL_HW_ARGUMENT:return "invalid HAL argument";
    case XPL_HW_FDT_INVALID:return "invalid live FDT";case XPL_HW_FDT_LIMIT:return "live FDT exceeds bounded limits";
    case XPL_HW_RESOURCE_MISSING:return "required live hardware resource absent";
    case XPL_HW_RESOURCE_UNSUPPORTED:return "hardware layout outside measured 8994 adapter";
    case XPL_HW_NO_RAM:return "live RAM map unavailable";case XPL_HW_MMIO_FAULT:return "guarded MMIO failed";
    case XPL_HW_DISPLAY_OFF:return "existing display power/clock/path inactive";
    case XPL_HW_DISPLAY_TOPOLOGY:return "display needs unsupported split/multiple/translated topology";
    case XPL_HW_DISPLAY_FORMAT:return "display is not a supported linear packed RGB surface";
    case XPL_HW_SURFACE_RANGE:return "scanout is outside permitted RAM/splash ranges";
    case XPL_HW_CHANGED:return "display configuration changed";case XPL_HW_SPMI_UNSUPPORTED:return "unsupported SPMI arbiter version";
    case XPL_HW_SPMI_TIMEOUT:return "SPMI read timed out";case XPL_HW_SPMI_ERROR:return "SPMI read denied/dropped/failed";
    case XPL_HW_KEYS_UNCONFIGURED:return "S1 did not leave usable input GPIO configuration";
    default:return "unknown HAL status";
    }
}
