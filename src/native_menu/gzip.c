/* SPDX-License-Identifier: MIT */
#include "gzip.h"
#include "../third_party/miniz-3.0.2/miniz.h"
static uint32_t u32(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t crc(const uint8_t *p,size_t bytes,int (*progress)(void *),void *ctx,unsigned *stopped) {
    uint32_t table[256],sum=UINT32_MAX;
    for(unsigned i=0;i<256;i++) {uint32_t v=i;for(unsigned n=0;n<8;n++)v=(v>>1)^((v&1u)?0xedb88320u:0);table[i]=v;}
    for(size_t i=0;i<bytes;i++) {
        sum=(sum>>8)^table[(sum^p[i])&255u];
        if(!(i&65535u)&&progress&&progress(ctx)) {*stopped=1;return 0;}
    }
    return sum^UINT32_MAX;
}
int xpl_gzip_decode(const void *source,size_t bytes,void *output,size_t capacity,
                     size_t *written,size_t *member,int (*progress)(void *),void *context) {
    const uint8_t *p=source;uint8_t *out=output;size_t at=10,total=0;unsigned stopped=0;
    if(written)*written=0;if(member)*member=0;
    if(!p||!out||!capacity||!written||!member||bytes<18)return XPL_GZIP_ARGUMENT;
    if((uintptr_t)out>(uintptr_t)p?((uintptr_t)out-(uintptr_t)p<bytes):((uintptr_t)p-(uintptr_t)out<capacity))return XPL_GZIP_ARGUMENT;
    if(p[0]!=31u||p[1]!=139u||p[2]!=8u||(p[3]&0xe0u))return XPL_GZIP_FORMAT;
    if(p[3]&4u) {
        if(bytes-at<2)return XPL_GZIP_FORMAT;
        size_t extra=p[at]|((size_t)p[at+1]<<8);at+=2;if(extra>bytes-at)return XPL_GZIP_FORMAT;at+=extra;
    }
    for(unsigned bit=8;bit<=16;bit*=2)if(p[3]&bit) {
        while(at<bytes&&at<65536u&&p[at])at++;
        if(at>=bytes||at>=65536u)return XPL_GZIP_FORMAT;at++;
    }
    if(p[3]&2u) {
        if(bytes-at<2)return XPL_GZIP_FORMAT;
        if((crc(p,at,progress,context,&stopped)&65535u)!=(uint32_t)(p[at]|((uint32_t)p[at+1]<<8)))return XPL_GZIP_CORRUPT;
        if(stopped)return XPL_GZIP_STOPPED;at+=2;
    }
    tinfl_decompressor state;tinfl_init(&state);
    for(;;) {
        size_t available=bytes-at,room=capacity-total;if(room>262144u)room=262144u;
        if(!room)return XPL_GZIP_CAPACITY;
        tinfl_status status=tinfl_decompress(&state,p+at,&available,out,out+total,&room,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        at+=available;total+=room;
        if(progress&&progress(context))return XPL_GZIP_STOPPED;
        if(status==TINFL_STATUS_DONE)break;
        if(status!=TINFL_STATUS_HAS_MORE_OUTPUT||(!available&&!room))return XPL_GZIP_CORRUPT;
    }
    if(bytes-at<8||total>UINT32_MAX||u32(p+at+4)!=(uint32_t)total)return XPL_GZIP_CORRUPT;
    uint32_t sum=crc(out,total,progress,context,&stopped);
    if(stopped)return XPL_GZIP_STOPPED;
    if(sum!=u32(p+at))return XPL_GZIP_CORRUPT;
    *written=total;*member=at+8;return 0;
}
