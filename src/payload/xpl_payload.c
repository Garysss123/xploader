/* SPDX-License-Identifier: MIT */
#include "xpl_payload.h"

#define CONFIG_FIELDS 0x7eu

static void zero_bytes(void *ptr, size_t size)
{
    unsigned char *p = (unsigned char *)ptr;
    while (size--) *p++ = 0;
}

static void copy_bytes(void *dst, const void *src, size_t size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (size--) *d++ = *s++;
}

static size_t bounded_length(const char *s, size_t bound)
{
    size_t n = 0;
    while (n < bound && s[n]) ++n;
    return n;
}

static int equals(const unsigned char *text, size_t length, const char *literal)
{
    size_t i;
    for (i = 0; i < length; ++i)
        if (!literal[i] || text[i] != (unsigned char)literal[i]) return 0;
    return literal[length] == 0;
}

static enum xpl_payload_status fail(struct xpl_payload_error *error,
    enum xpl_payload_status status, enum xpl_payload_field field,
    size_t line, size_t column, const void *subject, size_t length)
{
    if (error) {
        zero_bytes(error, sizeof(*error));
        error->status = status;
        error->field = field;
        error->line = line;
        error->column = column;
        if (subject) {
            if (length >= sizeof(error->subject)) length = sizeof(error->subject) - 1;
            copy_bytes(error->subject, subject, length);
        }
    }
    return status;
}

static enum xpl_payload_status path_status(const char *path, size_t length,
    unsigned allow_empty)
{
    size_t i, start = 0;
    if (!length) return allow_empty ? XPL_PAYLOAD_OK : XPL_PAYLOAD_PATH_INVALID;
    if (length >= XPL_PAYLOAD_PATH_BYTES) return XPL_PAYLOAD_VALUE_TOO_LONG;
    if (path[0] == '/' || path[0] == '\\') return XPL_PAYLOAD_PATH_ESCAPE;
    for (i = 0; i <= length; ++i) {
        unsigned char c = i < length ? (unsigned char)path[i] : '/';
        if (i < length && (c < 32 || c == 127)) return XPL_PAYLOAD_CONTROL_CHARACTER;
        if (c == '\\') return XPL_PAYLOAD_PATH_ESCAPE;
        if (c == ':') return XPL_PAYLOAD_PATH_INVALID;
        if (c == '/') {
            size_t n = i - start;
            if (n == 2 && path[start] == '.' && path[start + 1] == '.')
                return XPL_PAYLOAD_PATH_ESCAPE;
            if (!n || (n == 1 && path[start] == '.')) return XPL_PAYLOAD_PATH_INVALID;
            start = i + 1;
        }
    }
    return XPL_PAYLOAD_OK;
}

static enum xpl_payload_field key_field(const unsigned char *key, size_t length)
{
    if (equals(key, length, "mode")) return XPL_PAYLOAD_FIELD_MODE;
    if (equals(key, length, "kernel")) return XPL_PAYLOAD_FIELD_KERNEL;
    if (equals(key, length, "initrd")) return XPL_PAYLOAD_FIELD_INITRD;
    if (equals(key, length, "dtb")) return XPL_PAYLOAD_FIELD_DTB;
    if (equals(key, length, "abi")) return XPL_PAYLOAD_FIELD_ABI;
    if (equals(key, length, "cmdline")) return XPL_PAYLOAD_FIELD_CMDLINE;
    return XPL_PAYLOAD_FIELD_NONE;
}

