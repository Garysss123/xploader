/* SPDX-License-Identifier: MIT */
#ifndef XPL_PAYLOAD_H
#define XPL_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#define XPL_PAYLOAD_PATH_BYTES 256u
#define XPL_PAYLOAD_CMDLINE_BYTES 2048u
#define XPL_PAYLOAD_CONFIG_MAX 4096u
#define XPL_PAYLOAD_KERNEL_CHOICES 3u

enum xpl_payload_status {
    XPL_PAYLOAD_OK = 0,
    XPL_PAYLOAD_ARGUMENT,
    XPL_PAYLOAD_CONFIG_TOO_LARGE,
    XPL_PAYLOAD_CONTROL_CHARACTER,
    XPL_PAYLOAD_SYNTAX,
    XPL_PAYLOAD_UNKNOWN_KEY,
    XPL_PAYLOAD_DUPLICATE_KEY,
    XPL_PAYLOAD_VALUE_TOO_LONG,
    XPL_PAYLOAD_INVALID_VALUE,
    XPL_PAYLOAD_PATH_ESCAPE,
    XPL_PAYLOAD_PATH_INVALID,
    XPL_PAYLOAD_MODE_CONFLICT,
    XPL_PAYLOAD_NOT_FOUND,
    XPL_PAYLOAD_IO_ERROR,
    XPL_PAYLOAD_AMBIGUOUS,
    XPL_PAYLOAD_EMPTY_FILE,
    XPL_PAYLOAD_IMAGE_HEADER,
    XPL_PAYLOAD_IMAGE_UNSUPPORTED,
    XPL_PAYLOAD_IMAGE_TRUNCATED,
    XPL_PAYLOAD_IMAGE_RANGE
};

enum xpl_payload_mode {
    XPL_PAYLOAD_AUTO = 0,
    XPL_PAYLOAD_SPLIT = 1,
    XPL_PAYLOAD_BOOTIMG = 2
};

enum xpl_payload_abi {
    XPL_PAYLOAD_ABI_UNSPECIFIED = 0,
    XPL_PAYLOAD_ABI_LINUX_AARCH64 = 1,
    XPL_PAYLOAD_ABI_BRIDGE = 2
};

enum xpl_payload_field {
    XPL_PAYLOAD_FIELD_NONE = 0,
    XPL_PAYLOAD_FIELD_MODE,
    XPL_PAYLOAD_FIELD_KERNEL,
    XPL_PAYLOAD_FIELD_INITRD,
    XPL_PAYLOAD_FIELD_DTB,
    XPL_PAYLOAD_FIELD_ABI,
    XPL_PAYLOAD_FIELD_CMDLINE
};

#define XPL_PAYLOAD_HAS(field) (1u << (unsigned)(field))

struct xpl_payload_error {
    enum xpl_payload_status status;
    enum xpl_payload_field field;
    size_t line;                 /* 1-based for text parsing; otherwise zero. */
    size_t column;
    char subject[XPL_PAYLOAD_PATH_BYTES]; /* Diagnostic only; may be clipped. */
};

struct xpl_payload_config {
    uint32_t present;
    enum xpl_payload_mode mode;
    enum xpl_payload_abi abi;
    char kernel[XPL_PAYLOAD_PATH_BYTES];
    char initrd[XPL_PAYLOAD_PATH_BYTES];
    char dtb[XPL_PAYLOAD_PATH_BYTES];
    char cmdline[XPL_PAYLOAD_CMDLINE_BYTES];
};

struct xpl_payload_file {
    char path[XPL_PAYLOAD_PATH_BYTES];
    uint64_t bytes;
    unsigned present;
};

enum xpl_payload_fs_result {
    XPL_PAYLOAD_FS_ERROR = -1,
    XPL_PAYLOAD_FS_MISSING = 0,
    XPL_PAYLOAD_FS_FILE = 1
};

/* Literal relative path, rooted at the selected SD directory by the caller.
 * The callback must not execute commands, follow escaping symlinks, or return
 * directories as files. This parser does not resolve filesystem links. */
typedef enum xpl_payload_fs_result (*xpl_payload_stat_fn)(
    void *context, const char *relative_path, uint64_t *bytes);

struct xpl_payload_inventory {
    struct xpl_payload_file bootimg;
    struct xpl_payload_file kernels[XPL_PAYLOAD_KERNEL_CHOICES];
    unsigned kernel_count;
    struct xpl_payload_file initrd;
    struct xpl_payload_file dtb;
};

struct xpl_payload_descriptor {
    enum xpl_payload_mode mode;
    enum xpl_payload_abi abi;
    struct xpl_payload_file kernel; /* Whole boot image in BOOTIMG mode. */
    struct xpl_payload_file initrd;
    struct xpl_payload_file dtb;
    char cmdline[XPL_PAYLOAD_CMDLINE_BYTES];
    unsigned cmdline_present; /* Explicit metadata override, not a shell command. */
    unsigned runnable_verified; /* Always zero: a descriptor is not a handoff. */
};

/* Empty input is an optional/missing startup.txt and returns default AUTO.
 * Inputs are byte-counted, not NUL-terminated. Output is cleared on failure. */
enum xpl_payload_status xpl_payload_parse_startup(
    const void *bytes, size_t length, struct xpl_payload_config *out,
    struct xpl_payload_error *error);

/* Discovers boot.img, Image, Image.gz, zImage, initrd.gz, dtb, in that order.
 * No file is read or selected; even empty candidate files remain visible. */
enum xpl_payload_status xpl_payload_discover(
    xpl_payload_stat_fn stat_file, void *context,
    struct xpl_payload_inventory *out, struct xpl_payload_error *error);

/* choice is a UI mode choice or AUTO. Explicit config mode and UI choice must
 * agree. A config kernel with AUTO implies SPLIT; BOOTIMG uses kernel as its
 * optional image filename. Multiple split kernels require config.kernel.
 * On AMBIGUOUS, inspect the unchanged inventory for choices. No hash/manifest/
 * model registration is involved. Missing optional paths auto-detect; explicit
 * empty initrd= or dtb= disables that optional component. */
enum xpl_payload_status xpl_payload_select(
    const struct xpl_payload_config *config, enum xpl_payload_mode choice,
    const struct xpl_payload_inventory *inventory,
    xpl_payload_stat_fn stat_file, void *context,
    struct xpl_payload_descriptor *out, struct xpl_payload_error *error);

struct xpl_payload_span {
    uint64_t offset;
    uint64_t bytes;
};

struct xpl_android_v0_layout {
    uint32_t page_bytes;
    uint32_t kernel_load_address;
    uint32_t ramdisk_load_address;
    uint32_t second_load_address;
    uint32_t tags_address;
    uint32_t os_version;
    struct xpl_payload_span kernel;
    struct xpl_payload_span ramdisk;
    struct xpl_payload_span second;
    uint64_t required_data_end;
    uint64_t padded_payload_end;
    uint64_t trailing_bytes; /* After padded end; not authenticated or interpreted. */
    unsigned runnable_verified; /* Always zero; load addresses are untrusted metadata. */
};

/* Reads only supplied header bytes (minimum 1632), bounds spans against the
 * separate total file size. Supports modern Android boot header version 0,
 * not legacy dt_size headers, v1+, vendor_boot or signed-envelope parsing.
 * A missing final padding page is allowed if every component byte is present. */
enum xpl_payload_status xpl_payload_parse_android_v0(
    const void *header, size_t header_bytes, uint64_t file_bytes,
    struct xpl_android_v0_layout *out, struct xpl_payload_error *error);

const char *xpl_payload_status_string(enum xpl_payload_status status);

#endif
