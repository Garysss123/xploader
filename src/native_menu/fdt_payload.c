/* SPDX-License-Identifier: MIT */
#include "fdt_payload.h"
#include "bootargs.h"
#define ARGS_MAX 2048u
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint64_t be64(const uint8_t *p) {return ((uint64_t)be32(p)<<32)|be32(p+4);}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static void putcells(uint8_t *p,unsigned n,uint64_t v) {
    if(n==8) {put32(p,(uint32_t)(v>>32));p+=4;}
    put32(p,(uint32_t)v);
}
static int equal(const uint8_t *p,const char *s) {
    while(*s)if(*p++!=(uint8_t)*s++)return 0;
    return !*p;
}
static int whitespace(uint8_t c) {return c==' '||c=='\t'||c=='\r'||c=='\n';}
static size_t text_bytes(const uint8_t *p,size_t n) {
    size_t i=0;while(i<n&&p[i])i++;return i;
}
static int next(const uint8_t *p,size_t n,size_t *pos,size_t *first,size_t *last) {
    while(*pos<n&&whitespace(p[*pos]))(*pos)++;
    *first=*pos;
    while(*pos<n&&!whitespace(p[*pos]))(*pos)++;
    *last=*pos;return *last>*first;
}
static size_t key_bytes(const uint8_t *p,size_t n) {
    size_t i=0;while(i<n&&p[i]!='=')i++;return i;
}
static int same_key(const uint8_t *a,size_t an,const uint8_t *b,size_t bn) {
    an=key_bytes(a,an);bn=key_bytes(b,bn);if(an!=bn)return 0;
    for(size_t i=0;i<an;i++)if(a[i]!=b[i])return 0;return 1;
}
static int has_key(const uint8_t *p,size_t n,const uint8_t *key,size_t kn) {
    size_t pos=0,a,b;
    while(next(p,n,&pos,&a,&b))if(same_key(p+a,b-a,key,kn))return 1;
    return 0;
}
static int append(uint8_t *out,size_t *used,const uint8_t *p,size_t n) {
    if(!n||n>=ARGS_MAX||*used>=ARGS_MAX||n+(*used?1u:0u)>=ARGS_MAX-*used)return 0;
    if(*used)out[(*used)++]=' ';
    for(size_t i=0;i<n;i++) {
        if(p[i]<32u||p[i]>126u)return 0;
        out[(*used)++]=p[i];
    }
    out[*used]=0;return 1;
}
int xpl_fdt_payload_prepare(const void *source,size_t source_bytes,
    void *clone,size_t capacity,const char *header_args,size_t header_bytes,
    const char *mode,uint64_t initrd_start,uint64_t initrd_bytes,
    xpl_fdt_payload_diag *diag) {
    const uint8_t *src=source;uint8_t *p=clone;
    if(!src||!p||!diag||!header_args||!mode||source_bytes<40||capacity<source_bytes||
       capacity>2097152u||header_bytes>ARGS_MAX||!initrd_bytes||initrd_start>UINT64_MAX-initrd_bytes)
        return XPL_FDT_PAYLOAD_ARGUMENT;
    *diag=(xpl_fdt_payload_diag){0};
    uintptr_t a=(uintptr_t)src,b=(uintptr_t)p;
    if(a>UINTPTR_MAX-source_bytes||b>UINTPTR_MAX-capacity||
       (a<b+capacity&&b<a+source_bytes))return XPL_FDT_PAYLOAD_ARGUMENT;
    uint32_t total=be32(src+4),so=be32(src+8),st=be32(src+12),rs=be32(src+16),
        sn=be32(src+32),ss=be32(src+36);
    if(be32(src)!=0xd00dfeedu||total>source_bytes||total<40||be32(src+20)!=17||be32(src+24)>17||
       so<40||(so&3)||so>total||ss>total-so||st<so+ss||st>total||sn>total-st||
       rs<40||(rs&7)||rs>so||so-rs<16)return XPL_FDT_PAYLOAD_TREE;
    unsigned terminated=0;
    for(uint32_t off=rs;so-off>=16;off+=16) {
        uint64_t address=be64(src+off),bytes=be64(src+off+8);
        if(!address&&!bytes) {terminated=1;break;}
        if(address>UINT64_MAX-bytes)return XPL_FDT_PAYLOAD_TREE;
    }
    if(!terminated)return XPL_FDT_PAYLOAD_TREE;
    for(uint32_t i=0;i<total;i++)p[i]=src[i];
    xpl_bootargs_stats stats;
    if(xpl_bootargs_clean_fdt(p,total,&stats))return XPL_FDT_PAYLOAD_TREE;
    if(stats.chosen_nodes!=1||stats.properties!=1)return XPL_FDT_PAYLOAD_CHOSEN;
    uint32_t pos=so,end=so+ss,arg=0,arg_len=0,start=0,start_len=0,finish=0,finish_len=0;
    unsigned depth=0,chosen=0,starts=0,finishes=0;
    while(end-pos>=4) {
        uint32_t token=be32(p+pos);pos+=4;
        if(token==1) {
            uint32_t first=pos;while(pos<end&&p[pos])pos++;
            if(depth==1&&(equal(p+first,"chosen")||equal(p+first,"chosen@0")))chosen=2;
            depth++;pos=(pos+4)&~3u;
        } else if(token==2) {if(depth==chosen)chosen=0;depth--;}
        else if(token==3) {
            uint32_t n=be32(p+pos),name=be32(p+pos+4);pos+=8;
            if(depth==chosen) {
                if(equal(p+st+name,"bootargs")) {arg=pos;arg_len=n;}
                else if(equal(p+st+name,"linux,initrd-start")) {start=pos;start_len=n;starts++;}
                else if(equal(p+st+name,"linux,initrd-end")) {finish=pos;finish_len=n;finishes++;}
            }
            pos=(pos+n+3)&~3u;
        } else if(token==9)break;
    }
    if(!arg||starts!=1||finishes!=1||start_len!=finish_len||
       (start_len!=4&&start_len!=8)||(start_len==4&&initrd_start+initrd_bytes>UINT32_MAX))
        return XPL_FDT_PAYLOAD_CHOSEN;
    if(!arg_len||arg_len>ARGS_MAX)return XPL_FDT_PAYLOAD_ARGS;
    const uint8_t *hdr=(const uint8_t *)header_args,*mode_args=(const uint8_t *)mode;
    size_t hn=text_bytes(hdr,header_bytes),mn=text_bytes(mode_args,64u);
    if(hn==header_bytes||!mn||mn==64u)return XPL_FDT_PAYLOAD_ARGS;
    uint8_t merged[ARGS_MAX]={0};size_t used=0,scan=0,first,last;
    size_t old=text_bytes(p+arg,arg_len);
    while(next(p+arg,old,&scan,&first,&last)) {
        const uint8_t *word=p+arg+first;size_t n=last-first;
        if(has_key(hdr,hn,word,n)||has_key(mode_args,mn,word,n))continue;
        if(!append(merged,&used,word,n))return XPL_FDT_PAYLOAD_ARGS;
    }
    scan=0;
    while(next(hdr,hn,&scan,&first,&last)) {
        if(has_key(mode_args,mn,hdr+first,last-first)||has_key(hdr+last,hn-last,hdr+first,last-first))continue;
        if(!append(merged,&used,hdr+first,last-first))return XPL_FDT_PAYLOAD_ARGS;
    }
    scan=0;
    while(next(mode_args,mn,&scan,&first,&last))if(!append(merged,&used,mode_args+first,last-first))return XPL_FDT_PAYLOAD_ARGS;
    uint32_t old_aligned=(arg_len+3u)&~3u,new_len=(uint32_t)used+1u,new_aligned=(new_len+3u)&~3u;
    uint32_t extra=new_aligned>old_aligned?new_aligned-old_aligned:0u,cut=arg+old_aligned;
    if(extra>capacity-total)return XPL_FDT_PAYLOAD_CAPACITY;
    for(uint32_t i=total;i>cut;) {i--;p[i+extra]=p[i];}
    put32(p+arg-8,new_len);
    for(uint32_t i=0;i<old_aligned+extra;i++)p[arg+i]=i<new_len?merged[i]:0;
    /* A smaller property needs NOP tokens rather than anonymous structure bytes. */
    for(uint32_t i=new_aligned;i<old_aligned+extra;i+=4)put32(p+arg+i,4);
    if(start>=cut)start+=extra;if(finish>=cut)finish+=extra;
    putcells(p+start,start_len,initrd_start);putcells(p+finish,finish_len,initrd_start+initrd_bytes);
    total+=extra;st+=extra;ss+=extra;
    put32(p+4,total);put32(p+12,st);put32(p+36,ss);
    if(xpl_bootargs_clean_fdt(p,total,&stats))return XPL_FDT_PAYLOAD_TREE;
    *diag=(xpl_fdt_payload_diag){total,arg_len,new_len,start_len};
    return XPL_FDT_PAYLOAD_OK;
}