enum xpl_payload_status xpl_payload_parse_startup(
    const void *bytes, size_t length, struct xpl_payload_config *out,
    struct xpl_payload_error *error)
{
    const unsigned char *text = (const unsigned char *)bytes;
    struct xpl_payload_config config;
    size_t i, start = 0, line = 1, column = 1;
    if (error) zero_bytes(error, sizeof(*error));
    if (out) zero_bytes(out, sizeof(*out));
    if (!out || (!bytes && length))
        return fail(error, XPL_PAYLOAD_ARGUMENT, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    if (length > XPL_PAYLOAD_CONFIG_MAX)
        return fail(error, XPL_PAYLOAD_CONFIG_TOO_LARGE, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    zero_bytes(&config, sizeof(config));
    /* Check the entire byte range, including comments and ignored blank lines. */
    for (i = 0; i < length; ++i) {
        unsigned char c = text[i];
        if ((c < 32 && c != '\n' && c != '\r') || c == 127 ||
            (c == '\r' && (i + 1 == length || text[i + 1] != '\n')))
            return fail(error, XPL_PAYLOAD_CONTROL_CHARACTER, XPL_PAYLOAD_FIELD_NONE,
                        line, column, text + i, 1);
        if (c == '\n') { ++line; column = 1; }
        else ++column;
    }
    line = 1;
    while (start < length) {
        size_t end = start, left, right, split, key_end, value;
        enum xpl_payload_field field;
        enum xpl_payload_status status;
        char *destination = 0;
        size_t capacity = 0;
        while (end < length && text[end] != '\n') ++end;
        left = start; right = end;
        if (right > left && text[right - 1] == '\r') --right;
        while (left < right && text[left] == ' ') ++left;
        while (right > left && text[right - 1] == ' ') --right;
        if (left == right || text[left] == '#' || text[left] == ';') goto next_line;
        split = left;
        while (split < right && text[split] != '=') ++split;
        if (split == right)
            return fail(error, XPL_PAYLOAD_SYNTAX, XPL_PAYLOAD_FIELD_NONE,
                        line, left - start + 1, text + left, right - left);
        key_end = split;
        while (key_end > left && text[key_end - 1] == ' ') --key_end;
        field = key_field(text + left, key_end - left);
        if (field == XPL_PAYLOAD_FIELD_NONE)
            return fail(error, XPL_PAYLOAD_UNKNOWN_KEY, field, line,
                        left - start + 1, text + left, key_end - left);
        if (config.present & XPL_PAYLOAD_HAS(field))
            return fail(error, XPL_PAYLOAD_DUPLICATE_KEY, field, line,
                        left - start + 1, text + left, key_end - left);
        config.present |= XPL_PAYLOAD_HAS(field);
        value = split + 1;
        while (value < right && text[value] == ' ') ++value;
        if (field == XPL_PAYLOAD_FIELD_MODE) {
            if (equals(text + value, right - value, "split")) config.mode = XPL_PAYLOAD_SPLIT;
            else if (equals(text + value, right - value, "bootimg")) config.mode = XPL_PAYLOAD_BOOTIMG;
            else return fail(error, XPL_PAYLOAD_INVALID_VALUE, field, line,
                             value - start + 1, text + value, right - value);
        } else if (field == XPL_PAYLOAD_FIELD_ABI) {
            if (equals(text + value, right - value, "linux-aarch64")) config.abi = XPL_PAYLOAD_ABI_LINUX_AARCH64;
            else if (equals(text + value, right - value, "bridge")) config.abi = XPL_PAYLOAD_ABI_BRIDGE;
            else return fail(error, XPL_PAYLOAD_INVALID_VALUE, field, line,
                             value - start + 1, text + value, right - value);
        } else {
            switch (field) {
            case XPL_PAYLOAD_FIELD_KERNEL: destination = config.kernel; capacity = sizeof(config.kernel); break;
            case XPL_PAYLOAD_FIELD_INITRD: destination = config.initrd; capacity = sizeof(config.initrd); break;
            case XPL_PAYLOAD_FIELD_DTB: destination = config.dtb; capacity = sizeof(config.dtb); break;
            case XPL_PAYLOAD_FIELD_CMDLINE: destination = config.cmdline; capacity = sizeof(config.cmdline); break;
            default: return fail(error, XPL_PAYLOAD_ARGUMENT, field, line, 0, 0, 0);
            }
            if (right - value >= capacity)
                return fail(error, XPL_PAYLOAD_VALUE_TOO_LONG, field, line,
                            value - start + 1, text + value, right - value);
            if (field != XPL_PAYLOAD_FIELD_CMDLINE) {
                status = path_status((const char *)text + value, right - value,
                                     field != XPL_PAYLOAD_FIELD_KERNEL);
                if (status != XPL_PAYLOAD_OK)
                    return fail(error, status, field, line, value - start + 1,
                                text + value, right - value);
            }
            copy_bytes(destination, text + value, right - value);
        }
next_line:
        start = end < length ? end + 1 : end;
        ++line;
    }
    copy_bytes(out, &config, sizeof(config));
    return XPL_PAYLOAD_OK;
}

static enum xpl_payload_status query_file(xpl_payload_stat_fn stat_file, void *context,
    const char *path, unsigned required, enum xpl_payload_field field,
    struct xpl_payload_file *out, struct xpl_payload_error *error)
{
    enum xpl_payload_fs_result found;
    enum xpl_payload_status status;
    size_t length = bounded_length(path, XPL_PAYLOAD_PATH_BYTES);
    uint64_t bytes = 0;
    zero_bytes(out, sizeof(*out));
    status = path_status(path, length, 0);
    if (status != XPL_PAYLOAD_OK) return fail(error, status, field, 0, 0, path, length);
    found = stat_file(context, path, &bytes);
    if (found != XPL_PAYLOAD_FS_FILE && found != XPL_PAYLOAD_FS_MISSING)
        return fail(error, XPL_PAYLOAD_IO_ERROR, field, 0, 0, path, length);
    copy_bytes(out->path, path, length);
    if (found == XPL_PAYLOAD_FS_MISSING) {
        if (required) return fail(error, XPL_PAYLOAD_NOT_FOUND, field, 0, 0, path, length);
        return XPL_PAYLOAD_OK;
    }
    out->present = 1;
    out->bytes = bytes;
    return XPL_PAYLOAD_OK;
}

enum xpl_payload_status xpl_payload_discover(
    xpl_payload_stat_fn stat_file, void *context,
    struct xpl_payload_inventory *out, struct xpl_payload_error *error)
{
    static const char *const kernels[XPL_PAYLOAD_KERNEL_CHOICES] = { "Image", "Image.gz", "zImage" };
    struct xpl_payload_inventory inventory;
    struct xpl_payload_file file;
    enum xpl_payload_status status;
    unsigned i;
    if (error) zero_bytes(error, sizeof(*error));
    if (out) zero_bytes(out, sizeof(*out));
    if (!stat_file || !out)
        return fail(error, XPL_PAYLOAD_ARGUMENT, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    zero_bytes(&inventory, sizeof(inventory));
    status = query_file(stat_file, context, "boot.img", 0, XPL_PAYLOAD_FIELD_KERNEL, &inventory.bootimg, error);
    if (status != XPL_PAYLOAD_OK) return status;
    for (i = 0; i < XPL_PAYLOAD_KERNEL_CHOICES; ++i) {
        status = query_file(stat_file, context, kernels[i], 0, XPL_PAYLOAD_FIELD_KERNEL, &file, error);
        if (status != XPL_PAYLOAD_OK) return status;
        if (file.present) copy_bytes(&inventory.kernels[inventory.kernel_count++], &file, sizeof(file));
    }
    status = query_file(stat_file, context, "initrd.gz", 0, XPL_PAYLOAD_FIELD_INITRD, &inventory.initrd, error);
    if (status != XPL_PAYLOAD_OK) return status;
    status = query_file(stat_file, context, "dtb", 0, XPL_PAYLOAD_FIELD_DTB, &inventory.dtb, error);
    if (status != XPL_PAYLOAD_OK) return status;
    copy_bytes(out, &inventory, sizeof(inventory));
    return XPL_PAYLOAD_OK;
}

static enum xpl_payload_status validate_config(const struct xpl_payload_config *config,
    struct xpl_payload_error *error)
{
    const char *paths[3] = { config->kernel, config->initrd, config->dtb };
    unsigned i;
    size_t n;
    if ((config->present & ~CONFIG_FIELDS) || config->mode > XPL_PAYLOAD_BOOTIMG ||
        config->mode < XPL_PAYLOAD_AUTO || config->abi > XPL_PAYLOAD_ABI_BRIDGE ||
        config->abi < XPL_PAYLOAD_ABI_UNSPECIFIED)
        return fail(error, XPL_PAYLOAD_INVALID_VALUE, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    if ((config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_MODE)) && config->mode == XPL_PAYLOAD_AUTO)
        return fail(error, XPL_PAYLOAD_INVALID_VALUE, XPL_PAYLOAD_FIELD_MODE, 0, 0, 0, 0);
    if ((config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_ABI)) && config->abi == XPL_PAYLOAD_ABI_UNSPECIFIED)
        return fail(error, XPL_PAYLOAD_INVALID_VALUE, XPL_PAYLOAD_FIELD_ABI, 0, 0, 0, 0);
    for (i = 0; i < 3; ++i) {
        enum xpl_payload_field field = (enum xpl_payload_field)(XPL_PAYLOAD_FIELD_KERNEL + i);
        enum xpl_payload_status status;
        if (!(config->present & XPL_PAYLOAD_HAS(field))) continue;
        n = bounded_length(paths[i], XPL_PAYLOAD_PATH_BYTES);
        status = path_status(paths[i], n, i != 0);
        if (status != XPL_PAYLOAD_OK) return fail(error, status, field, 0, 0, paths[i], n);
    }
    if (config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_CMDLINE)) {
        n = bounded_length(config->cmdline, sizeof(config->cmdline));
        if (n == sizeof(config->cmdline))
            return fail(error, XPL_PAYLOAD_VALUE_TOO_LONG, XPL_PAYLOAD_FIELD_CMDLINE, 0, 0, 0, 0);
        for (i = 0; i < n; ++i)
            if ((unsigned char)config->cmdline[i] < 32 || (unsigned char)config->cmdline[i] == 127)
                return fail(error, XPL_PAYLOAD_CONTROL_CHARACTER, XPL_PAYLOAD_FIELD_CMDLINE, 0, i + 1, 0, 0);
    }
    return XPL_PAYLOAD_OK;
}

static enum xpl_payload_status select_file(xpl_payload_stat_fn stat_file, void *context,
    const char *path, unsigned required, enum xpl_payload_field field,
    struct xpl_payload_file *out, struct xpl_payload_error *error)
{
    enum xpl_payload_status status = query_file(stat_file, context, path, required, field, out, error);
    if (status != XPL_PAYLOAD_OK) return status;
    if (out->present && !out->bytes)
        return fail(error, XPL_PAYLOAD_EMPTY_FILE, field, 0, 0, path,
                    bounded_length(path, XPL_PAYLOAD_PATH_BYTES));
    return XPL_PAYLOAD_OK;
}

enum xpl_payload_status xpl_payload_select(
    const struct xpl_payload_config *config, enum xpl_payload_mode choice,
    const struct xpl_payload_inventory *inventory,
    xpl_payload_stat_fn stat_file, void *context,
    struct xpl_payload_descriptor *out, struct xpl_payload_error *error)
{
    struct xpl_payload_descriptor descriptor;
    enum xpl_payload_mode mode;
    enum xpl_payload_status status;
    const char *kernel;
    if (error) zero_bytes(error, sizeof(*error));
    if (out) zero_bytes(out, sizeof(*out));
    if (!config || !inventory || !stat_file || !out || choice < XPL_PAYLOAD_AUTO ||
        choice > XPL_PAYLOAD_BOOTIMG || inventory->kernel_count > XPL_PAYLOAD_KERNEL_CHOICES ||
        inventory->bootimg.present > 1)
        return fail(error, XPL_PAYLOAD_ARGUMENT, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    status = validate_config(config, error);
    if (status != XPL_PAYLOAD_OK) return status;
    zero_bytes(&descriptor, sizeof(descriptor));
    mode = config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_MODE) ? config->mode : XPL_PAYLOAD_AUTO;
    if (choice != XPL_PAYLOAD_AUTO) {
        if (mode != XPL_PAYLOAD_AUTO && mode != choice)
            return fail(error, XPL_PAYLOAD_MODE_CONFLICT, XPL_PAYLOAD_FIELD_MODE, 0, 0, 0, 0);
        mode = choice;
    }
    if (mode == XPL_PAYLOAD_AUTO && (config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_KERNEL)))
        mode = XPL_PAYLOAD_SPLIT;
    if (mode == XPL_PAYLOAD_AUTO) {
        if (inventory->bootimg.present && inventory->kernel_count)
            return fail(error, XPL_PAYLOAD_AMBIGUOUS, XPL_PAYLOAD_FIELD_MODE, 0, 0, 0, 0);
        if (inventory->bootimg.present) mode = XPL_PAYLOAD_BOOTIMG;
        else if (inventory->kernel_count) mode = XPL_PAYLOAD_SPLIT;
        else return fail(error, XPL_PAYLOAD_NOT_FOUND, XPL_PAYLOAD_FIELD_KERNEL, 0, 0, 0, 0);
    }
    if (mode == XPL_PAYLOAD_BOOTIMG) {
        if (((config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_INITRD)) && config->initrd[0]) ||
            ((config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_DTB)) && config->dtb[0]))
            return fail(error, XPL_PAYLOAD_MODE_CONFLICT, XPL_PAYLOAD_FIELD_MODE, 0, 0, 0, 0);
        kernel = config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_KERNEL) ? config->kernel : "boot.img";
    } else if (config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_KERNEL)) kernel = config->kernel;
    else {
        if (inventory->kernel_count > 1)
            return fail(error, XPL_PAYLOAD_AMBIGUOUS, XPL_PAYLOAD_FIELD_KERNEL, 0, 0, 0, 0);
        if (!inventory->kernel_count)
            return fail(error, XPL_PAYLOAD_NOT_FOUND, XPL_PAYLOAD_FIELD_KERNEL, 0, 0, 0, 0);
        if (inventory->kernels[0].present != 1)
            return fail(error, XPL_PAYLOAD_ARGUMENT, XPL_PAYLOAD_FIELD_KERNEL, 0, 0, 0, 0);
        kernel = inventory->kernels[0].path;
    }
    status = select_file(stat_file, context, kernel, 1, XPL_PAYLOAD_FIELD_KERNEL, &descriptor.kernel, error);
    if (status != XPL_PAYLOAD_OK) return status;
    if (mode == XPL_PAYLOAD_SPLIT) {
        unsigned explicit_initrd = !!(config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_INITRD));
        unsigned explicit_dtb = !!(config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_DTB));
        if (!explicit_initrd || config->initrd[0]) {
            status = select_file(stat_file, context, explicit_initrd ? config->initrd : "initrd.gz",
                                 explicit_initrd, XPL_PAYLOAD_FIELD_INITRD, &descriptor.initrd, error);
            if (status != XPL_PAYLOAD_OK) return status;
        }
        if (!explicit_dtb || config->dtb[0]) {
            status = select_file(stat_file, context, explicit_dtb ? config->dtb : "dtb",
                                 explicit_dtb, XPL_PAYLOAD_FIELD_DTB, &descriptor.dtb, error);
            if (status != XPL_PAYLOAD_OK) return status;
        }
    }
    descriptor.mode = mode;
    descriptor.abi = config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_ABI) ? config->abi : XPL_PAYLOAD_ABI_UNSPECIFIED;
    descriptor.cmdline_present = !!(config->present & XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_CMDLINE));
    if (descriptor.cmdline_present)
        copy_bytes(descriptor.cmdline, config->cmdline, bounded_length(config->cmdline, sizeof(config->cmdline)));
    copy_bytes(out, &descriptor, sizeof(descriptor));
    return XPL_PAYLOAD_OK;
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static enum xpl_payload_status component_span(uint64_t *cursor, uint32_t bytes,
    uint32_t page, uint64_t file_bytes, struct xpl_payload_span *span,
    uint64_t *required_end)
{
    uint64_t end;
    if (!bytes) return XPL_PAYLOAD_OK;
    if (*cursor > UINT64_MAX - bytes) return XPL_PAYLOAD_IMAGE_RANGE;
    end = *cursor + bytes;
    if (end > file_bytes) return XPL_PAYLOAD_IMAGE_TRUNCATED;
    span->offset = *cursor;
    span->bytes = bytes;
    *required_end = end;
    if (end > UINT64_MAX - (page - 1u)) return XPL_PAYLOAD_IMAGE_RANGE;
    *cursor = (end + page - 1u) & ~((uint64_t)page - 1u);
    return XPL_PAYLOAD_OK;
}

