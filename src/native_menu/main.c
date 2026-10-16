/* SPDX-License-Identifier: MIT
 * Inherited S1 surface/keys, explicit eMMC continuation, bounded SD payload
 * staging and sttext handoff. Kernel-level recovery write guards are pending.
 */
#include "entry.h"
#include "bootargs.h"
#include "dsi_window.h"
#include "dsi_frame.h"
#include "gzip.h"
#include "fdt_payload.h"
#include "instruction_cache.h"
#include "service_probe.h"
#include "service_reset.h"
#include "service_poweroff.h"
#include "../menu/xpl_locale.h"
#include "usb_power_probe.h"
#include "usb_guard.h"
#include "usb_enum.h"
#include "settings.h"
#include "../payload/xpl_payload.h"
#include "native_menu_config.h"
#ifndef XPL_LAB_SERVICE_KIND
#define XPL_LAB_SERVICE_KIND 0
#define XPL_LAB_SERVICE_SEQUENCE 0
#endif
#include "../native_hw/xpl_hw.h"
#include "../menu/xpl_menu.h"
#include "../sdhci/xpl_sd_board.h"
#include "../sdhci/xpl_sd_probe.h"
#include "../sdhci/xpl_sd_core_probe.h"
#include "../sdhci/xpl_sd_mode_probe.h"
#include "../sdhci/xpl_sd_powered.h"
#include "../sdhci/xpl_sd_fast.h"
#include "../sdhci/xpl_emmc_probe.h"
#include "../sdhci/xpl_emmc_read.h"
#include "../sdhci/xpl_emmc_image.h"
#include "../storage/xpl_exfat.h"
void *memcpy(void *,const void *,size_t);

static const uint8_t package_id[32]=XPL_PACKAGE_ID_BYTES;
static struct xpl_hw_config hardware;
static struct xpl_hw_diag diag;
static struct xpl_hw_diag display_diag, key_diag;
static unsigned display_result, key_result;
static struct xpl_hw_display_snapshot display;
static struct xpl_hw_surface surface;
static struct xpl_hw_keys keys;
static xpl_menu menu;
static xpl_menu_render_cache render_cache;
static uint64_t kernel_base, fdt_pa, timer_frequency, start_ticks;
static uint64_t watchdog_last_pet;
static uint32_t watchdog_enabled, watchdog_bark, watchdog_bite;
static unsigned stage, result, key_bits, transitions, boot_requested;
static unsigned presented_frames;
static unsigned full_frames, partial_frames;
static uint64_t draw_last_us, draw_max_us, draw_full_max_us, draw_partial_max_us;
static uint64_t present_max_us, key_poll_max_us, input_gap_max_us;
static uint64_t boot_accept_us, boot_page_us, boot_go_us, handoff_us;
static uint64_t countdown_us[3];
static unsigned countdown_completed, bootargs_result, bootargs_removed;
static size_t fdt_bytes;
static xpl_bootargs_stats bootarg_stats;
static uint64_t entry_sctlr,entry_el;
static uint32_t smmu_sctlr,smmu_ttbr_lo,smmu_ttbr_hi,smmu_ttbcr;
static unsigned smmu_result;
static unsigned boot_progress_frames;
static xpl_service_snapshot service_snapshot;
static int service_probe_result=-1;
static xpl_service_reset_diag service_reset_diag;
static int service_reset_result=-1;
static unsigned service_requested,service_requests;
static xpl_usb_power_diag usb_power_diag;
static int usb_power_result=-1;
static xpl_usb_guard usb_guard_diag;
static int usb_guard_result=-1;
static unsigned usb_guard_requested;
static xpl_usb_enum_diag usb_enum_diag;
static int usb_enum_result=-1;
static unsigned msd_requested,msd_finished,msd_writable_active;
static unsigned pcsd_requested,pcsd_requests;
static unsigned pcsd_remount_requested;
static int pcsd_remount_result=-1;
static uint64_t pcsd_remount_bytes,pcsd_remount_fingerprint;
static int msd_result=-1;
static uint64_t msd_sectors;
static uint32_t msd_cid[4];
static xpl_usb_msd msd_device;
static xpl_settings boot_settings,pending_settings;
static uint8_t settings_records[1024],settings_verify[512];
static uint64_t settings_lba[2];
static unsigned settings_operation,settings_requested,settings_io_active,settings_write_slot,settings_slot;
static unsigned settings_saves,settings_start_keys;
static int settings_load_result=-1,settings_save_result=-1;
static void settings_file_io(void);
static void settings_request(void *,unsigned,unsigned,const char *);
static int run_msd_session(void);
static xpl_sd_board_snapshot sd_board;
static xpl_sd_ldo_state sd_l13, sd_l21;
static int sd_board_result = XPL_SD_EPREP, sd_pmic_result = XPL_SD_EPREP;
static unsigned sd_reads, sd_read_commands;
static xpl_sd_interface_snapshot sd_interface;
static int sd_interface_result=XPL_SD_EPREP;
static unsigned sd_interface_writes,proof_overflow;
static xpl_sd_core_snapshot sd_core;
static int sd_core_result=XPL_SD_EPREP;
static xpl_sd_mode_snapshot sd_power;
static int sd_power_result=XPL_SD_EPREP;
static xpl_sd_powered_diag sd_legacy;
static xpl_sd_host sd_host;
static uint8_t sd_sector[512];
static int sd_media_result=XPL_SD_EPREP,sd_init_result=XPL_SD_EPREP,sd_lba_result=XPL_SD_EPREP;
static uint64_t sd_lba_fingerprint,sd_media_us;
static uint64_t sd_read_phase_us,sd_decode_us,sd_input_hash_us,sd_output_hash_us;
static xpl_exfat sd_fs;
static int sd_fs_result=XPL_EXFAT_EIO,sd_file_result=XPL_EXFAT_ENOTFOUND,sd_twrp_result=XPL_EXFAT_ENOTFOUND;
static uint64_t sd_file_bytes,sd_file_fingerprint,sd_twrp_bytes,sd_twrp_header_fingerprint;
static uint32_t sd_partition_start,sd_partition_sectors;
static xpl_sd_fast_diag sd_fast;
static int sd_fast_result=XPL_SD_EPREP,sd_ram_result=XPL_EXFAT_EIO;
static uint64_t sd_ram_fingerprint,sd_ram_bytes;
static int sd_inflate_result=XPL_GZIP_ARGUMENT;
static uint64_t sd_raw_bytes,sd_raw_fingerprint,sd_gzip_member_bytes;
static uint64_t sd_probe_us;
static unsigned dsi_result;
static uint32_t dsi_ctrl,dsi_mdp,dsi_clock,dsi_before_stream,dsi_before_total;
static uint32_t dsi_boot_stream,dsi_boot_total;
static xpl_dsi_window_diag roi_diag;
static unsigned roi_result=XPL_DSI_WINDOW_UNSUPPORTED,lab_auto_triggered,lab_sd_fallback;
static xpl_dsi_frame_diag frame_diag;
static unsigned frame_result;
static volatile uint8_t *proof;
static struct xpl_hw_range initrd;
static unsigned kernel_replaced;
static int twrp_prepare_result=XPL_FDT_PAYLOAD_ARGUMENT;
static struct xpl_android_v0_layout twrp_layout;
static xpl_fdt_payload_diag twrp_fdt;
static enum xpl_menu_action load_target=XPL_MENU_TWRP;
static struct xpl_payload_descriptor sd_payload;
static uint64_t payload_kernel_source,payload_kernel_bytes,payload_initrd_source,payload_initrd_bytes;
static uint64_t payload_dtb_source,payload_dtb_bytes;
static unsigned payload_dtb_selected,system_overlay;
static char payload_cmdline[XPL_PAYLOAD_CMDLINE_BYTES];
static xpl_menu_folder_entry sd_folders[XPL_MENU_FOLDER_MAX];
static char sd_browse_path[256]="/";
static unsigned sd_picker_request,sd_picker_operation,sd_folder_count;
static int sd_picker_result=XPL_EXFAT_EIO;
static unsigned sd_owned_power;
static unsigned tool_requested,tool_metadata_active,tool_line_count,power_requested;
static enum xpl_menu_tool tool_selected;
static int tool_result,power_result;
static char tool_lines[12][96];
static xpl_poweroff_diag power_diag;
static xpl_service_reset_diag restart_diag;
static unsigned lab_service_active,lab_service_phase,lab_service_result=UINT32_MAX,lab_service_flags;
static uint64_t lab_service_entry_sequence,lab_service_start_us,lab_service_payload_bytes,lab_service_sd_blocks;
static char lab_service_original_locale[16];
static xpl_icache_diag icache_diag;
static xpl_emmc_snapshot emmc;
static int emmc_result=XPL_SD_EPREP;
static xpl_emmc_read_diag emmc_read_diag;
static int emmc_media_result=XPL_SD_EPREP;
static uint64_t emmc_first_fingerprint,emmc_boot_fingerprint,emmc_tail_fingerprint;
static xpl_emmc_image_diag emmc_image_diag;
static xpl_icache_diag emmc_icache;
static int emmc_image_result=XPL_SD_EPREP,emmc_decode_result=XPL_GZIP_ARGUMENT,emmc_fast_result=XPL_SD_EPREP;
static uint64_t emmc_raw_bytes,emmc_raw_fingerprint;
static void folder_open(void *unused);
static void folder_select(void *unused,unsigned selected);
static void system_request(enum xpl_menu_action selected);
static int prepare_twrp(void);
static void commit_twrp(void);
static int now_us(uint64_t *out);
static int free_ram(uint64_t base,uint64_t bytes);
static int inflate_progress(void *unused);