static uint8_t *property(uint8_t *p,const char *wanted_path,const char *name,uint32_t *bytes) {
    uint32_t pos=be32(p+8),end=pos+be32(p+36),st=be32(p+12);
    char path[512]={0};size_t used=0,levels[64];unsigned depth=0;
    while(end-pos>=4) {
        uint32_t token=be32(p+pos);pos+=4;
        if(token==1) {
            size_t first=pos;while(pos<end&&p[pos])pos++;
            size_t len=pos-first;levels[depth]=used;
            if(len+(depth?1u:0u)>sizeof path-used-1u)return 0;
            if(depth)path[used++]='/';
            for(size_t i=0;i<len;i++)path[used++]=(char)p[first+i];path[used]=0;
            depth++;pos=(pos+4)&~3u;
        }else if(token==2){used=levels[--depth];path[used]=0;}
        else if(token==3) {
            uint32_t n=be32(p+pos),key=be32(p+pos+4);pos+=8;
            if(equal((const uint8_t *)path,wanted_path)&&equal(p+st+key,name)){*bytes=n;return p+pos;}
            pos=(pos+n+3)&~3u;
        }else if(token==9)break;
    }
    return 0;
}
static int tuple_match(const uint8_t *a,uint32_t an,const uint8_t *b,uint32_t bn) {
    if(an!=8||!bn||bn%8)return 0;
    for(uint32_t offset=0;offset<bn;offset+=8) {
        unsigned yes=1;for(unsigned i=0;i<8;i++)if(a[i]!=b[offset+i])yes=0;
        if(yes)return 1;
    }
    return 0;
}
int xpl_fdt_payload_system_overlay(void *clone,size_t clone_bytes,
    void *bundle,size_t bundle_bytes,unsigned *selected_index) {
    uint8_t *p=clone,*data=bundle;xpl_bootargs_stats stats;
    if(!p||!data||!selected_index||!bundle_bytes||bundle_bytes>2097152u||
       xpl_bootargs_clean_fdt(p,clone_bytes,&stats))return XPL_FDT_PAYLOAD_TREE;
    uint32_t mn=0,bn=0;uint8_t *msm=property(p,"","qcom,msm-id",&mn),*board=property(p,"","qcom,board-id",&bn);
    if(!msm||!board||mn!=8||bn!=8)return XPL_FDT_PAYLOAD_TREE;
    size_t offset=0;unsigned index=0,matches=0;uint8_t *selected=0;
    while(offset<bundle_bytes) {
        if(bundle_bytes-offset<40||index>=16)return XPL_FDT_PAYLOAD_TREE;
        uint8_t *tree=data+offset;uint32_t n=be32(tree+4);
        if(n<40||n>bundle_bytes-offset||xpl_bootargs_clean_fdt(tree,n,&stats))return XPL_FDT_PAYLOAD_TREE;
        uint32_t sm=0,sb=0;uint8_t *mt=property(tree,"","qcom,msm-id",&sm),*bt=property(tree,"","qcom,board-id",&sb);
        if(mt&&bt&&tuple_match(msm,mn,mt,sm)&&tuple_match(board,bn,bt,sb)) {
            matches++;selected=tree;*selected_index=index;
        }
        offset+=n;index++;
    }
    if(matches!=1)return XPL_FDT_PAYLOAD_CHOSEN;
    const char *path="/firmware/android/fstab/system";
    static const char *const names[]={"compatible","dev","type","mnt_flags","fsmgr_flags","status"};
    for(unsigned i=0;i<6;i++) {
        uint32_t dn=0,sn=0;uint8_t *dest=property(p,path,names[i],&dn),*src=property(selected,path,names[i],&sn);
        if(!dest||!src||!sn||sn>dn||sn>512||src[sn-1])return XPL_FDT_PAYLOAD_CAPACITY;
        uint32_t aligned=(dn+3u)&~3u,next=(sn+3u)&~3u;
        put32(dest-8,sn);
        for(uint32_t j=0;j<next;j++)dest[j]=j<sn?src[j]:0;
        for(uint32_t j=next;j<aligned;j+=4)put32(dest+j,4);
    }
    return xpl_bootargs_clean_fdt(p,clone_bytes,&stats)?XPL_FDT_PAYLOAD_TREE:XPL_FDT_PAYLOAD_OK;
}

