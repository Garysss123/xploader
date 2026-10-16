#ifndef XPL_EXFAT_H
#define XPL_EXFAT_H

#include <stddef.h>
#include <stdint.h>

/* Read-only, allocation-free exFAT transport. All device LBAs/counts are in
 * 512-byte units, even on volumes whose logical sectors are larger. Return 0
 * only when the complete requested read succeeded. No write callback exists.
 * Keep the medium stable for the mount's lifetime; remount after card changes.
 * Contexts/iterators are caller-owned and are not concurrent or reentrant. */
typedef int (*xpl_exfat_read_fn)(void *ctx, uint64_t lba512,
                               uint32_t count512, void *out);

enum {
    XPL_EXFAT_OK = 0, XPL_EXFAT_END = 1,
    XPL_EXFAT_EINVAL = -1, XPL_EXFAT_EIO = -2,
    XPL_EXFAT_EFORMAT = -3, XPL_EXFAT_EBOUNDS = -4,
    XPL_EXFAT_ELOOP = -5, XPL_EXFAT_ELIMIT = -6,
    XPL_EXFAT_ENOTFOUND = -7, XPL_EXFAT_ENOTDIR = -8,
    XPL_EXFAT_EUNSUPPORTED = -9
};

#define XPL_EXFAT_NAME_BYTES 766u
#define XPL_EXFAT_PATH_BYTES 4096u
#define XPL_EXFAT_ATTR_DIRECTORY 0x10u
/* Resource bound, not a format restriction. Override at compile time for a
 * platform willing to validate more clusters per stream. */
#ifndef XPL_EXFAT_CHAIN_LIMIT
#define XPL_EXFAT_CHAIN_LIMIT 1048576u
#endif

typedef struct {
    uint64_t data_length, valid_data_length;
    uint32_t first_cluster;
    uint16_t attributes;
    uint8_t no_fat_chain, is_root;
    char name[XPL_EXFAT_NAME_BYTES];
} xpl_exfat_node;

typedef struct {
    xpl_exfat_read_fn read;
    void *read_ctx;
    uint64_t volume_start512, volume_count512, fat_start512, heap_start512;
    uint64_t root_length, bitmap_length;
    uint64_t cache_lba, bitmap_cache_lba;
    uint32_t cluster_count, root_cluster, bitmap_cluster, cluster_bytes;
    uint32_t bitmap_cursor_cluster, bitmap_cursor_index, serial;
    uint16_t volume_flags;
    uint16_t read_limit_blocks; /* Defaults to1; explicit backend opt-in, <=128. */
    uint8_t sectors_shift, cluster_shift, active_fat, number_of_fats;
    uint8_t cache_valid, bitmap_cache_valid, mounted;
    uint8_t cache[512], bitmap_cache[512];
} xpl_exfat;

typedef struct {
    xpl_exfat *fs;
    uint64_t position, length;
    uint32_t first_cluster, cluster, cluster_index;
    uint8_t no_fat_chain, is_root, ended;
    int error;
} xpl_exfat_dir;

/* Caller supplies an already-discovered partition range, in device sectors.
 * Main boot region is required; no automatic backup-boot recovery or repair.
 * Partial mounts cannot be used after a nonzero return. */
int xpl_exfat_mount(xpl_exfat *fs, xpl_exfat_read_fn read, void *ctx,
                   uint64_t volume_start512, uint64_t volume_count512);
/* Permit bounded whole-sector file reads within one already validated cluster.
 * Metadata and partial sectors retain the one-sector cache. The backend must
 * support complete requests up to this bound; default1 retains older behavior.
 */
int xpl_exfat_set_read_limit(xpl_exfat *fs, unsigned blocks512);
int xpl_exfat_root(xpl_exfat *fs, xpl_exfat_node *out);
int xpl_exfat_dir_open(xpl_exfat *fs, xpl_exfat_dir *dir,
                      const xpl_exfat_node *node);
/* OK: one file/directory; END: completed; negative: error, iterator latched.
 * UTF-16 names convert strictly to UTF-8; malformed surrogate pairs fail.
 * Metadata records are omitted. Entries have validated stream allocation. */
int xpl_exfat_dir_next(xpl_exfat_dir *dir, xpl_exfat_node *out);
/* Absolute or root-relative UTF-8 path, '/' separators. Exact case-sensitive
 * name comparison: use names returned by dir_next. Up-case table and NameHash
 * are not used. '.'/'..' are rejected; repeated '/' is accepted. */
int xpl_exfat_lookup(xpl_exfat *fs, const char *path, xpl_exfat_node *out);
/* Stream allocation is checked before copying any bytes. On I/O failure,
 * bytes_read reports a partial copy: the caller must discard failed payloads.
 * Reads beyond ValidDataLength yield zeroes. END means offset >= DataLength;
 * a short successful read ending at EOF returns OK. Directories are rejected. */
int xpl_exfat_pread(xpl_exfat *fs, const xpl_exfat_node *node,
                   uint64_t offset, void *out, size_t length, size_t *bytes_read);
/* Map one fully initialized aligned file sector. No allocation or metadata
 * writes. Caller must retain exclusive ownership and revalidate on remount. */
int xpl_exfat_file_sector(xpl_exfat *,const xpl_exfat_node *,uint64_t offset,uint64_t *device_lba);

#endif