static uint64_t pack_ldo(const xpl_sd_ldo_state *s) {
    return (uint64_t)s->type|((uint64_t)s->subtype<<8)|((uint64_t)s->range<<16)|
        ((uint64_t)s->selector<<24)|((uint64_t)s->mode<<32)|
        ((uint64_t)s->enable<<40)|((uint64_t)s->pmic_subtype<<48);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static int same(const uint8_t *p,size_t n,const char *s) {
    for(size_t i=0;i<n;i++) {if(p[i]!=(uint8_t)s[i])return 0;if(!s[i])return 1;}return 0;
}
static uint64_t cells(const uint8_t *p,uint32_t n) {
    if(n==4)return be32(p);
    if(n==8)return ((uint64_t)be32(p)<<32)|be32(p+4);
    return 0;
}
/* Called only after the HAL validates the complete bounded FDT. */
static volatile uint8_t *find_proof(const uint8_t *p,size_t n) {
    if(n<40)return 0;
    uint32_t pos=be32(p+8),end=pos+be32(p+36),strings=be32(p+12),sn=be32(p+32);
    unsigned depth=0,chosen=0;uint64_t initrd_start=0,initrd_end=0;
    volatile uint8_t *found=0;
    if(end>n || strings>n || sn>n-strings)return 0;
    while(pos+4<=end) {
        uint32_t token=be32(p+pos);pos+=4;
        if(token==1) {
            uint32_t first=pos;while(pos<end && p[pos])pos++;
            if(pos==end)return 0;
            if(depth==1 && (same(p+first,pos-first+1,"chosen")||same(p+first,pos-first+1,"chosen@0")))chosen=depth+1;
            depth++;pos=(pos+4)&~3u;
        } else if(token==2) {if(!depth)return 0;if(chosen==depth)chosen=0;depth--;}
        else if(token==3) {
            if(pos+8>end)return 0;
            uint32_t len=be32(p+pos),name=be32(p+pos+4);pos+=8;
            if(len>end-pos || name>=sn)return 0;
            if(depth==chosen) {
                if(len==4096 && same(p+strings+name,sn-name,"xploader,menu-proof"))
                    found=(volatile uint8_t *)(p+pos);
                else if(same(p+strings+name,sn-name,"linux,initrd-start"))initrd_start=cells(p+pos,len);
                else if(same(p+strings+name,sn-name,"linux,initrd-end"))initrd_end=cells(p+pos,len);
            }
            pos=(pos+len+3)&~3u;
        } else if(token==4)continue;
        else if(token==9) {
            if(initrd_start && initrd_end>initrd_start)initrd=(struct xpl_hw_range){initrd_start,initrd_end-initrd_start};
            return found;
        } else return 0;
    }
    return 0;
}
static unsigned hex(volatile uint8_t *p,unsigned at,uint64_t value,unsigned digits) {
    static const char h[]="0123456789abcdef";
    for(unsigned i=0;i<digits;i++)p[at+i]=(uint8_t)h[(value>>(4*(digits-1-i)))&15];
    return at+digits;
}
static unsigned text(volatile uint8_t *p,unsigned at,const char *s) {
    while(*s && at<4095)p[at++]=(uint8_t)*s++;
    return at;
}
static unsigned field(unsigned at,const char *name,uint64_t value) {
    unsigned length=0;while(name[length])length++;
    unsigned digits=1;while(digits<16u&&(value>>(digits*4u)))digits++;
    if(length>64 || at>4095-length-18) {proof_overflow=1;return at;}
    at=text(proof,at,name);at=text(proof,at,"=");at=hex(proof,at,value,digits);return text(proof,at,"\n");
}
static void record(void) {
    unsigned at=0;
    if(!kernel_replaced) {
    volatile uint8_t *uts=(volatile uint8_t *)(uintptr_t)(kernel_base+XPL_UTS_VERSION_OFFSET);
    for(unsigned i=0;i<65;i++)uts[i]=0;
    uts[0]='X';uts[1]='P';uts[2]='M';at=3;
    for(unsigned i=0;i<8;i++)at=hex(uts,at,package_id[i],2);
    uts[at++]='-';at=hex(uts,at,stage,2);
    uts[at++]='-';at=hex(uts,at,result,2);
    uts[at++]='-';at=hex(uts,at,surface.base_pa,8);
    uts[at++]='-';at=hex(uts,at,transitions,4);
    uts[at++]='-';at=hex(uts,at,diag.detail,4);
    uts[at++]='-';at=hex(uts,at,diag.value,8);
    }
    if(proof) {
        proof_overflow=0;
        for(unsigned i=0;i<4096;i++)proof[i]=0;
        at=text(proof,0,"XPLMENU-001\npackage=");
        for(unsigned i=0;i<32;i++)at=hex(proof,at,package_id[i],2);
        at=text(proof,at,"\n");
        at=field(at,"stage",stage);at=field(at,"result",result);
        at=field(at,"kernel_base",kernel_base);at=field(at,"fdt_pa",fdt_pa);
        at=field(at,"initrd_base",initrd.base);at=field(at,"initrd_bytes",initrd.bytes);
        if(kernel_replaced) {
            if(XPL_EMMC_PROBE_LEVEL&&(!XPL_SERVICE_PROBE_LEVEL||load_target==XPL_MENU_BOOT_EMMC)) {
                at=field(at,"emmc_result",(uint32_t)emmc_result);
                at=field(at,"emmc_ahb_before",emmc.ahb_before);at=field(at,"emmc_ahb_live",emmc.ahb_live);
                at=field(at,"emmc_ahb_after",emmc.ahb_after);at=field(at,"emmc_apps_before",emmc.apps_before);
                at=field(at,"emmc_apps_after",emmc.apps_after);at=field(at,"emmc_version",emmc.version);
                at=field(at,"emmc_caps",emmc.caps);at=field(at,"emmc_caps1",emmc.caps1);
                at=field(at,"emmc_present",emmc.present);at=field(at,"emmc_core_mode",emmc.core_mode);
                at=field(at,"emmc_host",(uint64_t)emmc.host|((uint64_t)emmc.power<<8)|((uint64_t)emmc.clock<<16)|((uint64_t)emmc.host2<<32));
                at=field(at,"emmc_cmd",emmc.rcgr_cmd);at=field(at,"emmc_cfg",emmc.rcgr_cfg);
                at=field(at,"emmc_m",emmc.m);at=field(at,"emmc_n",emmc.n);at=field(at,"emmc_d",emmc.d);
                at=field(at,"emmc_reads",emmc.reads);at=field(at,"emmc_writes",emmc.writes);
                at=field(at,"emmc_restored",emmc.restored);at=field(at,"emmc_restore_result",(uint32_t)emmc.restore_result);
                if(XPL_EMMC_PROBE_LEVEL>=2) {
                    at=field(at,"emmc_media_result",(uint32_t)emmc_media_result);at=field(at,"emmc_phase",emmc_read_diag.phase);
                    at=field(at,"emmc_commands",emmc_read_diag.commands);at=field(at,"emmc_blocks",emmc_read_diag.blocks);
                    at=field(at,"emmc_last_cmd",emmc_read_diag.last_command);at=field(at,"emmc_irq",emmc_read_diag.last_irq);
                    at=field(at,"emmc_response",emmc_read_diag.last_response);at=field(at,"emmc_capacity",emmc_read_diag.capacity_sectors);
                    at=field(at,"emmc_ext",(uint64_t)emmc_read_diag.partition_config|((uint64_t)emmc_read_diag.bus_width<<8)|
                        ((uint64_t)emmc_read_diag.hs_timing<<16)|((uint64_t)emmc_read_diag.ext_revision<<24)|((uint64_t)emmc_read_diag.data_sector_size<<32));
                    at=field(at,"emmc_media_restored",emmc_read_diag.restored);at=field(at,"emmc_media_restore_result",(uint32_t)emmc_read_diag.restore_result);
                    at=field(at,"emmc_first_fnv",emmc_first_fingerprint);at=field(at,"emmc_boot_fnv",emmc_boot_fingerprint);
                    at=field(at,"emmc_tail_fnv",emmc_tail_fingerprint);
                    at=field(at,"emmc_mode_before",emmc_read_diag.core_before);at=field(at,"emmc_mode_active",emmc_read_diag.core_active);
                    at=field(at,"emmc_mode_after",emmc_read_diag.core_after);
                    at=field(at,"emmc_pll_vote_before",emmc_read_diag.pll_vote_before);at=field(at,"emmc_pll_vote_active",emmc_read_diag.pll_vote_active);
                    at=field(at,"emmc_pll_vote_after",emmc_read_diag.pll_vote_after);at=field(at,"emmc_pll_mode_before",emmc_read_diag.pll_mode_before);
                    at=field(at,"emmc_pll_mode_active",emmc_read_diag.pll_mode_active);at=field(at,"emmc_pll_mode_after",emmc_read_diag.pll_mode_after);
                    at=field(at,"emmc_bcr",emmc_read_diag.bcr);
                    at=field(at,"emmc_clock_saved",emmc_read_diag.saved_clock);at=field(at,"emmc_clock_active",emmc_read_diag.clock_active);
                    at=field(at,"emmc_reset_after",emmc_read_diag.reset_after);at=field(at,"emmc_vendor",emmc_read_diag.vendor);
                    at=field(at,"emmc_dll",emmc_read_diag.dll_config);at=field(at,"emmc_dll_status",emmc_read_diag.dll_status);
                    at=field(at,"emmc_core_power",emmc_read_diag.core_power);at=field(at,"emmc_core_version",emmc_read_diag.core_version);
                    if(XPL_EMMC_PROBE_LEVEL>=6) {
                        at=field(at,"emmc_init_cfg",emmc_read_diag.init_cfg);
                        at=field(at,"emmc_init_reset",emmc_read_diag.init_core_reset);
                        at=field(at,"emmc_init_irq",emmc_read_diag.init_power_irq);
                        at=field(at,"emmc_ocr",emmc_read_diag.ocr);
                        at=field(at,"emmc_init_card",emmc_read_diag.card_reinitialized);
                        at=field(at,"emmc_init_tries",emmc_read_diag.op_cond_tries);
                        at=field(at,"emmc_cid_hi",((uint64_t)emmc_read_diag.cid[0]<<32)|emmc_read_diag.cid[1]);
                        at=field(at,"emmc_cid_lo",((uint64_t)emmc_read_diag.cid[2]<<32)|emmc_read_diag.cid[3]);
                        if(XPL_EMMC_PROBE_LEVEL>=7) {
                            at=field(at,"emmc_max_hz",emmc_read_diag.max_clock_hz);
                            at=field(at,"emmc_fast_result",(uint32_t)emmc_fast_result);
                            at=field(at,"emmc_fast_cfg",emmc_read_diag.fast_cfg);
                            at=field(at,"emmc_img_result",(uint32_t)emmc_image_result);
                            at=field(at,"emmc_img_bytes",emmc_image_diag.bytes);
                            at=field(at,"emmc_img_fnv",emmc_image_diag.fingerprint);
                            at=field(at,"emmc_img_us",emmc_image_diag.elapsed_us);
                            at=field(at,"emmc_dec_result",(uint32_t)emmc_decode_result);
                            at=field(at,"emmc_raw_bytes",emmc_raw_bytes);
                            at=field(at,"emmc_raw_fnv",emmc_raw_fingerprint);
                            at=field(at,"emmc_ic_restore",emmc_icache.restored);
                        }
                    }
                }
            }
            at=field(at,"boot_target",boot_requested);at=field(at,"kernel_replaced",kernel_replaced);
            at=field(at,"payload_storage",load_target==XPL_MENU_BOOT_EMMC?1u:0u);
            at=field(at,"twrp_prepare_result",(unsigned)twrp_prepare_result);
            at=field(at,"payload_mode",load_target==XPL_MENU_TWRP?XPL_PAYLOAD_BOOTIMG:(unsigned)sd_payload.mode);at=field(at,"system_overlay",system_overlay);
            at=field(at,"dtb_selected",payload_dtb_selected);
            at=field(at,"fdt_bytes",fdt_bytes);at=field(at,"payload_args_bytes",twrp_fdt.args_bytes);
            at=field(at,"sd_ram_result",(unsigned)sd_ram_result);at=field(at,"sd_ram_bytes",sd_ram_bytes);
            at=field(at,"sd_ram_fingerprint",sd_ram_fingerprint);
            at=field(at,"sd_inflate_result",(unsigned)sd_inflate_result);at=field(at,"sd_raw_bytes",sd_raw_bytes);
            at=field(at,"sd_raw_fingerprint",sd_raw_fingerprint);at=field(at,"sd_gzip_member_bytes",sd_gzip_member_bytes);
            at=field(at,"sd_media_us",sd_media_us);at=field(at,"sd_read_commands",sd_read_commands);
            if(XPL_SD_MULTI_READ) {
                at=field(at,"sd_cmd18",sd_host.multi_commands);at=field(at,"sd_data_cmds",sd_host.data_commands);
                at=field(at,"sd_blocks",sd_host.blocks_read);
            }
            at=field(at,"sd_read_phase_us",sd_read_phase_us);at=field(at,"sd_decode_us",sd_decode_us);
            at=field(at,"sd_input_hash_us",sd_input_hash_us);at=field(at,"sd_output_hash_us",sd_output_hash_us);
            at=field(at,"lab_autoboot_ms",XPL_LAB_AUTOBOOT_MS);at=field(at,"lab_auto_sd",XPL_LAB_AUTOBOOT_SD);
            at=field(at,"lab_auto_target",XPL_LAB_AUTOBOOT_TARGET);
            at=field(at,"lab_auto_triggered",lab_auto_triggered);at=field(at,"lab_sd_fallback",lab_sd_fallback);
            at=field(at,"sd_owned_power",sd_owned_power);at=field(at,"sd_warm_reused",sd_legacy.warm_reused);
            for(unsigned i=0;i<4;i++) {
                static const char *const names[]={"sd_cid0","sd_cid1","sd_cid2","sd_cid3"};
                at=field(at,names[i],sd_host.cid[i]);
            }
            at=field(at,"countdown_completed",countdown_completed);at=field(at,"boot_progress_frames",boot_progress_frames);
            at=field(at,"boot_accept_us",boot_accept_us);at=field(at,"boot_go_us",boot_go_us);
            at=field(at,"handoff_us",handoff_us);at=field(at,"key_transitions",transitions);
            at=field(at,"esr",xpl_fault_record[0]);at=field(at,"far",xpl_fault_record[1]);at=field(at,"elr",xpl_fault_record[2]);
            at=field(at,"sd_boot_implemented",1);at=field(at,"recovery_protection_implemented",0);
        } else {
        at=field(at,"diag_phase",diag.phase);at=field(at,"diag_status",diag.status);
        at=field(at,"diag_address",diag.address);at=field(at,"diag_value",diag.value);
        at=field(at,"diag_detail",diag.detail);at=field(at,"diag_reads",diag.reads);at=field(at,"diag_writes",diag.writes);
        at=field(at,"display_result",display_result);at=field(at,"display_diag_address",display_diag.address);at=field(at,"display_diag_detail",display_diag.detail);
        at=field(at,"key_result",key_result);at=field(at,"key_diag_address",key_diag.address);at=field(at,"key_diag_value",key_diag.value);at=field(at,"key_diag_detail",key_diag.detail);
        at=field(at,"ram_count",hardware.ram_count);at=field(at,"splash_base",hardware.splash.base);at=field(at,"splash_bytes",hardware.splash.bytes);
        at=field(at,"gdsc",display.gdsc);at=field(at,"ahb_clock",display.ahb_clock);at=field(at,"core_clock",display.core_clock);
        at=field(at,"mdp_revision",display.revision);at=field(at,"ctl",display.ctl);at=field(at,"mixer",display.mixer);at=field(at,"pipe",display.pipe);
        at=field(at,"top0",display.top[0]);at=field(at,"layer0",display.layer[0]);at=field(at,"ext0",display.layer_ext[0]);
        at=field(at,"source_address",display.source.address);at=field(at,"source_format",display.source.format);at=field(at,"source_unpack",display.source.unpack);
        at=field(at,"source_size",display.source.size);at=field(at,"source_image_size",display.source.image_size);
        at=field(at,"source_stride",display.source.stride);
        at=field(at,"surface_pa",surface.base_pa);at=field(at,"surface_width",surface.width);at=field(at,"surface_height",surface.height);
        at=field(at,"surface_stride",surface.stride_bytes);at=field(at,"surface_bpp",surface.bytes_per_pixel);
        at=field(at,"scanout_base_pa",surface.scanout_base_pa);at=field(at,"scanout_bytes",surface.scanout_bytes);
        at=field(at,"scanout_height",surface.scanout_height);at=field(at,"top_rows_skipped",surface.top_rows_skipped);
        at=field(at,"presented_frames",presented_frames);
        at=field(at,"full_frames",full_frames);at=field(at,"partial_frames",partial_frames);
        at=field(at,"draw_last_us",draw_last_us);at=field(at,"draw_max_us",draw_max_us);
        at=field(at,"draw_full_max_us",draw_full_max_us);at=field(at,"draw_partial_max_us",draw_partial_max_us);
        at=field(at,"present_max_us",present_max_us);at=field(at,"key_poll_max_us",key_poll_max_us);
        at=field(at,"input_gap_max_us",input_gap_max_us);
        at=field(at,"boot_accept_us",boot_accept_us);at=field(at,"boot_page_us",boot_page_us);
        at=field(at,"boot_go_us",boot_go_us);at=field(at,"handoff_us",handoff_us);
        at=field(at,"boot_progress_frames",boot_progress_frames);
        at=field(at,"countdown3_us",countdown_us[0]);at=field(at,"countdown2_us",countdown_us[1]);
        at=field(at,"countdown1_us",countdown_us[2]);at=field(at,"countdown_completed",countdown_completed);
        at=field(at,"bootargs_result",bootargs_result);at=field(at,"bootargs_removed",bootargs_removed);
        at=field(at,"bootargs_props",bootarg_stats.properties);at=field(at,"startup_masked",bootarg_stats.startup_masked);
        at=field(at,"startup_original",bootarg_stats.first_startup);
        at=field(at,"entry_sctlr",entry_sctlr);at=field(at,"entry_el",entry_el);
        at=field(at,"frame_result",frame_result);at=field(at,"frame_dcs_initial",frame_diag.initial_dcs);
        at=field(at,"frame_dcs",frame_diag.current_dcs);at=field(at,"frame_done",frame_diag.frames);
        at=field(at,"frame_wait_us",frame_diag.max_wait_us);at=field(at,"frame_irq",frame_diag.last_irq);
        at=field(at,"sd_board_result",(uint32_t)sd_board_result);
        at=field(at,"sd_pmic_result",(uint32_t)sd_pmic_result);
        at=field(at,"sd_probe_us",sd_probe_us);at=field(at,"sd_reads",sd_reads);
        at=field(at,"sd_read_commands",sd_read_commands);
        at=field(at,"sd_apps",sd_board.gcc_apps);at=field(at,"sd_ahb",sd_board.gcc_ahb);
        at=field(at,"sd_rcgr_cmd",sd_board.rcgr_cmd);at=field(at,"sd_rcgr_cfg",sd_board.rcgr_cfg);
        at=field(at,"sd_m",sd_board.m);at=field(at,"sd_n",sd_board.n);at=field(at,"sd_d",sd_board.d);
        at=field(at,"sd_pads",sd_board.pads);at=field(at,"sd_l13",pack_ldo(&sd_l13));
        at=field(at,"sd_l21",pack_ldo(&sd_l21));
        at=field(at,"sd_if_result",(uint32_t)sd_interface_result);
        at=field(at,"sd_if_ahb_before",sd_interface.ahb_before);at=field(at,"sd_if_ahb_live",sd_interface.ahb_live);
        at=field(at,"sd_if_ahb_after",sd_interface.ahb_after);at=field(at,"sd_if_apps_after",sd_interface.apps_after);
        at=field(at,"sd_if_version",sd_interface.version);at=field(at,"sd_if_caps",sd_interface.caps);
        at=field(at,"sd_if_present",sd_interface.present);at=field(at,"sd_if_core_mode",sd_interface.core_mode);
        at=field(at,"sd_if_host",(uint64_t)sd_interface.host_mode|((uint64_t)sd_interface.power<<8)|((uint64_t)sd_interface.clock<<16));
        at=field(at,"sd_if_reads",sd_interface.host_reads);at=field(at,"sd_if_writes",sd_interface_writes);
        at=field(at,"sd_if_restored",sd_interface.restored);at=field(at,"sd_if_restore_result",(uint32_t)sd_interface.restore_result);
        at=field(at,"timer_hz",timer_frequency);at=field(at,"sd_core_result",(uint32_t)sd_core_result);
        at=field(at,"sd_core_phase",sd_core.phase);at=field(at,"sd_core_apps",sd_core.active.gcc_apps);
        at=field(at,"sd_core_ahb",sd_core.active.gcc_ahb);at=field(at,"sd_core_cfg",sd_core.active.rcgr_cfg);
        at=field(at,"sd_core_frac",(sd_core.active.m&255u)|((sd_core.active.n&255u)<<8)|((sd_core.active.d&255u)<<16));
        at=field(at,"sd_core_mode",sd_core.mode_active);at=field(at,"sd_core_caps",sd_core.caps);
        at=field(at,"sd_core_clock",sd_core.clock_active);at=field(at,"sd_core_power_status",sd_core.power_status);
        at=field(at,"sd_core_power_before",sd_core.power_before);
        at=field(at,"sd_core_writes",sd_core.writes);at=field(at,"sd_core_restored",sd_core.restored);
        at=field(at,"sd_core_restore_result",(uint32_t)sd_core.restore_result);
        at=field(at,"sd_pow_result",(uint32_t)sd_power_result);at=field(at,"sd_pow_status",sd_power.write_status);
        at=field(at,"sd_pow_modes",sd_power.before|(sd_power.active<<8)|(sd_power.after<<16));
        at=field(at,"sd_pow_attempts",sd_power.write_commands);at=field(at,"sd_pow_modified",sd_power.modified);
        at=field(at,"sd_pow_restored",sd_power.restored);at=field(at,"sd_pow_hold_us",sd_power.hold_us);
        at=field(at,"sd_pow_restore_result",(uint32_t)sd_power.restore_result);
        at=field(at,"sd_media_result",(uint32_t)sd_media_result);at=field(at,"sd_media_us",sd_media_us);
        at=field(at,"sd_legacy_phase",sd_legacy.phase);at=field(at,"sd_legacy_verified",sd_legacy.verified);
        at=field(at,"sd_legacy_req",sd_legacy.request);at=field(at,"sd_legacy_ack",sd_legacy.ack);
        at=field(at,"sd_legacy_r7",sd_legacy.r7);at=field(at,"sd_legacy_irq",sd_legacy.last_irq);
        at=field(at,"sd_legacy_cleanup",(uint32_t)sd_legacy.cleanup_result);
        at=field(at,"sd_owned_power",sd_owned_power);at=field(at,"sd_warm_reused",sd_legacy.warm_reused);
        at=field(at,"sd_init_result",(uint32_t)sd_init_result);at=field(at,"sd_lba_result",(uint32_t)sd_lba_result);
        at=field(at,"sd_proto_irq",sd_host.last_interrupt);at=field(at,"sd_proto_response",sd_host.last_response);
        at=field(at,"sd_blocks",sd_host.blocks512);
        at=field(at,"sd_cid_hi",((uint64_t)sd_host.cid[0]<<32)|sd_host.cid[1]);
        at=field(at,"sd_cid_lo",((uint64_t)sd_host.cid[2]<<32)|sd_host.cid[3]);
        at=field(at,"sd_lba_fingerprint",sd_lba_fingerprint);
        at=field(at,"sd_fs_result",(uint32_t)sd_fs_result);at=field(at,"sd_fs_serial",sd_fs.serial);
        at=field(at,"sd_fs_cluster",sd_fs.cluster_bytes);at=field(at,"sd_fs_flags",sd_fs.volume_flags);
        at=field(at,"sd_partition_start",sd_partition_start);at=field(at,"sd_partition_sectors",sd_partition_sectors);
        at=field(at,"sd_file_result",(uint32_t)sd_file_result);at=field(at,"sd_file_bytes",sd_file_bytes);
        at=field(at,"sd_file_fingerprint",sd_file_fingerprint);at=field(at,"sd_twrp_result",(uint32_t)sd_twrp_result);
        at=field(at,"sd_twrp_bytes",sd_twrp_bytes);at=field(at,"sd_twrp_header_fingerprint",sd_twrp_header_fingerprint);
        at=field(at,"sd_fast_result",(uint32_t)sd_fast_result);at=field(at,"sd_fast_cfg",sd_fast.cfg);
        at=field(at,"sd_fast_clock",sd_fast.clock);at=field(at,"sd_card_max_hz",sd_host.max_clock_hz);
        at=field(at,"sd_ram_result",(uint32_t)sd_ram_result);at=field(at,"sd_ram_bytes",sd_ram_bytes);
        at=field(at,"sd_ram_fingerprint",sd_ram_fingerprint);
        at=field(at,"sd_ram_base",0x12000000u);at=field(at,"sd_inflate_result",(uint32_t)sd_inflate_result);
        at=field(at,"sd_raw_bytes",sd_raw_bytes);at=field(at,"sd_raw_fingerprint",sd_raw_fingerprint);
        at=field(at,"sd_gzip_member_bytes",sd_gzip_member_bytes);
        at=field(at,"dsi_result",dsi_result);at=field(at,"dsi_ctrl",dsi_ctrl);
        at=field(at,"dsi_mdp",dsi_mdp);at=field(at,"dsi_clock",dsi_clock);
        at=field(at,"dsi_before_stream",dsi_before_stream);at=field(at,"dsi_before_total",dsi_before_total);
        at=field(at,"dsi_boot_stream",dsi_boot_stream);at=field(at,"dsi_boot_total",dsi_boot_total);
        at=field(at,"roi_result",roi_result);at=field(at,"roi_commands",roi_diag.commands);at=field(at,"roi_revision",roi_diag.revision);
        at=field(at,"roi_dma_control",roi_diag.dma_control);at=field(at,"roi_trigger_control",roi_diag.trigger_control);
        at=field(at,"roi_tpg_control",roi_diag.tpg_control);
        at=field(at,"lab_autoboot_ms",XPL_LAB_AUTOBOOT_MS);at=field(at,"lab_auto_triggered",lab_auto_triggered);
        at=field(at,"native_hold_ms",XPL_NATIVE_HOLD_MS);
        at=field(at,"command_mode",surface.command_mode);at=field(at,"ctl_start_pa",surface.ctl_start_pa);
        at=field(at,"ctl_flush_pa",surface.ctl_flush_pa);at=field(at,"ctl_flush_mask",surface.ctl_flush_mask);
        at=field(at,"spmi_version",keys.arbiter_version);at=field(at,"key_bits",key_bits);at=field(at,"key_transitions",transitions);
        at=field(at,"watchdog_enabled",watchdog_enabled);at=field(at,"watchdog_bark",watchdog_bark);at=field(at,"watchdog_bite",watchdog_bite);
        at=field(at,"esr",xpl_fault_record[0]);at=field(at,"far",xpl_fault_record[1]);at=field(at,"elr",xpl_fault_record[2]);
        at=field(at,"sd_boot_implemented",0);at=field(at,"recovery_protection_implemented",0);
        }
        if(kernel_replaced) {
            at=field(at,"icache_before",icache_diag.before);at=field(at,"icache_during",icache_diag.during);
            at=field(at,"icache_after",icache_diag.after);at=field(at,"icache_changed",icache_diag.changed);
            at=field(at,"icache_restored",icache_diag.restored);
        }
        if(XPL_SERVICE_PROBE_LEVEL) {
            at=field(at,"g8_probe",(uint32_t)service_probe_result);
            at=field(at,"g8_reason",service_snapshot.reason);at=field(at,"g8_pshold",service_snapshot.pshold);
            at=field(at,"g8_pon0",service_snapshot.pon[0]);at=field(at,"g8_pon2",service_snapshot.pon[1]);
            at=field(at,"g8_pon0_rc",(uint32_t)service_snapshot.pon_result[0]);at=field(at,"g8_pon2_rc",(uint32_t)service_snapshot.pon_result[1]);
            at=field(at,"g8_gdsc",service_snapshot.gdsc);at=field(at,"g8_master",service_snapshot.master);
            at=field(at,"g8_sleep",service_snapshot.sleep);at=field(at,"g8_mock",service_snapshot.mock);
            at=field(at,"g8_usb_rc",(uint32_t)service_snapshot.usb_result);at=field(at,"g8_usb_id",service_snapshot.usb_id);
            at=field(at,"g8_dctl",service_snapshot.usb_control);at=field(at,"g8_dcfg",service_snapshot.usb_config);
            at=field(at,"g8_dsts",service_snapshot.usb_status);at=field(at,"g8_commands",service_snapshot.commands);
            at=field(at,"g8_s1_requests",service_requests);at=field(at,"g8_s1_result",(uint32_t)service_reset_result);
            at=field(at,"g8_s1_phase",service_reset_diag.phase);at=field(at,"g8_s1_writes",service_reset_diag.writes);
            at=field(at,"g8_old_reason",service_reset_diag.old_reason);at=field(at,"g8_dl0",service_reset_diag.dload0);
            at=field(at,"g8_dl1",service_reset_diag.dload1);
            if(usb_guard_diag.phase) {
                at=field(at,"g8_wd_rc",(uint32_t)usb_guard_result);at=field(at,"g8_wd_phase",usb_guard_diag.phase);
                at=field(at,"g8_wd_old",((uint64_t)usb_guard_diag.old_bark<<32)|usb_guard_diag.old_bite);
                at=field(at,"g8_wd_en",usb_guard_diag.old_en);at=field(at,"g8_wd_restore",(uint32_t)usb_guard_diag.restore_result);
            }
            if(XPL_USB_ENUM_PROBE||XPL_USB_MSD_READONLY||XPL_USB_MSD_WRITABLE) {
                at=field(at,"g8_ue_rc",(uint32_t)usb_enum_result);at=field(at,"g8_ue_phase",usb_enum_diag.phase);
                at=field(at,"g8_ue_events",((uint64_t)usb_enum_diag.events<<32)|usb_enum_diag.setups);
                at=field(at,"g8_ue_bus",((uint64_t)usb_enum_diag.resets<<32)|usb_enum_diag.connects);
                at=field(at,"g8_ue_last",usb_enum_diag.last_event);at=field(at,"g8_ue_setup",usb_enum_diag.last_setup);
                at=field(at,"g8_ue_cmd",((uint64_t)usb_enum_diag.last_command<<32)|usb_enum_diag.last_command_value);
                at=field(at,"g8_ue_phy",((uint64_t)usb_enum_diag.phy_version<<32)|usb_enum_diag.phy_ctrl);
                at=field(at,"g8_ue_common",usb_enum_diag.phy_common);
                at=field(at,"g8_ue_ctl",((uint64_t)usb_enum_diag.dctl<<32)|usb_enum_diag.dsts);
                at=field(at,"g8_ue_state",((uint64_t)usb_enum_diag.configured<<32)|usb_enum_diag.address);
                at=field(at,"g8_ue_clean",((uint64_t)usb_enum_diag.cleanup_errors<<32)|usb_enum_diag.power.restore_mask);
                at=field(at,"g8_ue_pre",usb_enum_diag.power.before_hash);at=field(at,"g8_ue_post",usb_enum_diag.power.after_hash);
                at=field(at,"g8_ue_us",usb_enum_diag.elapsed_us);
                at=field(at,"g8_ue_addr",usb_enum_diag.address_register);
                for(unsigned n=0;n<4;n++){static const char *const labels[]={"g8_ue_q0","g8_ue_q1","g8_ue_q2","g8_ue_q3"};at=field(at,labels[n],usb_enum_diag.setup_history[n]);}
                at=field(at,"g8_ue_trb",((uint64_t)usb_enum_diag.last_trb_control<<32)|usb_enum_diag.last_trb_size);
                if(XPL_USB_MSD_READONLY||XPL_USB_MSD_WRITABLE){
                    at=field(at,"g8_msd_rc",(uint32_t)msd_result);at=field(at,"g8_msd_blocks",msd_sectors);
                    at=field(at,"g8_msd_io",((uint64_t)usb_enum_diag.media_commands<<32)|usb_enum_diag.media_reads);
                    at=field(at,"g8_msd_writes",usb_enum_diag.media_writes);
                    at=field(at,"g8_pcsd_requests",pcsd_requests);
                    at=field(at,"g8_pcsd_remount",(uint32_t)pcsd_remount_result);
                    at=field(at,"g8_pcsd_bytes",pcsd_remount_bytes);at=field(at,"g8_pcsd_fnv",pcsd_remount_fingerprint);
                    at=field(at,"g8_msd_state",((uint64_t)usb_enum_diag.media_ejected<<32)|usb_enum_diag.media_phase);
                    at=field(at,"g8_msd_cid0",((uint64_t)msd_cid[0]<<32)|msd_cid[1]);
                    at=field(at,"g8_msd_cid1",((uint64_t)msd_cid[2]<<32)|msd_cid[3]);
                    at=field(at,"g8_msd_fifo",((uint64_t)usb_enum_diag.fifo_ram<<32)|usb_enum_diag.fifo1);
                }
            }
            if(XPL_USB_POWER_PROBE) {
                at=field(at,"g8_up_rc",(uint32_t)usb_power_result);at=field(at,"g8_up_phase",usb_power_diag.phase);
                at=field(at,"g8_up_cleanup",(uint32_t)usb_power_diag.cleanup_result);at=field(at,"g8_up_restore",usb_power_diag.restore_mask);
                at=field(at,"g8_up_pre",usb_power_diag.before_hash);at=field(at,"g8_up_post",usb_power_diag.after_hash);
                at=field(at,"g8_up_id",usb_power_diag.id);at=field(at,"g8_up_ctl",((uint64_t)usb_power_diag.dctl<<32)|usb_power_diag.dsts);
                at=field(at,"g8_up_pll0",((uint64_t)usb_power_diag.pll_mode<<32)|usb_power_diag.pll_l);
                at=field(at,"g8_up_pll1",((uint64_t)usb_power_diag.pll_alpha_hi<<32)|usb_power_diag.pll_alpha);
                at=field(at,"g8_up_root0",((uint64_t)usb_power_diag.before[6]<<32)|usb_power_diag.before[5]);
                at=field(at,"g8_up_root1",((uint64_t)usb_power_diag.before[8]<<32)|usb_power_diag.before[7]);
                at=field(at,"g8_up_root2",((uint64_t)usb_power_diag.before[9]<<32)|usb_power_diag.before[11]);
                at=field(at,"g8_up_io",((uint64_t)usb_power_diag.reads<<32)|usb_power_diag.writes);
            }
        }
        if(XPL_SETTINGS_ENABLED){
            at=field(at,"cfg_load",(uint32_t)settings_load_result);at=field(at,"cfg_save",(uint32_t)settings_save_result);
            at=field(at,"cfg_seq",boot_settings.sequence);at=field(at,"cfg_mode",boot_settings.startup);
            at=field(at,"cfg_target",boot_settings.target);at=field(at,"cfg_slot",settings_slot);
            at=field(at,"cfg_lang",boot_settings.language[0]=='z'?1u:0u);
            at=field(at,"power_result",(uint32_t)power_result);at=field(at,"power_phase",power_diag.phase);
            at=field(at,"power_dload0",power_diag.dload0);at=field(at,"power_dload1",power_diag.dload1);
            at=field(at,"reset_phase",restart_diag.phase);at=field(at,"reset_writes",restart_diag.writes);
            at=field(at,"reset_old_reason",restart_diag.old_reason);
            at=field(at,"reset_dload0",restart_diag.dload0);at=field(at,"reset_dload1",restart_diag.dload1);
            if(XPL_LAB_SERVICE_KIND){
                at=field(at,"lab_kind",XPL_LAB_SERVICE_KIND);at=field(at,"lab_phase",lab_service_phase);
                at=field(at,"lab_result",lab_service_result);at=field(at,"lab_flags",lab_service_flags);
                at=field(at,"lab_entryseq",lab_service_entry_sequence);at=field(at,"lab_kernel",lab_service_payload_bytes);at=field(at,"lab_blocks",lab_service_sd_blocks);
            }
            at=field(at,"cfg_saves",settings_saves);at=field(at,"cfg_keys",settings_start_keys);
        }
        at=field(at,"proof_overflow",proof_overflow);
        (void)at;
    }
    __asm__ volatile("dsb sy":::"memory");
}
static int io_read(void *unused,uint64_t address,uint32_t *value) {
    (void)unused;return !(address&3) && !xpl_guard_read32((uintptr_t)address,value);
}
static int io_write(void *unused,uint64_t address,uint32_t value) {
    (void)unused;return !(address&3) && !xpl_guard_write32((uintptr_t)address,value);
}
static void fence(void *unused) {(void)unused;__asm__ volatile("dsb sy":::"memory");}
static int clean_bootargs(void) {
    xpl_bootargs_stats pass;
    bootargs_result=(unsigned)xpl_bootargs_clean_fdt((void *)(uintptr_t)fdt_pa,fdt_bytes,&pass);
    if(bootargs_result)return -1;
    if(!bootarg_stats.startup_masked)bootarg_stats.first_startup=pass.first_startup;
    bootarg_stats.properties=pass.properties;bootarg_stats.chosen_nodes=pass.chosen_nodes;
    bootarg_stats.nonterminated=pass.nonterminated;
    bootarg_stats.startup_masked+=pass.startup_masked;
    bootargs_removed+=pass.removed;
    __asm__ volatile("dsb sy":::"memory");
    return 0;
}
static enum xpl_menu_action_result action(void *unused,enum xpl_menu_action selected) {
    (void)unused;
    if((selected==XPL_MENU_RESET||selected==XPL_MENU_POWEROFF)&&XPL_SERVICE_PROBE_LEVEL){
        system_request(selected);return XPL_MENU_ACTION_RETURNED;
    }
    if(selected==XPL_MENU_BOOT_EMMC) {
        if(now_us(&boot_accept_us))return XPL_MENU_ACTION_FAILED;
        if(clean_bootargs())return XPL_MENU_ACTION_FAILED;
        if(XPL_LAB_EMMC_TAIL) {
            load_target=XPL_MENU_BOOT_EMMC;boot_requested=4;
            payload_dtb_source=payload_dtb_bytes=0;payload_dtb_selected=system_overlay=0;
            sd_payload=(struct xpl_payload_descriptor){.mode=XPL_PAYLOAD_BOOTIMG};
        }else boot_requested=1;
        return XPL_MENU_ACTION_RETURNED;
    }
    if(selected==XPL_MENU_TWRP||selected==XPL_MENU_BOOT_SD) {
        if(now_us(&boot_accept_us))return XPL_MENU_ACTION_FAILED;
        sd_picker_operation=sd_picker_request=0;
        sd_media_result=XPL_SD_EPREP;sd_ram_result=XPL_EXFAT_EIO;sd_inflate_result=XPL_GZIP_ARGUMENT;
        sd_ram_bytes=0;sd_raw_bytes=0;sd_ram_fingerprint=0;sd_raw_fingerprint=0;sd_gzip_member_bytes=0;
        sd_read_phase_us=sd_decode_us=sd_input_hash_us=sd_output_hash_us=0;
        payload_kernel_source=0;payload_kernel_bytes=0;payload_initrd_source=0;payload_initrd_bytes=0;
        payload_dtb_source=0;payload_dtb_bytes=0;payload_dtb_selected=0;system_overlay=0;
        sd_payload=(struct xpl_payload_descriptor){0};
        for(unsigned i=0;i<sizeof payload_cmdline;i++)payload_cmdline[i]=0;
        load_target=selected;boot_requested=selected==XPL_MENU_TWRP?2u:3u;
        return XPL_MENU_ACTION_RETURNED;
    }
    return XPL_MENU_ACTION_UNSUPPORTED;
}
static enum xpl_menu_action_result service_action(void *unused,enum xpl_menu_service_action selected) {
    (void)unused;
    if(selected==XPL_SERVICE_PC_SD&&(XPL_USB_MSD_READONLY||XPL_USB_MSD_WRITABLE)){
        pcsd_requested=1;return XPL_MENU_ACTION_RETURNED;
    }
    if(selected!=XPL_SERVICE_FASTBOOT)return XPL_MENU_ACTION_UNSUPPORTED;
    service_requested=1;return XPL_MENU_ACTION_RETURNED;
}
static int now_us(uint64_t *out) {
    uint64_t ticks;
    if(xpl_guard_counter(&ticks) || ticks<start_ticks)return -1;
    ticks-=start_ticks;
    if(ticks/timer_frequency>UINT64_MAX/1000000)return -1;
    *out=(ticks/timer_frequency)*1000000+((ticks%timer_frequency)*1000000)/timer_frequency;
    return 0;
}
static int pet_watchdog(uint64_t current) {
    if(!(watchdog_enabled&1) || current-watchdog_last_pet<50)return 0;
    if(xpl_guard_write32((uintptr_t)hardware.watchdog.base+4,1))return -1;
    watchdog_last_pet=current;
    return 0;
}

/* This adapter exposes observation only. All GCC/pad and PMIC configuration
 * writes are denied even though the reusable board library contains planners.
 * SPMI CMD writes can request only the thirteen audited one-byte PMIC reads.
 * No SDHC2, SDCC core, media command, RPM or regulator write is exposed. */
static int sd_read(void *unused,uint64_t a,unsigned width,uint32_t *v) {
    (void)unused;
    if(width!=4 || !(a==0xfc400504u||a==0xfc400508u||a==0xfc400510u||a==0xfc400514u||
       a==0xfc400518u||a==0xfc40051cu||a==0xfc400520u||a==0xfd512048u||
       a==0xfc4cf000u||a==0xfc4cf808u||a==0xfc4cf818u))return -1;
    sd_reads++;
    return xpl_guard_read32((uintptr_t)a,v);
}
static int sd_write(void *unused,uint64_t a,unsigned width,uint32_t v) {
    (void)unused;
    unsigned sid=(v>>20)&15u,addr=(v>>4)&65535u,o=addr&255u;
    if(a!=0xfc4cf800u||width!=4||v!=((1u<<27)|(sid<<20)|(addr<<4))||
       !((sid==0&&addr==0x105u)||(sid==1&&(addr>>8==0x4cu||addr>>8==0x54u)&&
       (o==4u||o==5u||o==0x40u||o==0x41u||o==0x45u||o==0x46u))))return -1;
    sd_read_commands++;
    return xpl_guard_write32((uintptr_t)a,v);
}
static uint64_t sd_time(void *unused) {
    uint64_t t=0;(void)unused;
    if(now_us(&t)||pet_watchdog(t/1000))return UINT64_MAX;
    return t;
}
static int usb_power_read(void *unused,uint64_t a,unsigned width,uint32_t *value) {
    (void)unused;
    if(width!=4)return -1;
    switch(a) {
    case 0xfc400000u:case 0xfc400004u:case 0xfc400008u:case 0xfc40000cu:case 0xfc4003c0u:
    case 0xfc4003c4u:case 0xfc4003fcu:case 0xfc4003c8u:case 0xfc4003ccu:case 0xfc4003d0u:
    case 0xfc4003d4u:case 0xfc4003d8u:case 0xfc4003dcu:case 0xfc4003e0u:case 0xfc4003e4u:
    case 0xfc4003e8u:case 0xfc4003ecu:case 0xf920c120u:case 0xf920c704u:case 0xf920c70cu:
        return xpl_guard_read32((uintptr_t)a,value);
    default:return -1;
    }
}
static int usb_power_write(void *unused,uint64_t a,unsigned width,uint32_t value) {
    (void)unused;if(width!=4)return -1;
    switch(a) {
    case 0xfc4003c4u:case 0xfc4003fcu:case 0xfc4003c8u:case 0xfc4003ccu:case 0xfc4003d0u:
    case 0xfc4003d4u:case 0xfc4003d8u:case 0xfc4003dcu:case 0xfc4003e0u:case 0xfc4003e4u:
    case 0xfc4003e8u:case 0xfc4003ecu:return xpl_guard_write32((uintptr_t)a,value);
    default:return -1;
    }
}
static int usb_enum_read(void *unused,uint64_t a,unsigned width,uint32_t *value) {
    if(width!=4||(a&3u))return -1;
    if((a>=0xf920c100u&&a<=0xf920c83cu)||a==0xf92f8800u||a==0xfc401a84u||a==0xfc4004acu||
       a==0xfc4004a8u||a==0xfd4ab000u||a==0xf92f8808u||a==0xf92f8810u||a==0xf92f8814u||a==0xf92f88ecu)
        return xpl_guard_read32((uintptr_t)a,value);
    return usb_power_read(unused,a,width,value);
}
static int usb_enum_write(void *unused,uint64_t a,unsigned width,uint32_t value) {
    if(width!=4||(a&3u))return -1;
    if((a>=0xf920c800u&&a<=0xf920c83cu)||a==0xf920c110u||a==0xf920c200u||a==0xf920c2c0u||a==0xf920c300u||a==0xf920c304u||
       a==0xf920c700u||a==0xf920c704u||a==0xf920c708u||a==0xf920c400u||a==0xf920c404u||
       a==0xf920c408u||a==0xf920c40cu||a==0xf920c720u||a==0xfc4003c0u||a==0xfc401a84u||a==0xfc4004acu||
       a==0xfc4004a8u||a==0xfd4ab000u||a==0xf92f8808u||a==0xf92f8810u||a==0xf92f8814u||a==0xf92f88ecu)
        return xpl_guard_write32((uintptr_t)a,value);
    return usb_power_write(unused,a,width,value);
}
static void sd_relax(void *unused) {(void)unused;__asm__ volatile("yield");}
static void sd_observe(void) {
    uint64_t start,end;
    const xpl_sd_board_io io={0,sd_read,sd_write,sd_time,sd_relax};
    if(now_us(&start))return;
    sd_board_result=xpl_sd_board_probe(&io,&sd_board);
    /* Serialized before the menu poll loop; no overlapping key transaction. */
    sd_pmic_result=xpl_sd_board_pmic_snapshot(&io,hardware.spmi.base,
        (uint32_t)hardware.spmi.bytes,hardware.spmi_channel,hardware.spmi_ee,1000,&sd_l13,&sd_l21);
    if(!now_us(&end)&&end>=start)sd_probe_us=end-start;
}

static int sd_interface_read(void *unused,uint64_t a,unsigned w,uint32_t *v) {
    (void)unused;
    if(w==4&&(a==0xfc400504u||a==0xfc400508u||a==0xf98a4940u||a==0xf98a4924u||a==0xf98a4078u))
        return xpl_guard_read32((uintptr_t)a,v);
    if(w==2&&(a==0xf98a49feu||a==0xf98a492cu))return xpl_guard_read16((uintptr_t)a,v);
    if(w==1&&(a==0xf98a4928u||a==0xf98a4929u))return xpl_guard_read8((uintptr_t)a,v);
    return -1;
}
static int sd_interface_write(void *unused,uint64_t a,unsigned w,uint32_t v) {
    (void)unused;uint32_t old;
    if(a!=0xfc400508u||w!=4||xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~0x80000001u))return -1;
    sd_interface_writes++;
    return xpl_guard_write32((uintptr_t)a,v);
}
static void sd_interface_observe(void) {
    /* Exact frozen E6653 DTs identify this separate SDCC2 interface. We run
     * once under S1's single boot-CPU contract before any OS/MMC driver runs.
     * The inherited peripheral interconnect is available; the gated APPS and
     * AHB branches confirm no live SDCC2 transfer. This earns only interface
     * probe ownership/access, not regulator/core/card-clock readiness. */
    if(!hardware.complete||sd_board_result||entry_el!=4u||(entry_sctlr&1u))return;
    const xpl_sd_board_io io={0,sd_interface_read,sd_interface_write,sd_time,sd_relax};
    sd_interface_result=xpl_sd_interface_probe(&io,XPL_SD_BOARD_EXCLUSIVE|XPL_SD_BOARD_BUS_SAFE,100000,&sd_interface);
}
static int emmc_read(void *unused,uint64_t a,unsigned w,uint32_t *v) {
    (void)unused;
    if(w==4&&(a==0xfc4004c4u||a==0xfc4004c8u||a==0xfc4004d0u||a==0xfc4004d4u||
       a==0xfc4004d8u||a==0xfc4004dcu||a==0xfc4004e0u||a==0xf9824940u||
       a==0xf9824944u||a==0xf9824924u||a==0xf9824078u))return xpl_guard_read32((uintptr_t)a,v);
    if(w==2&&(a==0xf98249feu||a==0xf982492cu||a==0xf982493eu))return xpl_guard_read16((uintptr_t)a,v);
    if(w==1&&(a==0xf9824928u||a==0xf9824929u))return xpl_guard_read8((uintptr_t)a,v);
    return -1;
}
static int emmc_write(void *unused,uint64_t a,unsigned w,uint32_t v) {
    (void)unused;uint32_t old;
    if(a!=0xfc4004c8u||w!=4||xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~0x80000001u))return -1;
    return xpl_guard_write32((uintptr_t)a,v);
}
static void emmc_observe(void) {
    /* Both immutable E6653 parent DTs describe SDCC1 hc=f9824900/core=f9824000.
     * The original S1 has finished loading before this single boot CPU entry.
     * It owns neither our SD reader nor a Linux MMC driver at this point. */
    if(!hardware.complete||entry_el!=4u||(entry_sctlr&1u))return;
    const xpl_sd_board_io io={0,emmc_read,emmc_write,sd_time,sd_relax};
    emmc_result=xpl_emmc_interface_probe(&io,3u,100000u,&emmc);
}
static int emmc_protocol_read(void *unused,uint64_t a,unsigned w,uint32_t *v) {
    (void)unused;
    if(XPL_EMMC_PROBE_LEVEL>=6&&w==4&&(a==0xfc4004d0u||a==0xfc4004d4u||a==0xfc4004d8u||a==0xfc4004dcu||a==0xfc4004e0u||a==0xf98240dcu||a==0xf98240e8u))return xpl_guard_read32((uintptr_t)a,v);
    if(XPL_EMMC_PROBE_LEVEL>=3&&a==0xf9824078u&&w==4)return xpl_guard_read32((uintptr_t)a,v);
    if(XPL_EMMC_PROBE_LEVEL>=4&&w==4&&(a==0xfc401480u||a==0xfc401dc0u||a==0xfc4004c0u))return xpl_guard_read32((uintptr_t)a,v);
    if(XPL_EMMC_PROBE_LEVEL>=5&&w==4&&(a==0xf9824a00u||a==0xf9824a08u||a==0xf9824a0cu||a==0xf9824000u||a==0xf9824050u))return xpl_guard_read32((uintptr_t)a,v);
    if(a<0xf9824900u||a+w>0xf9824a00u||a&(w-1u))return -1;
    unsigned o=(unsigned)(a-0xf9824900u);
    if(w==4&&(o==8u||(o>=0x10u&&o<=0x1cu)||o==0x20u||o==0x24u||o==0x30u||o==0x34u||o==0x38u))return xpl_guard_read32((uintptr_t)a,v);
    if(w==2&&(o==4u||o==6u||o==0x0cu||o==0x2cu||(XPL_EMMC_PROBE_LEVEL>=6&&o==0x3eu)))return xpl_guard_read16((uintptr_t)a,v);
    if(w==1&&(o==0x2eu||o==0x2fu||(XPL_EMMC_PROBE_LEVEL>=6&&(o==0x28u||o==0x29u))))return xpl_guard_read8((uintptr_t)a,v);
    return -1;
}
static int emmc_protocol_write(void *unused,uint64_t a,unsigned w,uint32_t v) {
    (void)unused;
    if(XPL_EMMC_PROBE_LEVEL>=6&&w==4) {
        if(a==0xfc4004d8u||a==0xfc4004dcu||a==0xfc4004e0u||a==0xfc4004d4u)return xpl_guard_write32((uintptr_t)a,v);
        if(a==0xfc4004d0u) {
            uint32_t old;if(xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~1u))return -1;
            return xpl_guard_write32((uintptr_t)a,v);
        }
        if(a==0xf9824000u&&v==(emmc_read_diag.core_power|0x80u))return xpl_guard_write32((uintptr_t)a,v);
        if((a==0xf98240e4u&&(v&~15u)==0u)||a==0xf98240e8u||a==0xf9824a00u||a==0xf9824a0cu)return xpl_guard_write32((uintptr_t)a,v);
    }
    if(XPL_EMMC_PROBE_LEVEL>=3&&a==0xf9824078u&&w==4&&(v==0u||v==1u||(XPL_EMMC_PROBE_LEVEL>=6&&v==0x2001u)))return xpl_guard_write32((uintptr_t)a,v);
    if(XPL_EMMC_PROBE_LEVEL>=4&&a==0xfc401480u&&w==4) {
        uint32_t old;if(xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~16u))return -1;
        return xpl_guard_write32((uintptr_t)a,v);
    }
    if(a<0xf9824900u||a+w>0xf9824a00u||a&(w-1u))return -1;
    unsigned o=(unsigned)(a-0xf9824900u);
    if(w==4&&(o==8u||o==0x30u||o==0x34u||o==0x38u))return xpl_guard_write32((uintptr_t)a,v);
    if(w==2&&(o==4u||o==6u||o==0x0cu||(o==0x2cu&&(v==1u||v==3u||v==7u||(XPL_EMMC_PROBE_LEVEL>=6&&v==0u)))||
       (o==0x0eu&&(v==0xd1au||v==0x83au||v==0x113au||(XPL_EMMC_PROBE_LEVEL>=6&&(v==0u||v==0x102u||v==0x209u||v==0x31au||v==0x71bu||v==0x909u))))||
       (XPL_EMMC_PROBE_LEVEL>=6&&o==0x3eu)))return xpl_guard_write16((uintptr_t)a,v);
    if(w==1&&(o==0x2eu||(o==0x2fu&&(v==6u||(XPL_EMMC_PROBE_LEVEL>=6&&v==1u)))||
       (XPL_EMMC_PROBE_LEVEL>=6&&((o==0x28u&&(v==0u||v==emmc_read_diag.saved_host))||(o==0x29u&&v==emmc.power)))))return xpl_guard_write8((uintptr_t)a,v);
    return -1;
}
static uint64_t emmc_fingerprint(const uint8_t p[512]) {
    uint64_t v=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<512;i++)v=(v^p[i])*UINT64_C(1099511628211);
    return v;
}
static int emmc_media_under_cache(void *unused) {
    (void)unused;if(emmc_result||!emmc.restored)return emmc_result;
    xpl_emmc_reader h;uint8_t b[512];
    const xpl_sd_board_io io={0,emmc_protocol_read,emmc_protocol_write,sd_time,sd_relax};
    emmc_media_result=XPL_EMMC_PROBE_LEVEL>=6?
        xpl_emmc_reader_begin_fresh(&h,&io,&emmc,100000u,&emmc_read_diag):XPL_EMMC_PROBE_LEVEL>=5?
        xpl_emmc_reader_begin_primed(&h,&io,&emmc,100000u,&emmc_read_diag):XPL_EMMC_PROBE_LEVEL>=4?
        xpl_emmc_reader_begin_hc_pll(&h,&io,&emmc,100000u,&emmc_read_diag):XPL_EMMC_PROBE_LEVEL>=3?
        xpl_emmc_reader_begin_hc(&h,&io,&emmc,100000u,&emmc_read_diag):
        xpl_emmc_reader_begin(&h,&io,&emmc,100000u,&emmc_read_diag);
    if(!emmc_media_result) {
        emmc_media_result=xpl_emmc_read512(&h,0u,1u,b);
        if(!emmc_media_result)emmc_first_fingerprint=emmc_fingerprint(b);
        if(!emmc_media_result)emmc_media_result=xpl_emmc_read512(&h,244224u,1u,b);
        if(!emmc_media_result)emmc_boot_fingerprint=emmc_fingerprint(b);
        if(!emmc_media_result)emmc_media_result=xpl_emmc_read512(&h,309760u,1u,b);
        if(!emmc_media_result)emmc_tail_fingerprint=emmc_fingerprint(b);
    }
    if(!emmc_media_result&&XPL_EMMC_PROBE_LEVEL>=7) {
        emmc_fast_result=xpl_emmc_reader_clock_9m6(&h);
        emmc_image_result=emmc_fast_result;
        if(!emmc_fast_result&&free_ram(0x12000000u,33554432u)&&free_ram(0x14000000u,XPL_STUB_OFFSET)) {
            emmc_image_result=xpl_emmc_load_android_v0(&h,XPL_LAB_EMMC_TAIL?309760u:506368u,XPL_LAB_EMMC_TAIL?65536u:131072u,
                (void *)(uintptr_t)0x12000000u,33554432u,&emmc_image_diag);
            if(!emmc_image_result) {
                size_t raw=0,member=0;
                emmc_decode_result=xpl_gzip_decode((const void *)(uintptr_t)(0x12000000u+emmc_image_diag.layout.kernel.offset),
                    (size_t)emmc_image_diag.layout.kernel.bytes,(void *)(uintptr_t)0x14000000u,
                    XPL_STUB_OFFSET,&raw,&member,inflate_progress,0);
                if(!emmc_decode_result) {
                    emmc_raw_bytes=raw;const uint8_t *p=(const void *)(uintptr_t)0x14000000u;
                    uint64_t hash=UINT64_C(14695981039346656037);
                    for(size_t i=0;i<raw;i++) {
                        hash=(hash^p[i])*UINT64_C(1099511628211);
                        if(!(i&65535u)&&sd_time(0)==UINT64_MAX){emmc_decode_result=XPL_GZIP_STOPPED;break;}
                    }
                    emmc_raw_fingerprint=hash;
                }
            }
        }else if(!emmc_fast_result)emmc_image_result=XPL_SD_EBOUNDS;
    }
    int e=xpl_emmc_reader_close(&h);if(!emmc_media_result)emmc_media_result=e;
    return emmc_media_result;
}
static void emmc_media_observe(void) {
    if(emmc_result||!emmc.restored)return;
    int e=xpl_icache_run(XPL_EMMC_PROBE_LEVEL>=7&&XPL_NATIVE_ICACHE,emmc_media_under_cache,0,&emmc_icache);
    if(e)emmc_media_result=e;
}
static int sd_core_read(void *unused,uint64_t a,unsigned w,uint32_t *v) {
    if(w==4&&(a==0xfc400510u||a==0xfc400514u||a==0xfc400518u||a==0xfc40051cu||a==0xfc400520u||
               a==0xfd512048u||a==0xf98a40dcu))return xpl_guard_read32((uintptr_t)a,v);
    return sd_interface_read(unused,a,w,v);
}
static int sd_core_write(void *unused,uint64_t a,unsigned w,uint32_t v) {
    (void)unused;uint32_t old;int r;
    if(w!=4)return -1;
    if(a==0xfc400504u||a==0xfc400508u||a==0xfc400510u||a==0xfc400514u) {
        uint32_t mask=a==0xfc400514u?0x371fu:a==0xfc400510u?0x80000001u:0xa0000001u;
        if(xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~mask))return -1;
    } else if(a==0xfc400518u) {if(v!=1u&&v!=sd_core.before.m)return -1;}
    else if(a==0xfc40051cu) {if(v!=~3u&&v!=sd_core.before.n)return -1;}
    else if(a==0xfc400520u) {if(v!=~4u&&v!=sd_core.before.d)return -1;}
    else if(a==0xf98a4078u) {if(v>1u)return -1;}
    else return -1;
    r=xpl_guard_write32((uintptr_t)a,v);fence(0);return r;
}
static void sd_core_observe(void) {
    /* The pinned E6653 clock table fixes CXO19.2MHz; check S1's independent
     * architectural counter binding as well.019 earned HC/AHB accessibility.
     * This scope enables only internal clocks/mode, with card output stopped. */
    if(sd_interface_result||!sd_interface.restored||timer_frequency!=19200000u)return;
    const xpl_sd_board_io io={0,sd_core_read,sd_core_write,sd_time,sd_relax};
    uint32_t prerequisites=XPL_SD_BOARD_EXCLUSIVE|XPL_SD_BOARD_BUS_SAFE|
        XPL_SD_BOARD_XO_19M2|XPL_SD_BOARD_HC_ACCESSIBLE;
    sd_core_result=xpl_sd_core_clock_probe(&io,prerequisites,100000,&sd_core);
}
static int sd_power_write(void *unused,uint64_t a,unsigned w,uint32_t v) {
    if(a==0xfc4cf810u&&w==4&&(v==0||v==128u)) {
        int r=xpl_guard_write32((uintptr_t)a,v);fence(0);return r;
    }
    if(a==0xfc4cf800u&&w==4&&v==0x0014c450u) {
        int r=xpl_guard_write32((uintptr_t)a,v);fence(0);return r;
    }
    return sd_write(unused,a,w,v);
}
static void sd_power_observe(void) {
    if(sd_core_result||!sd_core.restored)return;
    /* Serialized before any menu key polls. The only extra PMIC write is the
     * dedicated fixed L13 HPM mode bit; voltage/enable/other PMIC writes stay
     * unavailable. Arbitration DENIED is recorded and is never bypassed. */
    const xpl_sd_board_io io={0,sd_read,sd_power_write,sd_time,sd_relax};
    sd_power_result=xpl_sd_mode_probe(&io,1000,&sd_power);
}
static int sd_legacy_read(void *unused,uint64_t a,unsigned w,uint32_t *v) {
    if((w!=1&&w!=2&&w!=4)||(a&(w-1u)))return -1;
    if(a>=0xf98a4900u&&a+w<=0xf98a4a1cu) {
        if(w==1)return xpl_guard_read8((uintptr_t)a,v);
        if(w==2)return xpl_guard_read16((uintptr_t)a,v);
        return xpl_guard_read32((uintptr_t)a,v);
    }
    if(w==4&&(a==0xf98a4000u||a==0xf98a40dcu||a==0xf98a40e0u||a==0xf98a40e4u||a==0xf98a40e8u))
        return xpl_guard_read32((uintptr_t)a,v);
    return sd_core_read(unused,a,w,v);
}
static int sd_legacy_write(void *unused,uint64_t a,unsigned w,uint32_t v) {
    int allowed=0;uint32_t old;
    if(w==4&&a==0xfd512048u) {
        if(xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~0xdfffu))return -1;
        allowed=1;
    } else if(w==4&&a==0xf98a4000u) {
        if(xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~0x80u))return -1;
        allowed=1;
    } else if(w==4&&a==0xf98a4078u)allowed=v==0||v==1u||v==0x2001u;
    else if(w==4&&a==0xf98a40e0u)allowed=v<=15u;
    else if(w==4&&a==0xf98a40e4u)allowed=v<=15u;
    else if(w==4&&a==0xf98a40e8u)allowed=v<=15u;
    else if(w==4&&a==0xf98a4a0cu) {
        if(xpl_guard_read32((uintptr_t)a,&old)||((v^old)&~0x18302u))return -1;
        allowed=1;
    } else if(w==1&&a==0xf98a4928u)allowed=v<=255u;
    else if(w==1&&a==0xf98a4929u)allowed=v==0x0bu;
    else if(w==1&&a==0xf98a492eu)allowed=v==14u;
    else if(w==1&&a==0xf98a492fu)allowed=v==6u;
    else if(w==2&&a==0xf98a492cu)allowed=v==1u||v==3u||v==7u;
    else if(w==2&&a==0xf98a493eu)allowed=(v&0x80cfu)==0&&v<=65535u;
    else if(w==2&&a==0xf98a4904u)allowed=v==512u;
    else if(w==2&&a==0xf98a4906u)allowed=v==1u||(XPL_SD_MULTI_READ&&v>1u&&v<=XPL_SD_MAX_READ_BLOCKS);
    else if(w==4&&a==0xf98a4920u)allowed=(XPL_USB_MSD_WRITABLE&&msd_writable_active)||(XPL_SETTINGS_ENABLED&&settings_io_active);
    else if(w==2&&a==0xf98a490cu)allowed=v==0x12u||(XPL_SD_MULTI_READ&&v==0x36u)||
        (((XPL_USB_MSD_WRITABLE&&msd_writable_active)||(XPL_SETTINGS_ENABLED&&settings_io_active))&&v==2u);
    else if(w==2&&a==0xf98a490eu) {
        unsigned command=v>>8;
        allowed=v<=65535u&&(command==0||command==2||command==3||command==7||command==8||command==9||
            command==13||command==17||(XPL_SD_MULTI_READ&&command==18)||command==41||command==55||
            (((XPL_USB_MSD_WRITABLE&&msd_writable_active)||(XPL_SETTINGS_ENABLED&&settings_io_active))&&command==24));
    } else if(w==4&&(a==0xf98a4908u||a==0xf98a4930u||a==0xf98a4934u))allowed=1;
    else if(w==4&&a==0xf98a4938u)allowed=v==0;
    else return sd_core_write(unused,a,w,v);
    if(!allowed)return -1;
    int r=w==1?xpl_guard_write8((uintptr_t)a,v):w==2?xpl_guard_write16((uintptr_t)a,v):xpl_guard_write32((uintptr_t)a,v);
    fence(0);return r;
}
static int sd_protocol_read(void *u,uint32_t offset,unsigned w,uint32_t *v) {
    if(offset>0x11cu||w>0x11cu-offset)return -1;
    return sd_legacy_read(u,0xf98a4900u+offset,w,v);
}
static int sd_protocol_write(void *u,uint32_t offset,unsigned w,uint32_t v) {
    if(offset>0x11cu||w>0x11cu-offset)return -1;
    return sd_legacy_write(u,0xf98a4900u+offset,w,v);
}
static uint32_t le32(const uint8_t *p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int contains(struct xpl_hw_range r,uint64_t base,uint64_t bytes) {
    return bytes&&r.bytes&&base>=r.base&&base-r.base<=r.bytes&&bytes<=r.bytes-(base-r.base);
}
static int overlaps(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
    return an&&bn&&a<b+bn&&b<a+an;
}
static int free_ram(uint64_t base,uint64_t bytes) {
    unsigned in_ram=0;if(!bytes||base>UINT64_MAX-bytes)return 0;
    for(unsigned i=0;i<hardware.ram_count;i++)if(contains(hardware.ram[i],base,bytes))in_ram=1;
    if(!in_ram)return 0;
    for(unsigned i=0;i<hardware.reserved_count;i++)if(overlaps(base,bytes,hardware.reserved[i].base,hardware.reserved[i].bytes))return 0;
    return !overlaps(base,bytes,kernel_base,XPL_NATIVE_IMAGE_BYTES)&&!overlaps(base,bytes,fdt_pa,fdt_bytes)&&
           !overlaps(base,bytes,initrd.base,initrd.bytes)&&!overlaps(base,bytes,surface.scanout_base_pa,surface.scanout_bytes);
}
static int sd_path(char *out,const char *relative) {
    size_t used=0;
    for(size_t i=0;menu.sd_directory[i];i++) {
        if(used>=XPL_PAYLOAD_PATH_BYTES-2u)return 0;out[used++]=menu.sd_directory[i];
    }
    if(!used||out[used-1]!='/')out[used++]='/';
    while(*relative) {if(used>=XPL_PAYLOAD_PATH_BYTES-1)return 0;out[used++]=*relative++;}
    out[used]=0;return 1;
}
static void string_copy(char *out,size_t capacity,const char *input) {
    size_t i=0;while(i+1u<capacity&&input[i]) {out[i]=input[i];i++;}out[i]=0;
}
static int folder_join(char *out,const char *directory,const char *name) {
    size_t used=0;
    while(*directory) {if(used>=239u)return 0;out[used++]=*directory++;}
    if(!used||out[used-1]!='/')out[used++]='/';
    while(*name) {if(used>=240u)return 0;out[used++]=*name++;}out[used]=0;return 1;
}
static int folder_image_hint(const char *directory) {
    static const char *const names[]={"boot.img","Image.gz","Image","zImage"};
    char path[256];xpl_exfat_node node;
    for(unsigned i=0;i<4;i++) {
        if(!folder_join(path,directory,names[i]))return 0;
        int e=xpl_exfat_lookup(&sd_fs,path,&node);
        if(!e&&!(node.attributes&XPL_EXFAT_ATTR_DIRECTORY))return 1;
        if(e&&e!=XPL_EXFAT_ENOTFOUND&&e!=XPL_EXFAT_ENOTDIR)return -1;
    }
    return 0;
}
static void folder_request(const char *path) {
    string_copy(sd_browse_path,sizeof sd_browse_path,path);
    sd_folders[0]=(xpl_menu_folder_entry){0};string_copy(sd_folders[0].label,sizeof sd_folders[0].label,"< Back");
    sd_folder_count=1;sd_picker_request=sd_picker_operation=1;sd_picker_result=XPL_EXFAT_EIO;
    xpl_menu_show_folders(&menu,sd_browse_path,sd_folders,1,0,1,folder_select,0);
}
static void folder_open(void *unused) {(void)unused;folder_request("/");}
static void folder_select(void *unused,unsigned selected) {
    (void)unused;if(selected>=sd_folder_count)return;
    xpl_menu_folder_entry *entry=sd_folders+selected;
    if(!entry->kind) {
        if(sd_browse_path[0]=='/'&&!sd_browse_path[1]) {
            menu.page=XPL_MENU_BOOT_SETTINGS_PAGE;menu.selected=2;menu.folder_scanning=0;menu.dirty=1;
        }else folder_request(entry->path);
    }else if(entry->kind==1u)folder_request(entry->path);
    else {
        if(XPL_SETTINGS_ENABLED)settings_request(0,(unsigned)menu.startup,(unsigned)menu.quick_target,entry->path);
        else string_copy(menu.sd_directory,sizeof menu.sd_directory,entry->path);
        menu.page=XPL_MENU_MAIN;menu.selected=XPL_MENU_BOOT_SD;menu.folder_scanning=0;
        menu.notice=XPL_SETTINGS_ENABLED?XPL_MENU_NOTICE_SETTINGS_SAVE:XPL_MENU_NOTICE_NONE;menu.dirty=1;
    }
}
static int sd_list_folders(void) {
    xpl_exfat_node directory,node;xpl_exfat_dir iterator;
    int e=xpl_exfat_lookup(&sd_fs,sd_browse_path,&directory);
    if(e)return e;
    e=xpl_exfat_dir_open(&sd_fs,&iterator,&directory);if(e)return e;
    sd_folders[0]=(xpl_menu_folder_entry){0};string_copy(sd_folders[0].label,128,"< Back");
    string_copy(sd_folders[0].path,256,sd_browse_path);
    size_t length=0;while(sd_folders[0].path[length])length++;
    if(length>1u) {
        while(length>1u&&sd_folders[0].path[length-1]!='/')length--;
        if(length>1u)length--;sd_folders[0].path[length]=0;
    }
    sd_folders[1]=(xpl_menu_folder_entry){0};sd_folders[1].kind=2;
    string_copy(sd_folders[1].label,128,"< Select this folder >");string_copy(sd_folders[1].path,256,sd_browse_path);
    sd_folder_count=2;
    while(!(e=xpl_exfat_dir_next(&iterator,&node))) {
        if(!(node.attributes&XPL_EXFAT_ATTR_DIRECTORY))continue;
        if(sd_folder_count>=XPL_MENU_FOLDER_MAX)return XPL_EXFAT_ELIMIT;
        xpl_menu_folder_entry *entry=sd_folders+sd_folder_count;*entry=(xpl_menu_folder_entry){0};
        if(!folder_join(entry->path,sd_browse_path,node.name))return XPL_EXFAT_ELIMIT;
        string_copy(entry->label,128,node.name);
        int hint=folder_image_hint(entry->path);if(hint<0)return XPL_EXFAT_EIO;
        entry->kind=hint?2u:1u;sd_folder_count++;
    }
    return e==XPL_EXFAT_END?0:e;
}
static enum xpl_payload_fs_result sd_stat_file(void *unused,const char *relative,uint64_t *bytes) {
    (void)unused;char path[XPL_PAYLOAD_PATH_BYTES];xpl_exfat_node node;
    if(!sd_path(path,relative))return XPL_PAYLOAD_FS_ERROR;
    int e=xpl_exfat_lookup(&sd_fs,path,&node);
    if(e==XPL_EXFAT_ENOTFOUND)return XPL_PAYLOAD_FS_MISSING;
    if(e||node.attributes&0x10u)return XPL_PAYLOAD_FS_ERROR;
    *bytes=node.data_length;return XPL_PAYLOAD_FS_FILE;
}
static int sd_load_component(const struct xpl_payload_file *file,uint64_t destination,uint64_t limit) {
    if(!file->present||!file->bytes||file->bytes>limit||!free_ram(destination,file->bytes))return XPL_EXFAT_EBOUNDS;
    char path[XPL_PAYLOAD_PATH_BYTES];xpl_exfat_node node;size_t got;
    if(!sd_path(path,file->path))return XPL_EXFAT_EBOUNDS;
    int e=xpl_exfat_lookup(&sd_fs,path,&node);
    if(e||node.data_length!=file->bytes)return e?e:XPL_EXFAT_EFORMAT;
    e=xpl_exfat_pread(&sd_fs,&node,0,(void *)(uintptr_t)destination,(size_t)file->bytes,&got);
    return e?e:got!=file->bytes?XPL_EXFAT_EIO:0;
}
static int sd_describe_payload(void) {
    uint8_t startup[XPL_PAYLOAD_CONFIG_MAX];size_t bytes=0,got=0;char path[XPL_PAYLOAD_PATH_BYTES];xpl_exfat_node node;
    struct xpl_payload_config config;struct xpl_payload_inventory inventory;struct xpl_payload_error error;
    if(!sd_path(path,"startup.txt"))return XPL_EXFAT_EBOUNDS;
    int e=xpl_exfat_lookup(&sd_fs,path,&node);
    if(e!=XPL_EXFAT_ENOTFOUND) {
        if(e||node.data_length>sizeof startup)return XPL_EXFAT_EFORMAT;
        bytes=(size_t)node.data_length;
        e=xpl_exfat_pread(&sd_fs,&node,0,startup,bytes,&got);
        if(e||got!=bytes)return e?e:XPL_EXFAT_EIO;
    }
    if(xpl_payload_parse_startup(startup,bytes,&config,&error)||
       xpl_payload_discover(sd_stat_file,0,&inventory,&error)||
       xpl_payload_select(&config,XPL_PAYLOAD_AUTO,&inventory,sd_stat_file,0,&sd_payload,&error)||
       (sd_payload.abi!=XPL_PAYLOAD_ABI_UNSPECIFIED&&sd_payload.abi!=XPL_PAYLOAD_ABI_LINUX_AARCH64)) {
        return XPL_EXFAT_EFORMAT;
    }
    return 0;
}
static void sd_folder_observe(void) {
    sd_ram_result=sd_describe_payload();if(sd_ram_result)return;
    sd_ram_bytes=sd_payload.kernel.bytes;
    sd_ram_result=sd_load_component(&sd_payload.kernel,0x12000000u,33554432u);
    if(sd_ram_result)return;
    for(unsigned i=0;i<XPL_PAYLOAD_CMDLINE_BYTES;i++)payload_cmdline[i]=sd_payload.cmdline[i];
    if(sd_payload.mode==XPL_PAYLOAD_SPLIT) {
        sd_ram_result=sd_load_component(&sd_payload.initrd,0x18000000u,16777216u);
        if(sd_ram_result)return;
        payload_initrd_source=0x18000000u;payload_initrd_bytes=sd_payload.initrd.bytes;
        sd_ram_result=sd_load_component(&sd_payload.dtb,0x19000000u,2097152u);
        if(sd_ram_result)return;
        payload_dtb_source=0x19000000u;payload_dtb_bytes=sd_payload.dtb.bytes;
    }
    uint64_t before_hash,after_hash;
    if(now_us(&before_hash)) {sd_ram_result=XPL_EXFAT_EIO;return;}
    const uint8_t *p=(const void *)(uintptr_t)0x12000000u;uint64_t hash=UINT64_C(0xcbf29ce484222325);
    for(uint64_t i=0;i<sd_ram_bytes;i++) {
        hash=(hash^p[i])*UINT64_C(0x100000001b3);
        if(!(i&65535u)&&sd_time(0)==UINT64_MAX) {sd_ram_result=XPL_EXFAT_EIO;return;}
    }
    sd_ram_fingerprint=hash;
    if(now_us(&after_hash)||after_hash<before_hash) {sd_ram_result=XPL_EXFAT_EIO;return;}
    sd_input_hash_us=after_hash-before_hash;
}
static void sd_filesystem_observe(void) {
    if(sd_sector[510]!=0x55u||sd_sector[511]!=0xaau) {sd_fs_result=XPL_EXFAT_EFORMAT;return;}
    for(unsigned i=0;i<4;i++) {
        const uint8_t *p=sd_sector+446u+16u*i;
        uint32_t first=le32(p+8),count=le32(p+12);
        if(p[4]!=7u||!first||count<24u||first>=sd_host.blocks512||count>sd_host.blocks512-first)continue;
        sd_partition_start=first;sd_partition_sectors=count;
        sd_fs_result=xpl_exfat_mount(&sd_fs,XPL_SD_MULTI_READ?xpl_sd_read512_multiblock:xpl_sd_read512,&sd_host,first,count);
        if(!sd_fs_result)break;
    }
    if(sd_fs_result)return;
    if(XPL_SD_MULTI_READ) {
        sd_fs_result=xpl_exfat_set_read_limit(&sd_fs,XPL_SD_MAX_READ_BLOCKS);
        if(sd_fs_result)return;
    }
    if(settings_operation){settings_file_io();return;}
    if(tool_metadata_active){tool_result=tool_selected==XPL_TOOL_PAYLOAD?sd_describe_payload():0;return;}
    if(pcsd_remount_requested){
        xpl_exfat_node node;uint8_t chunk[512];size_t got=0;
        pcsd_remount_result=xpl_exfat_lookup(&sd_fs,XPL_PC_SD_TEST_PATH,&node);
        if(pcsd_remount_result)return;
        pcsd_remount_bytes=node.data_length;
        if(node.data_length!=8192u){pcsd_remount_result=XPL_EXFAT_ELIMIT;return;}
        uint64_t hash=UINT64_C(0xcbf29ce484222325);
        for(uint64_t offset=0;offset<8192u;offset+=512u){
            pcsd_remount_result=xpl_exfat_pread(&sd_fs,&node,offset,chunk,512,&got);
            if(pcsd_remount_result||got!=512){if(!pcsd_remount_result)pcsd_remount_result=XPL_EXFAT_EIO;return;}
            for(unsigned n=0;n<512;n++)hash=(hash^chunk[n])*UINT64_C(0x100000001b3);
        }
        pcsd_remount_fingerprint=hash;return;
    }
    if(sd_picker_operation) {sd_picker_result=sd_list_folders();return;}
    if(load_target==XPL_MENU_BOOT_SD) {sd_folder_observe();return;}
    xpl_exfat_node node;uint8_t chunk[512];size_t got;
    sd_file_result=xpl_exfat_lookup(&sd_fs,"/xploader-probe-026.bin",&node);
    if(!sd_file_result) {
        sd_file_bytes=node.data_length;
        if(node.data_length>65536u)sd_file_result=XPL_EXFAT_ELIMIT;
        else {
            uint64_t hash=UINT64_C(0xcbf29ce484222325),offset=0;
            while(offset<node.data_length) {
                size_t bytes=node.data_length-offset>sizeof chunk?sizeof chunk:(size_t)(node.data_length-offset);
                sd_file_result=xpl_exfat_pread(&sd_fs,&node,offset,chunk,bytes,&got);
                if(sd_file_result||got!=bytes) {if(!sd_file_result)sd_file_result=XPL_EXFAT_EIO;break;}
                for(size_t i=0;i<got;i++)hash=(hash^chunk[i])*UINT64_C(0x100000001b3);
                offset+=got;
            }
            if(!sd_file_result)sd_file_fingerprint=hash;
        }
    }
    const char *recovery_path=lab_auto_triggered?
        XPL_LAB_RECOVERY_PATH:"/twrp.img";
    sd_twrp_result=xpl_exfat_lookup(&sd_fs,recovery_path,&node);
    if(!sd_twrp_result) {
        sd_twrp_bytes=node.data_length;
        sd_twrp_result=xpl_exfat_pread(&sd_fs,&node,0,chunk,64,&got);
        if(!sd_twrp_result&&got!=64)sd_twrp_result=XPL_EXFAT_EIO;
        if(!sd_twrp_result) {
            uint64_t hash=UINT64_C(0xcbf29ce484222325);
            for(unsigned i=0;i<64;i++)hash=(hash^chunk[i])*UINT64_C(0x100000001b3);
            sd_twrp_header_fingerprint=hash;
        }
        if(XPL_SD_PROBE_LEVEL>=6&&!sd_twrp_result) {
            sd_ram_bytes=node.data_length;
            if(node.data_length>33554432u||!free_ram(0x12000000u,node.data_length))sd_ram_result=XPL_EXFAT_EBOUNDS;
            else {
                uint8_t *p=(uint8_t *)(uintptr_t)0x12000000u;
                sd_ram_result=xpl_exfat_pread(&sd_fs,&node,0,p,(size_t)node.data_length,&got);
                if(!sd_ram_result&&got!=node.data_length)sd_ram_result=XPL_EXFAT_EIO;
                if(!sd_ram_result) {
                    uint64_t hash=UINT64_C(0xcbf29ce484222325);
                    for(size_t i=0;i<got;i++) {
                        hash=(hash^p[i])*UINT64_C(0x100000001b3);
                        if(!(i&65535u)&&sd_time(0)==UINT64_MAX) {sd_ram_result=XPL_EXFAT_EIO;break;}
                    }
                    sd_ram_fingerprint=hash;
                }
            }
        }
    }
}
static int inflate_progress(void *unused) {
    (void)unused;return sd_time(0)==UINT64_MAX;
}
static void sd_inflate_observe(void) {
    if(sd_ram_result||!sd_ram_bytes||!free_ram(0x14000000u,XPL_STUB_OFFSET))return;
    const uint8_t *image=(const void *)(uintptr_t)0x12000000u;
    struct xpl_payload_error error;
    if(load_target==XPL_MENU_TWRP||sd_payload.mode==XPL_PAYLOAD_BOOTIMG) {
        if(sd_ram_bytes<4096||xpl_payload_parse_android_v0(image,4096,sd_ram_bytes,&twrp_layout,&error)||twrp_layout.second.bytes)return;
        payload_kernel_source=0x12000000u+twrp_layout.kernel.offset;payload_kernel_bytes=twrp_layout.kernel.bytes;
        payload_initrd_source=0x12000000u+twrp_layout.ramdisk.offset;payload_initrd_bytes=twrp_layout.ramdisk.bytes;
        if(load_target==XPL_MENU_TWRP||!sd_payload.cmdline_present) {
            memcpy(payload_cmdline,image+64,512);
            memcpy(payload_cmdline+512,image+608,1024);
            payload_cmdline[1536]=0;
        }
    }else {payload_kernel_source=0x12000000u;payload_kernel_bytes=sd_ram_bytes;}
    size_t raw=0,member=0;uint64_t decode_start,decode_end;
    if(now_us(&decode_start)) {sd_inflate_result=XPL_GZIP_STOPPED;return;}
    sd_inflate_result=xpl_gzip_decode((const void *)(uintptr_t)payload_kernel_source,(size_t)payload_kernel_bytes,
        (void *)(uintptr_t)0x14000000u,XPL_STUB_OFFSET,&raw,&member,inflate_progress,0);
    if(now_us(&decode_end)||decode_end<decode_start) {sd_inflate_result=XPL_GZIP_STOPPED;return;}
    sd_decode_us=decode_end-decode_start;
    if(sd_inflate_result)return;
    sd_raw_bytes=raw;sd_gzip_member_bytes=member;
    if(load_target==XPL_MENU_BOOT_SD&&sd_payload.mode==XPL_PAYLOAD_BOOTIMG&&payload_kernel_bytes>member) {
        payload_dtb_source=payload_kernel_source+member;payload_dtb_bytes=payload_kernel_bytes-member;
    }
    const uint8_t *p=(const void *)(uintptr_t)0x14000000u;
    if(raw<68||le32(p+56)!=0x644d5241u||le32(p+64)!=XPL_REPLAY_INSN||
       ((uint64_t)le32(p+8)|((uint64_t)le32(p+12)<<32))!=XPL_TEXT_OFFSET) {sd_inflate_result=XPL_GZIP_FORMAT;return;}
    uint64_t before_hash,after_hash;
    if(now_us(&before_hash)) {sd_inflate_result=XPL_GZIP_STOPPED;return;}
    uint64_t hash=UINT64_C(0xcbf29ce484222325);
    for(size_t i=0;i<raw;i++) {
        hash=(hash^p[i])*UINT64_C(0x100000001b3);
        if(!(i&65535u)&&inflate_progress(0)) {sd_inflate_result=XPL_GZIP_STOPPED;return;}
    }
    sd_raw_fingerprint=hash;
    if(now_us(&after_hash)||after_hash<before_hash) {sd_inflate_result=XPL_GZIP_STOPPED;return;}
    sd_output_hash_us=after_hash-before_hash;
}
static int sd_media_under_clock(void *unused) {
    (void)unused;xpl_sd_ldo_state a,b;xpl_sd_prepared prepared;
    const xpl_sd_board_io board={0,sd_legacy_read,sd_legacy_write,sd_time,sd_relax};
    const xpl_sd_board_io pmic={0,sd_read,sd_power_write,sd_time,sd_relax};
    int e=xpl_sd_board_pmic_snapshot(&pmic,hardware.spmi.base,(uint32_t)hardware.spmi.bytes,0,0,1000,&a,&b);
    if(e)return e;
    e=xpl_sd_prepare_legacy_owned(&board,1000000,&a,&b,sd_owned_power,&prepared,&sd_legacy);
    if(!e) {
        const xpl_sdhci_io protocol={0,sd_protocol_read,sd_protocol_write,sd_time,sd_relax};
        e=xpl_sdhci_bind(&sd_host,&protocol,XPL_SDHC2_BASE,XPL_SDHC2_SPAN,1000000);
        if(!e)e=xpl_sd_initialize(&sd_host,&prepared);
        sd_init_result=e;
        if(!e) {
            if(XPL_SD_PROBE_LEVEL>=6) {
                sd_fast_result=xpl_sd_clock_9m6(&board,1000000,sd_host.max_clock_hz,&sd_fast);
                if(sd_fast_result)e=sd_fast_result;
            }
        }
        if(!e) {
            e=xpl_sd_read512(&sd_host,0,1,sd_sector);sd_lba_result=e;
            if(!e) {
                uint64_t hash=UINT64_C(0xcbf29ce484222325);
                for(unsigned i=0;i<512;i++)hash=(hash^sd_sector[i])*UINT64_C(0x100000001b3);
                sd_lba_fingerprint=hash;
                if(msd_requested)msd_result=run_msd_session();
                else if(XPL_SD_PROBE_LEVEL>=5)sd_filesystem_observe();
            }
        }
    }
    int cleanup=xpl_sd_cleanup_legacy(&board,1000000,&sd_legacy);
    return e?e:cleanup;
}
static int sd_media_under_hpm(void *unused) {
    (void)unused;
    const xpl_sd_board_io io={0,sd_legacy_read,sd_legacy_write,sd_time,sd_relax};
    uint32_t pre=XPL_SD_BOARD_EXCLUSIVE|XPL_SD_BOARD_BUS_SAFE|XPL_SD_BOARD_XO_19M2|XPL_SD_BOARD_HC_ACCESSIBLE;
    if(sd_owned_power)pre|=XPL_SD_BOARD_NATIVE_WARM_POWER;
    sd_core_result=xpl_sd_with_core_clock(&io,pre,100000,sd_media_under_clock,0,&sd_core);
    return sd_core_result;
}
static void sd_media_observe(void) {
    if(sd_interface_result||!sd_interface.restored||timer_frequency!=19200000u)return;
    uint64_t start,end,read_end;if(now_us(&start))return;
    const xpl_sd_board_io io={0,sd_read,sd_power_write,sd_time,sd_relax};
    sd_power_result=xpl_sd_with_hpm(&io,1000,sd_media_under_hpm,0,&sd_power);
    if(now_us(&read_end)||read_end<start) {sd_media_result=XPL_SD_ETIMEOUT;return;}
    sd_read_phase_us=read_end-start;
    sd_media_result=sd_power_result;
    if(!sd_media_result&&!sd_init_result&&!sd_lba_result&&sd_core.restored&&sd_power.restored)
        sd_owned_power=1;
    if(XPL_SD_PROBE_LEVEL>=7&&!sd_picker_operation)sd_inflate_observe();
    if(!now_us(&end)&&end>=start)sd_media_us=end-start;
}

static int msd_read_sector(void *context,uint64_t lba,uint8_t out[512]) {
    return xpl_sd_read512(context,lba,1,out);
}
static void settings_request(void *unused,unsigned mode,unsigned target,const char *directory){
    (void)unused;pending_settings=boot_settings;pending_settings.startup=mode;pending_settings.target=target;
    string_copy(pending_settings.directory,sizeof pending_settings.directory,directory);settings_requested=1;
}
static void language_request(void *unused,const char *language){
    (void)unused;pending_settings=boot_settings;
    string_copy(pending_settings.language,sizeof pending_settings.language,language);settings_requested=1;
}
static void defaults_request(void *unused){(void)unused;xpl_settings_default(&pending_settings);settings_requested=1;}
static void info_line(const char *text){
    if(tool_line_count<12)string_copy(tool_lines[tool_line_count++],96,text);
}
static void info_number(const char *label,uint64_t value,unsigned hex){
    if(tool_line_count>=12)return;char *out=tool_lines[tool_line_count++];unsigned at=0;char digits[24];unsigned n=0;
    while(*label&&at<70)out[at++]=*label++;
    do{digits[n++]="0123456789abcdef"[value%(hex?16u:10u)];value/=hex?16u:10u;}while(value&&n<sizeof digits);
    if(hex){out[at++]='0';out[at++]='x';}while(n&&at<95)out[at++]=digits[--n];out[at]=0;
}
static void tool_request(void *unused,enum xpl_menu_tool selected){
    (void)unused;tool_selected=selected;tool_requested=1;tool_line_count=0;info_line("Reading information...");
    xpl_menu_show_info(&menu,selected==XPL_TOOL_PAYLOAD?"Payload Info":selected==XPL_TOOL_STORAGE?"Storage Info":
        selected==XPL_TOOL_LOG?"Boot Log":"About",tool_lines,tool_line_count,selected==XPL_TOOL_ABOUT?6u:(unsigned)selected+1u);
}
static void system_request(enum xpl_menu_action selected){
    power_requested=selected==XPL_MENU_POWEROFF?2u:1u;tool_line_count=0;
    info_line(xpl_locale_message(menu.language,selected==XPL_MENU_POWEROFF?"powering_off":"resetting"));
    xpl_menu_show_info(&menu,selected==XPL_MENU_POWEROFF?"PowerOff":"Reset",tool_lines,1,0);
}
static void show_tool_info(void){
    if(tool_selected==XPL_TOOL_PAYLOAD||tool_selected==XPL_TOOL_STORAGE){
        tool_metadata_active=1;sd_picker_operation=6;sd_media_observe();sd_picker_operation=tool_metadata_active=0;
    }
    tool_line_count=0;
    if(tool_selected==XPL_TOOL_PAYLOAD){
        info_line("Selected SD directory:");info_line(menu.sd_directory);
        int e=sd_media_result?sd_media_result:sd_fs_result?sd_fs_result:tool_result;
        if(e)info_number("Read/format error: ",(uint32_t)e,1);
        else {
            info_line(sd_payload.mode==XPL_PAYLOAD_SPLIT?"Format: split kernel/initrd/DTB":"Format: Android boot image");
            info_line("Kernel file:");info_line(sd_payload.kernel.path);info_number("Bytes: ",sd_payload.kernel.bytes,0);
            if(sd_payload.mode==XPL_PAYLOAD_SPLIT){info_line(sd_payload.initrd.path);info_number("Initrd bytes: ",sd_payload.initrd.bytes,0);info_line(sd_payload.dtb.path);info_number("DTB bytes: ",sd_payload.dtb.bytes,0);}
            info_line("Handoff: Linux AArch64 entry");
        }
    }else if(tool_selected==XPL_TOOL_STORAGE){
        info_number("SD size (MiB): ",sd_host.blocks512/2048u,0);info_number("SD media result: ",(uint32_t)sd_media_result,1);
        info_number("exFAT result: ",(uint32_t)sd_fs_result,1);info_number("SD partition LBA: ",sd_partition_start,0);
        info_number("CID register 0: ",sd_host.cid[0],1);info_number("CID register 1: ",sd_host.cid[1],1);
        info_number("CID register 2: ",sd_host.cid[2],1);info_number("CID register 3: ",sd_host.cid[3],1);
        info_line("PC access exports SD only");info_line("eMMC OS boot uses its own GUID");
    }else if(tool_selected==XPL_TOOL_LOG){
        info_line("Current loader session");info_number("Stage: ",stage,0);info_number("Result: ",result,1);
        info_number("SD media: ",(uint32_t)sd_media_result,1);info_number("exFAT: ",(uint32_t)sd_fs_result,1);
        info_number("Payload: ",(uint32_t)sd_ram_result,1);info_number("Decode: ",(uint32_t)sd_inflate_result,1);
        info_number("Settings read: ",(uint32_t)settings_load_result,1);info_number("Settings save: ",(uint32_t)settings_save_result,1);
        info_number("Settings generation: ",boot_settings.sequence,0);info_number("Read time (us): ",sd_media_us,0);
        info_number("Power request: ",(uint32_t)power_result,1);
    }else {
        info_line("xploader");info_line(XPL_BUILD_LABEL);info_line("Sony Xperia Z5 / E6653");info_line("MAGLDR / HTC HD2 tribute");
        info_line("Noto Sans TC glyphs / OFL 1.1");info_line("Experimental native manager");
        info_line("Volume +/- and Power keys");info_line("Startup Power holds the menu");
    }
    xpl_menu_show_info(&menu,tool_selected==XPL_TOOL_PAYLOAD?"Payload Info":tool_selected==XPL_TOOL_STORAGE?"Storage Info":
        tool_selected==XPL_TOOL_LOG?"Boot Log":"About",tool_lines,tool_line_count,tool_selected==XPL_TOOL_ABOUT?6u:(unsigned)tool_selected+1u);
}
static int power_wait(void *unused,unsigned us){
    (void)unused;uint64_t start,at;if(now_us(&start))return -1;
    for(unsigned n=0;n<1000000;n++){
        if(now_us(&at)||at<start)return -1;if(at-start>=us)return 0;
        if(pet_watchdog(at/1000))return -1;
    }return -1;
}
static void service_lab_fail(unsigned code){
    lab_service_result=code;lab_service_phase=7;lab_service_active=0;lab_auto_triggered=1;
    (void)action(0,XPL_MENU_TWRP);record();
}
/* Actual native hardware callback tests. These do not claim physical clicks.
 * Kind2 resets only after one verified config generation increment. A fresh
 * entry with that exact generation skips the test and boots ordinary TWRP. */
static void service_lab_tick(void){
    if(!lab_service_active)return;
    uint64_t at;if(now_us(&at)){service_lab_fail(1);return;}
    if(key_bits){lab_service_active=0;lab_service_phase=10;record();return;}
    if(!lab_service_start_us)lab_service_start_us=at;
    if(at-lab_service_start_us>45000000u){service_lab_fail(2);return;}
    if(menu.dirty||settings_requested||tool_requested||power_requested)return;
    if(!lab_service_phase){
        if(at-lab_service_start_us<5000000u)return;
        lab_service_phase=1;
        if(XPL_LAB_SERVICE_KIND==1)language_request(0,"zh-TW");
        else settings_request(0,boot_settings.startup,boot_settings.target,boot_settings.directory);
        return;
    }
    if(lab_service_phase==1){
        if(settings_save_result||boot_settings.sequence!=lab_service_entry_sequence+1u){service_lab_fail(3);return;}
        if(XPL_LAB_SERVICE_KIND==2){lab_service_phase=2;system_request(XPL_MENU_RESET);return;}
        if(!same((const uint8_t *)boot_settings.language,16,"zh-TW")){service_lab_fail(4);return;}
        lab_service_flags|=1u;lab_service_phase=2;tool_request(0,XPL_TOOL_ABOUT);return;
    }
    if(XPL_LAB_SERVICE_KIND==2){if(power_result<0)service_lab_fail(5);return;}
    if(lab_service_phase==2){
        if(menu.page!=XPL_MENU_INFO_PAGE||tool_line_count<5){service_lab_fail(6);return;}
        lab_service_flags|=2u;lab_service_phase=3;tool_request(0,XPL_TOOL_STORAGE);return;
    }
    if(lab_service_phase==3){
        if(sd_media_result||sd_fs_result||!sd_host.blocks512){service_lab_fail(7);return;}
        lab_service_sd_blocks=sd_host.blocks512;lab_service_flags|=4u;lab_service_phase=4;tool_request(0,XPL_TOOL_PAYLOAD);return;
    }
    if(lab_service_phase==4){
        if(sd_media_result||sd_fs_result||tool_result||!sd_payload.kernel.bytes){service_lab_fail(8);return;}
        lab_service_payload_bytes=sd_payload.kernel.bytes;lab_service_flags|=8u;lab_service_phase=5;
        language_request(0,lab_service_original_locale);return;
    }
    if(lab_service_phase==5){
        if(settings_save_result||boot_settings.sequence!=lab_service_entry_sequence+2u||
           !same((const uint8_t *)boot_settings.language,16,lab_service_original_locale)){service_lab_fail(9);return;}
        lab_service_flags|=16u;lab_service_result=0;lab_service_phase=6;lab_service_active=0;lab_auto_triggered=1;
        (void)action(0,XPL_MENU_TWRP);record();
    }
}
static void settings_file_io(void){
    xpl_exfat_node node;size_t got=0;int e=xpl_exfat_lookup(&sd_fs,XPL_SETTINGS_PATH,&node);
    if(!e&&(node.data_length!=1024u||node.valid_data_length!=1024u))e=XPL_EXFAT_EFORMAT;
    for(unsigned slot=0;slot<2&&!e;slot++)e=xpl_exfat_file_sector(&sd_fs,&node,slot*512u,&settings_lba[slot]);
    if(!e&&settings_lba[0]==settings_lba[1])e=XPL_EXFAT_EFORMAT;
    if(!e)e=xpl_exfat_pread(&sd_fs,&node,0,settings_records,sizeof settings_records,&got);
    if(!e&&got!=sizeof settings_records)e=XPL_EXFAT_EIO;
    xpl_settings current;unsigned current_slot=0;int valid=e?-1:xpl_settings_choose(settings_records,&current,&current_slot);
    if(settings_operation==1){
        settings_load_result=e?e:valid;
        if(!settings_load_result){boot_settings=current;settings_slot=current_slot;}return;
    }
    settings_save_result=e;if(e)return;
    if(valid==-2){settings_save_result=XPL_EXFAT_EFORMAT;return;}
    if(valid)xpl_settings_default(&current);
    if(current.sequence==UINT64_MAX){settings_save_result=XPL_EXFAT_ELIMIT;return;}
    pending_settings.sequence=current.sequence+1u;settings_write_slot=valid?0u:current_slot^1u;
    uint8_t *next=settings_records+settings_write_slot*512u;
    if(xpl_settings_encode(&pending_settings,next)){settings_save_result=XPL_EXFAT_EFORMAT;return;}
    settings_io_active=1;
    settings_save_result=xpl_sd_write512(&sd_host,settings_lba[settings_write_slot],1,next);
    settings_io_active=0;if(settings_save_result)return;
    sd_fs.cache_valid=sd_fs.bitmap_cache_valid=0;
    settings_save_result=xpl_sd_read512(&sd_host,settings_lba[settings_write_slot],1,settings_verify);
    if(settings_save_result)return;
    for(unsigned n=0;n<512;n++)if(next[n]!=settings_verify[n]){settings_save_result=XPL_EXFAT_EIO;return;}
    if(xpl_settings_decode(settings_verify,512,&current)){settings_save_result=XPL_EXFAT_EFORMAT;return;}
    boot_settings=current;settings_slot=settings_write_slot;settings_saves++;
}
static int msd_write_sector(void *context,uint64_t lba,const uint8_t data[512]) {
    if(!XPL_USB_MSD_WRITABLE||!msd_writable_active)return -1;
    return xpl_sd_write512(context,lba,1,data);
}
static int run_msd_session(void) {
    msd_finished=1;msd_sectors=sd_host.blocks512;
    sd_fs.mounted=sd_fs.cache_valid=sd_fs.bitmap_cache_valid=0;
    for(unsigned n=0;n<4;n++)msd_cid[n]=sd_host.cid[n];
    if(!sd_host.card_ready||!sd_host.bound||!free_ram(0x1a000000u,8192u)||
       xpl_usb_msd_init(&msd_device,msd_sectors,&sd_host,msd_read_sector))return -1;
    if(XPL_USB_MSD_WRITABLE&&xpl_usb_msd_enable_write(&msd_device,msd_write_sector))return -1;
    const struct xpl_hw_io guard_io={0,io_read,io_write,fence};
    usb_guard_result=xpl_usb_guard_arm(&hardware,&guard_io,&usb_guard_diag);record();
    if(usb_guard_result)return -30;
    char serial[25]="XPL-";static const char digits[]="0123456789abcdef";
    for(unsigned n=0;n<8;n++){serial[4+2*n]=digits[package_id[n]>>4];serial[5+2*n]=digits[package_id[n]&15u];}serial[20]=0;
    watchdog_enabled=1;watchdog_last_pet=0;
    const xpl_sd_board_io usb_io={0,usb_enum_read,usb_enum_write,sd_time,0};
    msd_writable_active=XPL_USB_MSD_WRITABLE;
    usb_enum_result=xpl_usb_msd_probe(&usb_io,(void *)(uintptr_t)0x1a000000u,0x1a000000u,8192u,serial,&msd_device,&usb_enum_diag);
    msd_writable_active=0;
    sd_fs.mounted=sd_fs.cache_valid=sd_fs.bitmap_cache_valid=0;
    if(usb_enum_diag.power.cleanup_result){record();for(;;)__asm__ volatile("yield");}
    usb_guard_result=xpl_usb_guard_disarm(&guard_io,&usb_guard_diag);
    watchdog_enabled=usb_guard_diag.old_en;uint64_t restored=0;if(!now_us(&restored))watchdog_last_pet=restored/1000;
    return usb_enum_result;
}

static int payload_slot(uint64_t base,uint64_t bytes) {
    unsigned in_ram=0;if(!bytes||base>UINT64_MAX-bytes)return 0;
    for(unsigned i=0;i<hardware.ram_count;i++)if(contains(hardware.ram[i],base,bytes))in_ram=1;
    if(!in_ram)return 0;
    for(unsigned i=0;i<hardware.reserved_count;i++)if(overlaps(base,bytes,hardware.reserved[i].base,hardware.reserved[i].bytes))return 0;
    return !overlaps(base,bytes,surface.scanout_base_pa,surface.scanout_bytes);
}
static int prepare_twrp(void) {
    if(sd_media_result||sd_ram_result||sd_inflate_result||!sd_ram_bytes||!sd_raw_bytes)
        return 0xb1;
    const uint8_t *raw=(const void *)(uintptr_t)0x14000000u;
    if(!payload_initrd_source||!payload_initrd_bytes)return 0xb2;
    /* This first adapter reuses the earned S1->Linux sttext continuation.
     * Check its instruction ABI, not a model/hash whitelist. */
    if(sd_raw_bytes<68u||sd_raw_bytes>XPL_STUB_OFFSET||le32(raw)!=0x14000010u||
       le32(raw+56)!=0x644d5241u||le32(raw+64)!=XPL_REPLAY_INSN||
       ((uint64_t)le32(raw+8)|((uint64_t)le32(raw+12)<<32))!=XPL_TEXT_OFFSET||
       !payload_slot(kernel_base,sd_raw_bytes))return 0xb3;
    uint64_t image_size=(uint64_t)le32(raw+16)|((uint64_t)le32(raw+20)<<32);
    if(image_size&&image_size!=sd_raw_bytes)return 0xb3;
    if(!free_ram(0x16000000u,1048576u)||!payload_slot(initrd.base,payload_initrd_bytes)||
       overlaps(initrd.base,payload_initrd_bytes,kernel_base,XPL_NATIVE_IMAGE_BYTES)||
       overlaps(initrd.base,payload_initrd_bytes,0x12000000u,sd_ram_bytes)||
       overlaps(initrd.base,payload_initrd_bytes,0x14000000u,XPL_STUB_OFFSET)||
       overlaps(initrd.base,payload_initrd_bytes,0x16000000u,1048576u)||
       overlaps(initrd.base,payload_initrd_bytes,payload_initrd_source,payload_initrd_bytes))return 0xb4;
    int e=xpl_fdt_payload_prepare((const void *)(uintptr_t)fdt_pa,fdt_bytes,
        (void *)(uintptr_t)0x16000000u,1048576u,payload_cmdline,sizeof payload_cmdline,
        load_target==XPL_MENU_TWRP?"androidboot.mode=recovery":"androidboot.mode=normal",
        initrd.base,payload_initrd_bytes,&twrp_fdt);
    if(e)return 0xc0+e;
    if(load_target==XPL_MENU_BOOT_SD) {
        e=xpl_fdt_payload_directory((void *)(uintptr_t)0x16000000u,1048576u,menu.sd_directory,&twrp_fdt.total_bytes);
        if(e)return 0xc0+e;
        if(!payload_dtb_bytes||payload_dtb_bytes>2097152u)return 0xb6;
        e=xpl_fdt_payload_system_overlay((void *)(uintptr_t)0x16000000u,twrp_fdt.total_bytes,
            (void *)(uintptr_t)payload_dtb_source,(size_t)payload_dtb_bytes,&payload_dtb_selected);
        if(e)return 0xd0+e;
        system_overlay=1;
    }
    struct xpl_hw_range original_initrd=initrd;
    volatile uint8_t *clone_proof=find_proof((const void *)(uintptr_t)0x16000000u,twrp_fdt.total_bytes);
    initrd=original_initrd;
    if(!payload_slot(fdt_pa,twrp_fdt.total_bytes)||
       overlaps(fdt_pa,twrp_fdt.total_bytes,kernel_base,XPL_NATIVE_IMAGE_BYTES)||
       overlaps(fdt_pa,twrp_fdt.total_bytes,initrd.base,payload_initrd_bytes)||
       !clone_proof)return 0xb5;
    return 0;
}
static int prepare_emmc_image(void) {
    emmc_media_observe();
    if(emmc_media_result||emmc_image_result||emmc_decode_result||!emmc_image_diag.bytes||!emmc_raw_bytes)return 0xb1;
    /* Shared staging/ABI handoff; these legacy sd_* names are RAM metrics.
     * payload_storage identifies the actual MMC source in the proof. */
    sd_media_result=0;sd_ram_result=0;sd_inflate_result=0;
    sd_ram_bytes=emmc_image_diag.bytes;sd_ram_fingerprint=emmc_image_diag.fingerprint;
    sd_raw_bytes=emmc_raw_bytes;sd_raw_fingerprint=emmc_raw_fingerprint;
    const struct xpl_android_v0_layout *l=&emmc_image_diag.layout;
    payload_kernel_source=0x12000000u+l->kernel.offset;payload_kernel_bytes=l->kernel.bytes;
    payload_initrd_source=0x12000000u+l->ramdisk.offset;payload_initrd_bytes=l->ramdisk.bytes;
    const uint8_t *image=(const void *)(uintptr_t)0x12000000u;
    memcpy(payload_cmdline,image+64,512);
    memcpy(payload_cmdline+512,image+608,1024);
    payload_cmdline[1536]=0;
    return prepare_twrp();
}
static void commit_twrp(void) {
    /* Every fallible parse/layout operation completed on disposable RAM.
     * No old-kernel UTS marker stores are permitted after this point. */
    kernel_replaced=1;
    uint8_t *dest=(void *)(uintptr_t)kernel_base;
    const uint8_t *src=(const void *)(uintptr_t)0x14000000u;
    for(uint64_t i=0;i<sd_raw_bytes;i++) {
        dest[i]=src[i];if(!(i&65535u))(void)sd_time(0);
    }
    dest=(void *)(uintptr_t)initrd.base;
    src=(const void *)(uintptr_t)payload_initrd_source;
    for(uint64_t i=0;i<payload_initrd_bytes;i++) {
        dest[i]=src[i];if(!(i&65535u))(void)sd_time(0);
    }
    dest=(void *)(uintptr_t)fdt_pa;src=(const void *)(uintptr_t)0x16000000u;
    for(size_t i=0;i<twrp_fdt.total_bytes;i++)dest[i]=src[i];
    fdt_bytes=twrp_fdt.total_bytes;proof=find_proof(dest,fdt_bytes);
    stage=7;result=0;record();
    /* Entry has MMU/data cache off. Invalidate S1's possible old kernel I-cache
     * before the existing entry replay branches into the replacement Image. */
    __asm__ volatile("dsb sy\n\tic iallu\n\tdsb sy\n\tisb":::"memory");
}

/* Sumire's two frozen donor DTBs both define DSI0 at fd998000/260. Read only
 * the active DSI0 command-mode path selected by the validated MDP snapshot.
 * No DSI/PHY/panel command writes, resets, mode changes or guessed addresses. */
static void dsi_observe(unsigned boot_page) {
    if(!surface.ready||!surface.command_mode||display.intf!=2) {dsi_result=1;return;}
    if(!boot_page) {
        if(xpl_guard_read32(0xfd998004u,&dsi_ctrl)) {dsi_result=3;return;}
        if(!(dsi_ctrl&1u)) {dsi_result=2;return;}
        if(xpl_guard_read32(0xfd998040u,&dsi_mdp)||
           xpl_guard_read32(0xfd99811cu,&dsi_clock)||
           xpl_guard_read32(0xfd998058u,&dsi_before_stream)||
           xpl_guard_read32(0xfd99805cu,&dsi_before_total)) {dsi_result=3;return;}
    } else {
        if(dsi_result)return;
        if(xpl_guard_read32(0xfd998058u,&dsi_boot_stream)||
           xpl_guard_read32(0xfd99805cu,&dsi_boot_total)) {dsi_result=3;return;}
    }
}
static int window_read(void *unused,uint32_t offset,uint32_t *v) {
    (void)unused;
    if(offset>0x25cu||(offset&3u))return -1;
    return xpl_guard_read32(0xfd998000u+offset,v);
}
static int window_write(void *unused,uint32_t offset,uint32_t v) {
    (void)unused;
    if(!((offset==0x44u&&v==0x13c2cu)||(offset==0x4cu&&v==12u)||(offset==0x90u&&v==1u)||offset==0x110u||
         (offset==0x15cu&&((v&~0x30004u)==0||v==0x30006u))||offset==0x17cu||
         (offset==0x1ecu&&v<=1u)))return -1;
    return xpl_guard_write32(0xfd998000u+offset,v);
}
static void restore_panel_window(void) {
    if(dsi_result||!surface.command_mode||display.intf!=2)return;
    const xpl_dsi_window_io io={0,window_read,window_write,sd_time,fence,sd_relax};
    roi_result=(unsigned)xpl_dsi_window_full(&io,100000,&roi_diag);
}

/* Both frozen sumire DTBs select the v1 MDSS nonsecure context at fd9d4000.
 * Observe only its SCTLR/TTBR0/TTBCR while the validated display AHB is active.
 * No translation requests, page-table writes, context resets or secure reads. */
static void mapping_observe(void) {
    if(!surface.ready||display.revision!=0x10090000u) {smmu_result=1;return;}
    if(xpl_guard_read32(0xfd9d4000u,&smmu_sctlr)||
       xpl_guard_read32(0xfd9d4020u,&smmu_ttbr_lo)||
       xpl_guard_read32(0xfd9d4024u,&smmu_ttbr_hi)||
       xpl_guard_read32(0xfd9d4030u,&smmu_ttbcr))smmu_result=2;
}

static unsigned present_frame(const struct xpl_hw_io *io) {
    const xpl_dsi_window_io dsi={0,window_read,window_write,sd_time,fence,sd_relax};
    if(surface.command_mode&&display.intf==2) {
        frame_result=(unsigned)xpl_dsi_frame_begin(&dsi,&frame_diag);
        if(frame_result)return 0x90u+frame_result;
    }
    unsigned r=xpl_hw_present(&hardware,io,&surface,&diag);
    if(frame_diag.active) {
        frame_result=(unsigned)xpl_dsi_frame_wait(&dsi,1000000,&frame_diag);
        if(!r&&frame_result)r=0x90u+frame_result;
    }
    return r;
}

static int boot_progress(const struct xpl_hw_io *io,const xpl_fb *fb) {
    uint64_t t;
    enum xpl_menu_action target=boot_requested==1?XPL_MENU_BOOT_EMMC:load_target;
    if(xpl_boot_render_target(fb,XPL_BOOT_ACCEPTED,target)!=XPL_FB_OK)return 0xe5;
    unsigned r=present_frame(io);
    if(r)return (int)r;
    if(now_us(&boot_page_us))return 0xe6;
    dsi_observe(1);
    boot_progress_frames++;
    record();
    if(pet_watchdog(boot_page_us/1000))return 0xe9;
    if(boot_requested>1) {
        if(load_target==XPL_MENU_BOOT_EMMC)twrp_prepare_result=prepare_emmc_image();
        else {sd_media_observe();twrp_prepare_result=prepare_twrp();}
        if(twrp_prepare_result)return twrp_prepare_result;
    }
    for(unsigned i=0;i<3;i++) {
        if(xpl_boot_render_target(fb,(enum xpl_boot_phase)(XPL_BOOT_COUNT_3+i),target)!=XPL_FB_OK)return 0xe5;
        r=present_frame(io);
        if(r)return (int)r;
        if(now_us(&countdown_us[i]))return 0xe6;
        boot_progress_frames++;
        record();
        uint64_t previous=countdown_us[i],current;
        unsigned stalled=0;
        do {
            if(now_us(&current)||current<previous)return 0xe6;
            if(current==previous) {if(++stalled>1000000)return 0xe6;}
            else stalled=0;
            previous=current;
            if(pet_watchdog(current/1000))return 0xe9;
            __asm__ volatile("yield");
        } while(current-countdown_us[i]<1000000u+(i==2?(uint64_t)XPL_NATIVE_HOLD_MS*1000u:0));
        countdown_completed++;
    }
    if(xpl_boot_render_target(fb,XPL_BOOT_HANDOFF,target)!=XPL_FB_OK)return 0xe5;
    r=present_frame(io);
    if(r)return (int)r;
    if(now_us(&boot_go_us)||boot_go_us<boot_page_us)return 0xe6;
    boot_progress_frames++;
    if(now_us(&t)||t<boot_go_us)return 0xe6;
    handoff_us=t;
    return 0;
}
static void native_menu_run(const struct xpl_entry_state *state,uintptr_t base) {
    kernel_base=base;fdt_pa=state->x[0];entry_sctlr=state->sctlr;entry_el=state->el;stage=1;record();
    if(base<XPL_TEXT_OFFSET || (base&4095) || fdt_pa<base-XPL_TEXT_OFFSET ||
       fdt_pa>base-XPL_TEXT_OFFSET+0x20000000u-40 || (fdt_pa&7)) {result=0xe1;record();return;}
    uint32_t value;
    if(xpl_guard_read32(fdt_pa,&value) || value!=0xedfe0dd0u || xpl_guard_read32(fdt_pa+4,&value)) {result=0xe2;record();return;}
    uint32_t length=((value&255)<<24)|((value&0xff00)<<8)|((value>>8)&0xff00)|(value>>24);
    if(length<40 || length>2097152 || length>base-XPL_TEXT_OFFSET+0x20000000u-fdt_pa ||
       (fdt_pa<base+XPL_NATIVE_IMAGE_BYTES && base<fdt_pa+length)) {result=0xe3;record();return;}
    stage=2;
    result=xpl_hw_resolve_fdt((const void *)(uintptr_t)fdt_pa,length,&hardware,&diag);
    /* A validated tree is needed before the writeable proof property is used. */
    if(result==XPL_HW_OK || result==XPL_HW_RESOURCE_MISSING || result==XPL_HW_RESOURCE_UNSUPPORTED || result==XPL_HW_NO_RAM) {
        proof=find_proof((const void *)(uintptr_t)fdt_pa,length);
        fdt_bytes=length;
        /* Also clean validated-tree fallback routes before display/key setup.
         * This exact18.1 kernel re-adds charger mode from startup bits0x4004. */
        if(clean_bootargs()) {result=0xea;record();return;}
    }
    record();if(result)return;
    if(xpl_guard_frequency(&timer_frequency) || !timer_frequency || timer_frequency>1000000000 ||
       xpl_guard_counter(&start_ticks)) {result=0xe4;record();return;}
    if(hardware.watchdog.bytes) {
        if(hardware.watchdog.base!=0xf9017000u || hardware.watchdog.bytes!=0x1000u ||
           xpl_guard_read32(0xf9017008u,&watchdog_enabled) ||
           xpl_guard_read32(0xf9017010u,&watchdog_bark) ||
           xpl_guard_read32(0xf9017014u,&watchdog_bite)) {result=0xe8;record();return;}
        /* Preserve the existing watchdog configuration; only feed an active
         * APPS watchdog through the donor's WDT0_RST register. */
        if((watchdog_enabled&1) && xpl_guard_write32(0xf9017004u,1)) {result=0xe9;record();return;}
    }
    const struct xpl_hw_io io={0,io_read,io_write,fence};
    /* Original ramdisk is required for this exact parent-kernel continuation. */
    if(!initrd.bytes) {result=0xe7;record();return;}
    struct xpl_hw_range excluded[3]={{base,XPL_NATIVE_IMAGE_BYTES},{fdt_pa,length},initrd};
    stage=3;record();
    result=xpl_hw_display_discover(&hardware,&io,excluded,3,&surface,&display,&diag);
    display_result=result;display_diag=diag;record();
    stage=4;record();
    result=xpl_hw_keys_init(&hardware,&io,1000,&keys,&diag);
    if(!result)result=xpl_hw_keys_poll(&hardware,&io,&keys,&key_bits,&diag);
    key_result=result;key_diag=diag;record();
    if(display_result) {stage=3;result=display_result;diag=display_diag;record();return;}
    if(key_result)return;
    if(XPL_SERVICE_PROBE_LEVEL)service_probe_result=xpl_service_probe(&hardware,&io,1000,&service_snapshot);
    if(XPL_USB_POWER_PROBE) {
        const xpl_sd_board_io usb_io={0,usb_power_read,usb_power_write,sd_time,0};
        usb_power_result=xpl_usb_power_probe(&usb_io,&usb_power_diag);
    }
    if(XPL_USB_ENUM_PROBE) {
        if(!free_ram(0x1a000000u,8192u))usb_enum_result=-1;
        else {
            usb_guard_result=xpl_usb_guard_arm(&hardware,&io,&usb_guard_diag);record();
            if(!usb_guard_result) {
                char serial[25]="XPL-";static const char digits[]="0123456789abcdef";
                for(unsigned n=0;n<8;n++){serial[4+2*n]=digits[package_id[n]>>4];serial[5+2*n]=digits[package_id[n]&15u];}serial[20]=0;
                watchdog_enabled=1;watchdog_last_pet=0;
                const xpl_sd_board_io usb_io={0,usb_enum_read,usb_enum_write,sd_time,0};
                usb_enum_result=xpl_usb_enum_probe(&usb_io,(void *)(uintptr_t)0x1a000000u,0x1a000000u,8192u,serial,&usb_enum_diag);
                if(usb_enum_diag.power.cleanup_result) {
                    /* Do not hand RAM to another kernel while DMA stop/power
                     * restoration is uncertain. The verified guard owns reset. */
                    record();for(;;)__asm__ volatile("yield");
                }
                usb_guard_result=xpl_usb_guard_disarm(&io,&usb_guard_diag);
                watchdog_enabled=usb_guard_diag.old_en;uint64_t restored=0;if(!now_us(&restored))watchdog_last_pet=restored/1000;
            }else usb_enum_result=-30;
        }
        record();
    }
    /* Diagnostic failure is recorded independently and never enables SD boot. */
    mapping_observe();sd_observe();
    if(XPL_EMMC_PROBE_LEVEL)emmc_observe();
    if(XPL_EMMC_PROBE_LEVEL>=2&&!XPL_LAB_EMMC_TAIL)emmc_media_observe();
    if(XPL_SD_PROBE_LEVEL>=1)sd_interface_observe();
    if((XPL_USB_MSD_READONLY||XPL_USB_MSD_WRITABLE)&&!XPL_LAB_SERVICE_PC_SD&&!XPL_USB_MSD_SERVICE_ONLY) {
        msd_requested=1;sd_picker_operation=3;sd_media_observe();msd_requested=0;sd_picker_operation=0;
        if(!msd_finished)msd_result=sd_media_result?sd_media_result:-1;
        record();
    }
    if(XPL_SD_PROBE_LEVEL>=4&&XPL_SD_PROBE_LEVEL<7)sd_media_observe();
    else {
        if(XPL_SD_PROBE_LEVEL>=2)sd_core_observe();
        if(XPL_SD_PROBE_LEVEL>=3)sd_power_observe();
    }
    dsi_observe(0);restore_panel_window();record();
    xpl_settings_default(&boot_settings);settings_start_keys=key_bits;
    if(XPL_SETTINGS_ENABLED){
        settings_operation=1;sd_picker_operation=5;sd_media_observe();settings_operation=sd_picker_operation=0;
        unsigned sampled=0;
        if(!xpl_hw_keys_poll(&hardware,&io,&keys,&sampled,&diag))settings_start_keys|=sampled;
        else settings_start_keys|=XPL_MENU_KEY_POWER;
    }
    struct xpl_menu_config config={(enum xpl_menu_startup)boot_settings.startup,(enum xpl_menu_action)boot_settings.target,
        XPL_BUILD_LABEL,boot_settings.directory,boot_settings.language};
    if(XPL_LAB_SERVICE_KIND){
        lab_service_entry_sequence=boot_settings.sequence;
        string_copy(lab_service_original_locale,16,boot_settings.language);
        lab_service_active=boot_settings.sequence==XPL_LAB_SERVICE_SEQUENCE&&boot_settings.startup==1&&boot_settings.target==2&&!settings_start_keys;
        if(lab_service_active)config.startup=XPL_MENU_ALWAYS;
        else lab_service_phase=XPL_LAB_SERVICE_KIND==2&&boot_settings.sequence==XPL_LAB_SERVICE_SEQUENCE+1u?8u:9u;
    }
    xpl_menu_render_set_locale(boot_settings.language);
    xpl_menu_init(&menu,&config,XPL_MENU_AVAILABLE(XPL_MENU_BOOT_EMMC)|
        (XPL_SD_PROBE_LEVEL>=7?(XPL_MENU_AVAILABLE(XPL_MENU_TWRP)|XPL_MENU_AVAILABLE(XPL_MENU_BOOT_SD)):0)|
        (XPL_SERVICE_PROBE_LEVEL?(XPL_MENU_AVAILABLE(XPL_MENU_RESET)|XPL_MENU_AVAILABLE(XPL_MENU_POWEROFF)):0),action,0,settings_start_keys,0);
    if(XPL_SETTINGS_ENABLED){xpl_menu_set_settings(&menu,settings_request,0);xpl_menu_set_extended(&menu,language_request,defaults_request,tool_request,0);}
    if(XPL_SD_PROBE_LEVEL>=7)menu.folder_open=folder_open;
    if(XPL_SERVICE_PROBE_LEVEL)xpl_menu_set_services(&menu,(1u<<XPL_SERVICE_FASTBOOT)|
        ((XPL_USB_MSD_READONLY||XPL_USB_MSD_WRITABLE)?1u<<XPL_SERVICE_PC_SD:0),service_action,0);
    if(XPL_LAB_AUTOBOOT_MS&&!XPL_SETTINGS_ENABLED)menu.notice=XPL_LAB_WATCHDOG_S1?XPL_MENU_NOTICE_LAB_GUARD_AUTO:
        XPL_LAB_SERVICE_PC_SD?XPL_MENU_NOTICE_LAB_PC_SD_AUTO:
        XPL_LAB_SERVICE_FASTBOOT?XPL_MENU_NOTICE_LAB_S1_AUTO:XPL_LAB_AUTOBOOT_TARGET==XPL_MENU_TWRP?
        XPL_MENU_NOTICE_LAB_TWRP_AUTO:XPL_LAB_AUTOBOOT_SD?XPL_MENU_NOTICE_LAB_SD_AUTO:XPL_MENU_NOTICE_LAB_AUTO;
    xpl_menu_render_invalidate(&render_cache);
    xpl_fb fb={(volatile uint8_t *)(uintptr_t)surface.base_pa,surface.buffer_bytes,surface.width,surface.height,
               surface.stride_bytes,surface.bytes_per_pixel,surface.red_mask,surface.green_mask,surface.blue_mask,surface.alpha_mask};
    stage=5;record();
    uint64_t current=0,last_poll=0,previous_time=0,last_key_start=0;
    unsigned stalled=0;uint64_t lab_start_us=0;
    for(;;) {
        if(menu.dirty) {
            uint64_t before,after,present_end;
            if(now_us(&before)) {result=0xe6;record();return;}
            if(xpl_menu_render_update(&menu,&fb,&render_cache)!=XPL_FB_OK) {result=0xe5;record();return;}
            if(now_us(&after) || after<before) {result=0xe6;record();return;}
            draw_last_us=after-before;
            if(draw_last_us>draw_max_us)draw_max_us=draw_last_us;
            if(render_cache.kind==XPL_MENU_RENDER_FULL && draw_last_us>draw_full_max_us)draw_full_max_us=draw_last_us;
            if(render_cache.kind==XPL_MENU_RENDER_PARTIAL && draw_last_us>draw_partial_max_us)draw_partial_max_us=draw_last_us;
            if(render_cache.kind!=XPL_MENU_RENDER_NONE) {
                result=present_frame(&io);
                if(now_us(&present_end) || present_end<after) {result=0xe6;record();return;}
                if(present_end-after>present_max_us)present_max_us=present_end-after;
                if(!result) {
                    presented_frames++;
                    if(render_cache.kind==XPL_MENU_RENDER_FULL)full_frames++;
                    else partial_frames++;
                }
            }
            record();if(result)return;
            xpl_menu_mark_clean(&menu);
            if(XPL_LAB_AUTOBOOT_MS&&!lab_start_us&&now_us(&lab_start_us)) {result=0xe6;record();return;}
        }
        uint64_t loop_us;
        if(now_us(&loop_us)) {result=0xe6;record();return;}
        current=loop_us/1000;
        if(current==previous_time) {
            if(++stalled>1000000) {result=0xe6;record();return;}
        } else {stalled=0;previous_time=current;}
        if(pet_watchdog(current)) {result=0xe9;record();return;}
        if(current-last_poll<5) {__asm__ volatile("yield");continue;}
        last_poll=current;
        unsigned next_keys;
        if(last_key_start && loop_us<last_key_start) {result=0xe6;record();return;}
        if(last_key_start && loop_us-last_key_start>input_gap_max_us)input_gap_max_us=loop_us-last_key_start;
        last_key_start=loop_us;
        result=xpl_hw_keys_poll(&hardware,&io,&keys,&next_keys,&diag);
        uint64_t poll_end;
        if(now_us(&poll_end) || poll_end<loop_us) {result=0xe6;record();return;}
        if(poll_end-loop_us>key_poll_max_us)key_poll_max_us=poll_end-loop_us;
        if(result) {record();return;}
        if(next_keys!=key_bits) {key_bits=next_keys;transitions++;record();}
        xpl_menu_poll(&menu,key_bits,poll_end/1000);
#if XPL_LAB_AUTOBOOT_MS > 0
        if(XPL_LAB_AUTOBOOT_MS&&!XPL_SETTINGS_ENABLED&&lab_start_us&&!lab_auto_triggered&&!transitions&&!key_bits&&
           poll_end>=lab_start_us&&poll_end-lab_start_us>=(uint64_t)XPL_LAB_AUTOBOOT_MS*1000) {
            if(XPL_LAB_WATCHDOG_S1) {usb_guard_requested=1;lab_auto_triggered=1;}
            else if(XPL_LAB_SERVICE_PC_SD) {(void)service_action(0,XPL_SERVICE_PC_SD);lab_auto_triggered=1;}
            else if(XPL_LAB_SERVICE_FASTBOOT) {service_requested=1;lab_auto_triggered=1;}
            else if(action(0,(enum xpl_menu_action)XPL_LAB_AUTOBOOT_TARGET)==XPL_MENU_ACTION_RETURNED)lab_auto_triggered=1;
        }
#endif
        if(usb_guard_requested) {
            usb_guard_requested=0;usb_guard_result=xpl_usb_guard_arm(&hardware,&io,&usb_guard_diag);record();
            if(!usb_guard_result) {
                /* Deliberately do not pet while observing the independent
                 * hardware reset. A failed timer still returns after25s. */
                uint64_t begin=0,at=0;if(now_us(&begin)){result=0xe6;record();return;}
                do {if(now_us(&at)||at<begin){result=0xe6;record();return;}__asm__ volatile("yield");}
                while(at-begin<25000000u);
                usb_guard_result=xpl_usb_guard_disarm(&io,&usb_guard_diag);
                if(!usb_guard_result)usb_guard_result=-6;
                watchdog_enabled=usb_guard_diag.old_en;watchdog_last_pet=at/1000;
            }
            record();(void)action(0,XPL_MENU_TWRP);
        }
        if(settings_requested){
            settings_requested=0;settings_save_result=XPL_SD_EPREP;
            settings_operation=2;sd_picker_operation=5;sd_media_observe();settings_operation=sd_picker_operation=0;
            uint64_t saved_us=0;if(now_us(&saved_us)){record();return;}
            if(settings_save_result||sd_media_result)xpl_menu_show_error(&menu,"Unable to save /xploader.cfg",
                (uint32_t)(settings_save_result?settings_save_result:sd_media_result),saved_us/1000u);
            else {
                menu.startup=(enum xpl_menu_startup)boot_settings.startup;menu.quick_target=(enum xpl_menu_action)boot_settings.target;
                string_copy(menu.sd_directory,sizeof menu.sd_directory,boot_settings.directory);
                string_copy(menu.language,sizeof menu.language,boot_settings.language[0]?boot_settings.language:"en");
                menu.notice=XPL_MENU_NOTICE_SETTINGS_SAVED;
            }
            menu.dirty=1;xpl_menu_render_invalidate(&render_cache);record();
        }
        if(tool_requested&&!menu.dirty){tool_requested=0;show_tool_info();record();continue;}
        if(power_requested&&!menu.dirty){
            unsigned selected=power_requested;power_requested=0;
            power_result=selected==1?xpl_service_restart_trigger(&hardware,&io,&service_snapshot,&restart_diag):
                xpl_service_poweroff_trigger(&hardware,&io,power_wait,0,&power_diag);record();
            if(power_result==XPL_S1_TRIGGERED){
                uint64_t began,at;if(!now_us(&began))for(unsigned n=0;n<5000000;n++){
                    if(now_us(&at)||at<began||at-began>=3000000)break;
                    if(pet_watchdog(at/1000))break;__asm__ volatile("yield");
                }
                power_result=selected==1?xpl_service_restart_returned(&io,&restart_diag):xpl_service_poweroff_returned(&io,power_wait,0,&power_diag);
                if(!power_result)power_result=XPL_POWER_CHANGED;
            }
            uint64_t at;if(now_us(&at)){record();return;}
            xpl_menu_show_error(&menu,selected==1?"Restart did not complete":"Power-off did not complete",(uint32_t)power_result,at/1000u);
            menu.error_return_page=XPL_MENU_MAIN;menu.error_return_selected=selected==1?XPL_MENU_RESET:XPL_MENU_POWEROFF;
            xpl_menu_render_invalidate(&render_cache);record();continue;
        }
        if(pcsd_requested){
            pcsd_requested=0;pcsd_requests++;menu.page=XPL_MENU_PC_SD_PAGE;menu.dirty=1;
            if(xpl_menu_render(&menu,&fb)!=XPL_FB_OK||present_frame(&io)){result=0xe7;record();return;}
            msd_finished=0;msd_requested=1;sd_picker_operation=3;sd_media_observe();msd_requested=0;sd_picker_operation=0;
            if(!msd_finished)msd_result=sd_media_result?sd_media_result:-1;
            if(XPL_LAB_SERVICE_PC_SD&&lab_auto_triggered&&!msd_result){
                /* USB is stopped and DMA/clock leases restored before a new
                 * card initialization and exFAT mount. The LAB probe is a
                 * newly created PC file, never an input-ROM allowlist. */
                sd_fs.mounted=sd_fs.cache_valid=sd_fs.bitmap_cache_valid=0;
                pcsd_remount_requested=1;sd_picker_operation=4;sd_media_observe();
                pcsd_remount_requested=0;sd_picker_operation=0;
            }
            uint64_t finished_us=0;if(now_us(&finished_us)){record();return;}
            menu.page=XPL_MENU_SERVICE_PAGE;menu.selected=2;menu.notice=XPL_MENU_NOTICE_NONE;menu.dirty=1;
            if(msd_result||usb_guard_result||sd_media_result)
                xpl_menu_show_error(&menu,"PC SD session failed",(uint32_t)(msd_result?msd_result:usb_guard_result?usb_guard_result:sd_media_result),finished_us/1000u);
            xpl_menu_render_invalidate(&render_cache);record();
            if(XPL_LAB_SERVICE_PC_SD&&lab_auto_triggered)(void)action(0,XPL_MENU_TWRP);
        }
        if(service_requested) {
            service_requested=0;service_requests++;menu.notice=XPL_MENU_NOTICE_S1_REBOOT;
            xpl_menu_render_invalidate(&render_cache);
            if(xpl_menu_render(&menu,&fb)!=XPL_FB_OK||present_frame(&io)) {result=0xe5;record();return;}
            service_reset_result=xpl_service_fastboot_trigger(&hardware,&io,&service_snapshot,&service_reset_diag);
            record();
            if(service_reset_result==XPL_S1_TRIGGERED) {
                uint64_t began=0,elapsed=0;
                if(now_us(&began)) {result=0xe6;record();return;}
                do {
                    if(now_us(&elapsed)||elapsed<began||pet_watchdog(elapsed/1000)) {result=0xe6;record();return;}
                    __asm__ volatile("yield");
                }while(elapsed-began<5000000u);
                service_reset_result=xpl_service_reset_returned(&io,&service_reset_diag);record();
            }
            if(XPL_LAB_SERVICE_FASTBOOT&&lab_auto_triggered) {
                (void)action(0,XPL_MENU_TWRP); /* One returning-error recovery path. */
            }else {
                xpl_menu_show_error(&menu,"Sony S1 Fastboot request returned",(uint32_t)service_reset_result,current);
            }
            xpl_menu_render_invalidate(&render_cache);
        }
        if(sd_picker_request&&!menu.dirty) {
            sd_media_observe();sd_picker_request=0;menu.folder_scanning=0;
            if(sd_media_result||sd_fs_result||sd_picker_result) {
                uint64_t error_us;if(now_us(&error_us)) {record();return;}
                xpl_menu_show_error(&menu,"Unable to read SD directories",
                    (unsigned)(sd_media_result?sd_media_result:sd_fs_result?sd_fs_result:sd_picker_result),error_us/1000u);
            }else {
                unsigned selected=1;
                for(unsigned i=2;i<sd_folder_count;i++)
                    if(same((const uint8_t *)sd_folders[i].path,256,menu.sd_directory))selected=i;
                xpl_menu_show_folders(&menu,sd_browse_path,sd_folders,sd_folder_count,selected,0,folder_select,0);
            }
            record();continue;
        }
        if(XPL_LAB_SERVICE_KIND)service_lab_tick();
        if(boot_requested) {
            result=(unsigned)boot_progress(&io,&fb);
            if(result&&boot_requested>1) {
                /* A returned SD error gets one known eMMC continuation in lab
                 * mode. A hung kernel is outside this returning-error path. */
                if(XPL_LAB_AUTOBOOT_MS&&lab_auto_triggered&&!lab_sd_fallback) {
                    lab_sd_fallback=1;
                    if(action(0,load_target==XPL_MENU_BOOT_EMMC?XPL_MENU_TWRP:XPL_MENU_BOOT_EMMC)==XPL_MENU_ACTION_RETURNED) {
                        result=0;record();continue;
                    }
                }
                uint64_t error_us;if(now_us(&error_us)) {record();return;}
                xpl_menu_show_error(&menu,load_target==XPL_MENU_BOOT_EMMC?"eMMC image load failed":load_target==XPL_MENU_TWRP?
                    (!sd_fs_result&&sd_twrp_result==XPL_EXFAT_ENOTFOUND?"/twrp.img not found":"TWRP SD load failed"):
                    "SD folder load failed",result,error_us/1000u);
                boot_requested=0;result=0;xpl_menu_render_invalidate(&render_cache);record();continue;
            }
            if(!result&&boot_requested>1) {commit_twrp();return;}
            if(!result)stage=6;
            record();return;
        }
    }
}

struct native_work {const struct xpl_entry_state *state;uintptr_t base;};
static int run_native_work(void *context) {
    struct native_work *work=context;native_menu_run(work->state,work->base);return 0;
}
void xpl_native_main(const struct xpl_entry_state *state,uintptr_t base) {
    struct native_work work={state,base};
    int e=xpl_icache_run(XPL_NATIVE_ICACHE,run_native_work,&work,&icache_diag);
    if(e==XPL_ICACHE_STATE)native_menu_run(state,base);
    /* SD commit now owns a low-RAM FDT record; no old UTS stores occur. */
    if(proof)record();
}