int xpl_fdt_payload_directory(void *clone,size_t capacity,const char *path,size_t *total_bytes) {
    uint8_t *p=clone;xpl_bootargs_stats stats;
    if(!p||!path||!total_bytes||capacity>2097152u||xpl_bootargs_clean_fdt(p,capacity,&stats)||stats.chosen_nodes!=1)
        return XPL_FDT_PAYLOAD_ARGUMENT;
    size_t length=0,first=path[0]=='/'?1u:0u;
    while(length<=240u&&path[length]) {
        unsigned c=(uint8_t)path[length];if(c<32u||c==127u||c=='\\'||c==':')return XPL_FDT_PAYLOAD_ARGS;
        length++;
    }
    if(!length||length>240u)return XPL_FDT_PAYLOAD_ARGS;
    if(length>1u)for(size_t i=first;i<=length;i++)if(i==length||path[i]=='/') {
        if(i==first||(i-first==1u&&path[first]=='.')||(i-first==2u&&path[first]=='.'&&path[first+1]=='.'))
            return XPL_FDT_PAYLOAD_ARGS;
        first=i+1u;
    }
    uint32_t old_total=be32(p+4),so=be32(p+8),st=be32(p+12),sn=be32(p+32),ss=be32(p+36),pos=so,end=so+ss,insert=0;
    unsigned depth=0,chosen=0;
    while(end-pos>=4) {
        uint32_t token=be32(p+pos),at=pos;pos+=4;
        if(token==1) {
            uint32_t name=pos;while(pos<end&&p[pos])pos++;
            if(depth==1&&(equal(p+name,"chosen")||equal(p+name,"chosen@0")))chosen=2;
            depth++;pos=(pos+4)&~3u;
        }else if(token==2) {if(depth==chosen){insert=at;break;}depth--;}
        else if(token==3){uint32_t n=be32(p+pos);pos+=8;pos=(pos+n+3)&~3u;}
        else if(token==9)break;
    }
    static const char name[]="xploader,sd-directory";
    uint32_t unused=0;
    if(!insert||st<so+ss||property(p,"/chosen",name,&unused)||property(p,"/chosen@0",name,&unused))
        return XPL_FDT_PAYLOAD_CHOSEN;
    uint32_t bytes=(uint32_t)length+1u,aligned=(bytes+3u)&~3u,added=12u+aligned;
    size_t total=(size_t)old_total+added+sizeof name;total=(total+7u)&~(size_t)7u;
    if(total>capacity)return XPL_FDT_PAYLOAD_CAPACITY;
    for(uint32_t i=old_total;i>insert;) {i--;p[i+added]=p[i];}
    put32(p+insert,3);put32(p+insert+4,bytes);put32(p+insert+8,sn);
    for(uint32_t i=0;i<aligned;i++)p[insert+12u+i]=i<bytes?(uint8_t)path[i]:0;
    st+=added;uint32_t string_end=st+sn,new_total=old_total+added;
    for(uint32_t i=new_total;i>string_end;) {i--;p[i+sizeof name]=p[i];}
    for(unsigned i=0;i<sizeof name;i++)p[string_end+i]=(uint8_t)name[i];
    for(size_t i=(size_t)new_total+sizeof name;i<total;i++)p[i]=0;
    put32(p+4,(uint32_t)total);put32(p+12,st);put32(p+32,sn+(uint32_t)sizeof name);put32(p+36,ss+added);
    if(xpl_bootargs_clean_fdt(p,total,&stats))return XPL_FDT_PAYLOAD_TREE;
    *total_bytes=total;return 0;
}
