/* SPDX-License-Identifier: MIT. Bounded E6653 first-install/update writer.
 * Only the original64MiB boot extent and its GPT mirrors can be written.
 * The host supplies a live CID and before bytes; the opened device must match.
 * Source files are staged in recovery RAM. No SD/system/userdata/S1/TA writes.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define SECTORS UINT64_C(61071360)
#define BOOT_FIRST UINT64_C(244224)
#define OS_FIRST UINT64_C(309760)
#define HALF_BYTES 33554432u
static uint32_t le32(const unsigned char *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t le64(const unsigned char *p){return le32(p)|((uint64_t)le32(p+4)<<32);}
static int nonzero(const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 1;return 0;}
static uint32_t crc(const unsigned char *p,size_t n){
    uint32_t c=UINT32_MAX;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^((c&1u)?0xedb88320u:0);}return ~c;
}
static int named(const unsigned char *p,const char *s){
    unsigned char want[72]={0};size_t n=strlen(s);if(n>35)return 0;
    for(size_t i=0;i<n;i++)want[i*2]=(unsigned char)s[i];return !memcmp(p,want,72);
}
static int valid_gpt(const unsigned char *b,int backup){
    const unsigned char *h=b+(backup?16384u:512u),*a=b+(backup?0u:1024u);unsigned char copy[92];
    if(memcmp(h,"EFI PART",8)||le32(h+8)!=0x10000u||le32(h+12)!=92u||le32(h+20)||
       le64(h+24)!=(backup?SECTORS-1:1)||le64(h+32)!=(backup?1:SECTORS-1)||
       le64(h+40)!=34||le64(h+48)!=SECTORS-34||!nonzero(h+56,16)||
       le64(h+72)!=(backup?SECTORS-33:2)||le32(h+80)!=128||le32(h+84)!=128||
       le32(h+88)!=crc(a,16384)||nonzero(h+92,420))return 0;
    memcpy(copy,h,92);memset(copy+16,0,4);return le32(h+16)==crc(copy,92);
}
static int split_pair(const unsigned char *before,const unsigned char *after,int backup){
    size_t n=(backup?33u:34u)*512u,at=backup?0u:1024u,head=backup?16384u:512u;
    if(!valid_gpt(before,backup)||!valid_gpt(after,backup))return 0;
    const unsigned char *part=before+at+28u*128u,*os=after+at+43u*128u;
    if(!named(part+56,"boot")||!nonzero(part,16)||!nonzero(part+16,16)||
       le64(part+32)!=BOOT_FIRST||le64(part+40)!=BOOT_FIRST+131072-1||
       nonzero(before+at+43u*128u,128)||!nonzero(os+16,16))return 0;
    for(unsigned i=0;i<128;i++)if(i!=43&&!memcmp(os+16,before+at+i*128u+16,16))return 0;
    unsigned char *expected=malloc(n);if(!expected)return 0;memcpy(expected,before,n);
    unsigned char *manager=expected+at+28u*128u,*created=expected+at+43u*128u,*h=expected+head;
    memset(manager+56,0,72);const char *name="xploader";for(unsigned i=0;name[i];i++)manager[56+i*2]=(unsigned char)name[i];
    uint64_t last=OS_FIRST-1;for(unsigned i=0;i<8;i++)manager[40+i]=(unsigned char)(last>>(8*i));
    memset(created,0,128);memcpy(created,part,16);memcpy(created+16,os+16,16);
    last=BOOT_FIRST+131072-1;for(unsigned i=0;i<8;i++){created[32+i]=(unsigned char)(OS_FIRST>>(8*i));created[40+i]=(unsigned char)(last>>(8*i));}
    name="boot";for(unsigned i=0;name[i];i++)created[56+i*2]=(unsigned char)name[i];
    uint32_t c=crc(expected+at,16384);for(unsigned i=0;i<4;i++)h[88+i]=(unsigned char)(c>>(8*i));
    memset(h+16,0,4);c=crc(h,92);for(unsigned i=0;i<4;i++)h[16+i]=(unsigned char)(c>>(8*i));
    int ok=!memcmp(expected,after,n);free(expected);return ok;
}
static int text_equal(const char *path,const char *want){
    char b[160]={0};int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0)return 0;
    ssize_t n=read(f,b,sizeof b-1);close(f);if(n<=0)return 0;
    while(n>0&&(b[n-1]=='\n'||b[n-1]=='\r'))b[--n]=0;return !strcmp(b,want);
}
static int cid_ok(const char *cid){return strlen(cid)==32&&strspn(cid,"0123456789abcdef")==32&&
    text_equal("/sys/class/block/mmcblk0/device/cid",cid);}
static int open_device(const char *cid,int writing){
    if(getuid()!=0){fputs("Root recovery is required\n",stderr);return -1;}
    if(!cid_ok(cid)){fputs("Live eMMC identity differs\n",stderr);return -1;}
    if(!text_equal("/sys/class/block/mmcblk0/size","61071360")){fputs("Unsupported eMMC geometry\n",stderr);return -1;}
    /* The accepted3.10 recovery exposes /proc/device-tree, without the newer
     * /sys/firmware/devicetree/base alias. Both carry a terminating NUL. */
    if(!text_equal("/proc/device-tree/model","SoMC Sumire-ROW")&&
       !text_equal("/sys/firmware/devicetree/base/model","SoMC Sumire-ROW")){
        fputs("Unsupported device-tree board model\n",stderr);return -1;
    }
    int f=open("/dev/block/mmcblk0",(writing?O_RDWR:O_RDONLY)|O_DIRECT|O_CLOEXEC|O_NOFOLLOW);
    struct stat st;uint64_t size=0;
    if(f<0)return -1;
    if(fstat(f,&st)||!S_ISBLK(st.st_mode)||major(st.st_rdev)!=179||minor(st.st_rdev)!=0||
       ioctl(f,BLKGETSIZE64,&size)||size!=SECTORS*512u){close(f);return -1;}
    return f;
}
static int load(const char *path,void **out,size_t *size){
    int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;if(f<0)return 0;
    if(fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<512||st.st_size>HALF_BYTES||st.st_size%512){close(f);return 0;}
    *size=(size_t)st.st_size;if(posix_memalign(out,4096,*size)){close(f);return 0;}
    int ok=pread(f,*out,*size,0)==(ssize_t)*size;close(f);return ok;
}
static int android_boot(const unsigned char *b,size_t n,int manager){
    if(n<4096||n%4096||memcmp(b,"ANDROID!",8)||le32(b+36)!=4096||le32(b+40)||le32(b+24)||
       !le32(b+8)||!le32(b+16))return 0;
    uint64_t end=4096+(((uint64_t)le32(b+8)+4095)&~UINT64_C(4095))+(((uint64_t)le32(b+16)+4095)&~UINT64_C(4095));
    return end==n&&(!manager||!memcmp(b+48,"xploader",8));
}
static int current_layout(int f,int need_split){
    unsigned char p[34u*512u] __attribute__((aligned(4096)));
    unsigned char b[33u*512u] __attribute__((aligned(4096)));
    if(pread(f,p,sizeof p,0)!=(ssize_t)sizeof p||pread(f,b,sizeof b,(off_t)((SECTORS-33)*512))!=(ssize_t)sizeof b||
       !valid_gpt(p,0)||!valid_gpt(b,1)||memcmp(p+1024,b,16384))return 0;
    const unsigned char *manager=p+1024+28u*128u,*os=p+1024+43u*128u;
    if(le64(manager+32)!=BOOT_FIRST)return 0;
    if(named(manager+56,"boot")&&le64(manager+40)==BOOT_FIRST+131072-1&&!nonzero(os,128))return !need_split;
    return named(manager+56,"xploader")&&le64(manager+40)==OS_FIRST-1&&named(os+56,"boot")&&
        le64(os+32)==OS_FIRST&&le64(os+40)==BOOT_FIRST+131072-1&&!memcmp(manager,os,16)&&!le64(os+48);
}
int main(int argc,char **argv){
    if(argc==3&&!strcmp(argv[1],"--check-device")){
        int f=open_device(argv[2],0);if(f<0)return 1;
        int ok=current_layout(f,0);close(f);if(!ok)return 1;
        puts("{\"accepted\":true,\"read_only\":true,\"supported_E6653_layout\":true}");return 0;
    }
    int validate=argc==5&&!strcmp(argv[1],"--validate");
    int execute=argc==6&&!strcmp(argv[1],"--write");
    if(!validate&&!execute)return 2;
    const char *kind=argv[2],*before_path=argv[validate?3:4],*after_path=argv[validate?4:5];
    int primary=!strcmp(kind,"primary"),backup=!strcmp(kind,"backup"),manager=!strcmp(kind,"manager"),os=!strcmp(kind,"os");
    if(!primary&&!backup&&!manager&&!os)return 2;
    if(execute&&(strncmp(before_path,"/tmp/xploader-install-",22)||strncmp(after_path,"/tmp/xploader-install-",22)))return 2;
    void *before=0,*after=0,*live=0;size_t bn=0,an=0;int f=-1,rc=1;
    uint64_t offset=primary?0:backup?(SECTORS-33)*512u:manager?BOOT_FIRST*512u:OS_FIRST*512u;
    if(!load(before_path,&before,&bn)||!load(after_path,&after,&an)||bn!=an||posix_memalign(&live,4096,an))goto done;
    if(primary||backup){if(an!=(backup?33u:34u)*512u||!split_pair(before,after,backup))goto done;}
    else if(!android_boot(after,an,manager))goto done;
    if(validate){puts("{\"accepted\":true,\"host_validation_only\":true}");rc=0;goto done;}
    f=open_device(argv[3],1);if(f<0||pread(f,live,an,(off_t)offset)!=(ssize_t)an)goto done;
    if(!memcmp(live,after,an)){
        puts("{\"accepted\":true,\"already_equal\":true,\"write_bytes\":0,\"exact_readback\":true}");rc=0;goto done;
    }
    if(memcmp(live,before,an)||((manager||os)&&!current_layout(f,manager)))goto done;
    if(pwrite(f,after,an,(off_t)offset)!=(ssize_t)an||fsync(f)||pread(f,live,an,(off_t)offset)!=(ssize_t)an||memcmp(live,after,an))goto done;
    printf("{\"accepted\":true,\"first_sector\":%" PRIu64 ",\"write_bytes\":%zu,\"fsync_result\":0,\"exact_readback\":true}\n",offset/512u,an);rc=0;
done:
    if(f>=0)close(f);free(before);free(after);free(live);
    if(rc)fputs("Installer guard refused or write result is uncertain\n",stderr);return rc;
}