enum xpl_payload_status xpl_payload_parse_android_v0(
    const void *header, size_t header_bytes, uint64_t file_bytes,
    struct xpl_android_v0_layout *out, struct xpl_payload_error *error)
{
    const unsigned char *data = (const unsigned char *)header;
    struct xpl_android_v0_layout layout;
    enum xpl_payload_status status;
    uint64_t cursor;
    uint32_t kernel_bytes, ramdisk_bytes, second_bytes;
    if (error) zero_bytes(error, sizeof(*error));
    if (out) zero_bytes(out, sizeof(*out));
    if (!out || (!header && header_bytes))
        return fail(error, XPL_PAYLOAD_ARGUMENT, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    if (header_bytes < 1632 || file_bytes < 1632 || header_bytes > file_bytes)
        return fail(error, XPL_PAYLOAD_IMAGE_TRUNCATED, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    if (!equals(data, 8, "ANDROID!"))
        return fail(error, XPL_PAYLOAD_IMAGE_HEADER, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    if (le32(data + 40) != 0)
        return fail(error, XPL_PAYLOAD_IMAGE_UNSUPPORTED, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    zero_bytes(&layout, sizeof(layout));
    layout.page_bytes = le32(data + 36);
    if (layout.page_bytes != 2048 && layout.page_bytes != 4096 &&
        layout.page_bytes != 8192 && layout.page_bytes != 16384)
        return fail(error, XPL_PAYLOAD_IMAGE_UNSUPPORTED, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    kernel_bytes = le32(data + 8); ramdisk_bytes = le32(data + 16); second_bytes = le32(data + 24);
    if (!kernel_bytes)
        return fail(error, XPL_PAYLOAD_IMAGE_HEADER, XPL_PAYLOAD_FIELD_KERNEL, 0, 0, 0, 0);
    cursor = layout.page_bytes;
    status = component_span(&cursor, kernel_bytes, layout.page_bytes, file_bytes, &layout.kernel, &layout.required_data_end);
    if (status != XPL_PAYLOAD_OK) return fail(error, status, XPL_PAYLOAD_FIELD_KERNEL, 0, 0, 0, 0);
    status = component_span(&cursor, ramdisk_bytes, layout.page_bytes, file_bytes, &layout.ramdisk, &layout.required_data_end);
    if (status != XPL_PAYLOAD_OK) return fail(error, status, XPL_PAYLOAD_FIELD_INITRD, 0, 0, 0, 0);
    status = component_span(&cursor, second_bytes, layout.page_bytes, file_bytes, &layout.second, &layout.required_data_end);
    if (status != XPL_PAYLOAD_OK) return fail(error, status, XPL_PAYLOAD_FIELD_NONE, 0, 0, 0, 0);
    layout.kernel_load_address = le32(data + 12);
    layout.ramdisk_load_address = le32(data + 20);
    layout.second_load_address = le32(data + 28);
    layout.tags_address = le32(data + 32);
    layout.os_version = le32(data + 44);
    layout.padded_payload_end = cursor;
    layout.trailing_bytes = file_bytes > cursor ? file_bytes - cursor : 0;
    copy_bytes(out, &layout, sizeof(layout));
    return XPL_PAYLOAD_OK;
}

const char *xpl_payload_status_string(enum xpl_payload_status status)
{
    switch (status) {
    case XPL_PAYLOAD_OK: return "Payload metadata accepted; execution compatibility is unverified";
    case XPL_PAYLOAD_ARGUMENT: return "Invalid parser argument or inventory";
    case XPL_PAYLOAD_CONFIG_TOO_LARGE: return "startup.txt exceeds the 4096-byte limit";
    case XPL_PAYLOAD_CONTROL_CHARACTER: return "Remove control bytes; use spaces and LF or CRLF lines";
    case XPL_PAYLOAD_SYNTAX: return "Expected a declarative key=value line";
    case XPL_PAYLOAD_UNKNOWN_KEY: return "Unknown startup key; use mode/kernel/initrd/dtb/abi/cmdline";
    case XPL_PAYLOAD_DUPLICATE_KEY: return "Remove the duplicate startup key";
    case XPL_PAYLOAD_VALUE_TOO_LONG: return "Value is too long; it was not truncated";
    case XPL_PAYLOAD_INVALID_VALUE: return "Unsupported mode or ABI value";
    case XPL_PAYLOAD_PATH_ESCAPE: return "Path must stay relative to the selected SD directory";
    case XPL_PAYLOAD_PATH_INVALID: return "Use a normalized relative path without colon, empty or dot segments";
    case XPL_PAYLOAD_MODE_CONFLICT: return "Mode conflicts with the explicit choice or external boot-image components";
    case XPL_PAYLOAD_NOT_FOUND: return "Required payload file was not found";
    case XPL_PAYLOAD_IO_ERROR: return "Filesystem query failed; check the SD directory";
    case XPL_PAYLOAD_AMBIGUOUS: return "Choose bootimg/split or select one split kernel explicitly";
    case XPL_PAYLOAD_EMPTY_FILE: return "Selected payload component is empty";
    case XPL_PAYLOAD_IMAGE_HEADER: return "Invalid Android boot header or missing kernel";
    case XPL_PAYLOAD_IMAGE_UNSUPPORTED: return "Only Android header v0 and 2/4/8/16-KiB pages are supported here";
    case XPL_PAYLOAD_IMAGE_TRUNCATED: return "Android header or component exceeds the available file";
    case XPL_PAYLOAD_IMAGE_RANGE: return "Android component arithmetic exceeds supported ranges";
    default: return "Unknown payload parser status";
    }
}
