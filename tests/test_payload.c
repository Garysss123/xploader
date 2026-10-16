/* SPDX-License-Identifier: MIT */
#include "xpl_payload.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct mock_entry { const char *path; uint64_t bytes; };
struct mock_fs { struct mock_entry entries[8]; size_t count; const char *io_error; unsigned calls; };

static enum xpl_payload_fs_result mock_stat(void *context, const char *path, uint64_t *bytes)
{
    struct mock_fs *fs = context;
    ++fs->calls;
    assert(path[0] != '/' && strcmp(path, "..") && strncmp(path, "../", 3) &&
           !strstr(path, "/../") && !strchr(path, '\\'));
    if (fs->io_error && !strcmp(path, fs->io_error)) return XPL_PAYLOAD_FS_ERROR;
    for (size_t i = 0; i < fs->count; ++i)
        if (!strcmp(path, fs->entries[i].path)) { *bytes = fs->entries[i].bytes; return XPL_PAYLOAD_FS_FILE; }
    return XPL_PAYLOAD_FS_MISSING;
}

static int all_zero(const void *p, size_t size)
{
    const unsigned char *bytes = p;
    for (size_t i = 0; i < size; ++i) if (bytes[i]) return 0;
    return 1;
}

static struct xpl_payload_config parse(const char *text)
{
    struct xpl_payload_config config;
    struct xpl_payload_error error;
    enum xpl_payload_status status = xpl_payload_parse_startup(text, strlen(text), &config, &error);
    if (status) fprintf(stderr, "Unexpected parser status %d at %zu:%zu for %s\n", status, error.line, error.column, text);
    assert(status == XPL_PAYLOAD_OK);
    return config;
}

static void bad(const char *text, enum xpl_payload_status expected)
{
    struct xpl_payload_config config;
    struct xpl_payload_error error;
    memset(&config, 0x5a, sizeof(config));
    assert(xpl_payload_parse_startup(text, strlen(text), &config, &error) == expected);
    assert(error.status == expected && all_zero(&config, sizeof(config)));
}

static void test_configs(void)
{
    struct xpl_payload_config config = parse("");
    struct xpl_payload_error error;
    char text[4200];
    assert(config.mode == XPL_PAYLOAD_AUTO && config.abi == XPL_PAYLOAD_ABI_UNSPECIFIED && !config.present);
    config = parse("# optional\r\nmode = split\r\nkernel=sub folder/Image.gz\r\ninitrd=\r\ndtb=board.dtb\r\nabi=bridge\r\ncmdline=root=/dev/ram0 a=b;$(literal)\r\n");
    assert(config.mode == XPL_PAYLOAD_SPLIT && config.abi == XPL_PAYLOAD_ABI_BRIDGE);
    assert(!strcmp(config.kernel, "sub folder/Image.gz") && !config.initrd[0]);
    assert(!strcmp(config.cmdline, "root=/dev/ram0 a=b;$(literal)"));
    bad("set KERNEL zImage", XPL_PAYLOAD_SYNTAX);
    bad("hash=123", XPL_PAYLOAD_UNKNOWN_KEY);
    bad("mode=split\nmode=split", XPL_PAYLOAD_DUPLICATE_KEY);
    bad("kernel=Image\nunknown=1", XPL_PAYLOAD_UNKNOWN_KEY);
    assert(xpl_payload_parse_startup("kernel=Image\nunknown=1", 22, &config, &error) == XPL_PAYLOAD_UNKNOWN_KEY);
    assert(error.line == 2 && error.column == 1 && !strcmp(error.subject, "unknown"));
    bad("mode=auto", XPL_PAYLOAD_INVALID_VALUE);
    bad("abi=linux-arm32", XPL_PAYLOAD_INVALID_VALUE);
    bad("kernel=", XPL_PAYLOAD_PATH_INVALID);
    bad("kernel=../Image", XPL_PAYLOAD_PATH_ESCAPE);
    bad("kernel=a/../../Image", XPL_PAYLOAD_PATH_ESCAPE);
    bad("kernel=/Image", XPL_PAYLOAD_PATH_ESCAPE);
    bad("kernel=a\\Image", XPL_PAYLOAD_PATH_ESCAPE);
    bad("kernel=C:/Image", XPL_PAYLOAD_PATH_INVALID);
    bad("kernel=./Image", XPL_PAYLOAD_PATH_INVALID);
    bad("kernel=a//Image", XPL_PAYLOAD_PATH_INVALID);
    bad("kernel=a/", XPL_PAYLOAD_PATH_INVALID);
    bad("kernel=Image\t", XPL_PAYLOAD_CONTROL_CHARACTER);
    bad("# comment\rkernel=Image", XPL_PAYLOAD_CONTROL_CHARACTER);
    assert(xpl_payload_parse_startup("kernel=Image\0bad", 16, &config, &error) == XPL_PAYLOAD_CONTROL_CHARACTER);
    memset(text, 'a', sizeof(text));
    memcpy(text, "kernel=", 7); text[7 + 255] = 0;
    config = parse(text); assert(strlen(config.kernel) == 255);
    text[7 + 255] = 'a'; text[7 + 256] = 0;
    bad(text, XPL_PAYLOAD_VALUE_TOO_LONG);
    memcpy(text, "cmdline=", 8); memset(text + 8, 'x', 2048); text[8 + 2047] = 0;
    config = parse(text); assert(strlen(config.cmdline) == 2047);
    text[8 + 2047] = 'x'; text[8 + 2048] = 0;
    bad(text, XPL_PAYLOAD_VALUE_TOO_LONG);
    assert(xpl_payload_parse_startup(text, 4097, &config, &error) == XPL_PAYLOAD_CONFIG_TOO_LARGE);
    assert(xpl_payload_parse_startup(NULL, 0, &config, NULL) == XPL_PAYLOAD_OK);
    assert(xpl_payload_parse_startup(NULL, 1, &config, NULL) == XPL_PAYLOAD_ARGUMENT);
}

static void test_selection(void)
{
    struct mock_fs fs = { .entries = {{"boot.img", 9000}, {"Image.gz", 1000}, {"initrd.gz", 100}, {"dtb", 80}}, .count = 4 };
    struct xpl_payload_inventory inventory;
    struct xpl_payload_descriptor descriptor;
    struct xpl_payload_error error;
    struct xpl_payload_config config = parse("");
    assert(xpl_payload_discover(mock_stat, &fs, &inventory, &error) == XPL_PAYLOAD_OK);
    assert(inventory.bootimg.present && inventory.kernel_count == 1);
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_AMBIGUOUS);
    assert(all_zero(&descriptor, sizeof(descriptor)) && error.field == XPL_PAYLOAD_FIELD_MODE);
    assert(xpl_payload_select(&config, XPL_PAYLOAD_BOOTIMG, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_OK);
    assert(!strcmp(descriptor.kernel.path, "boot.img") && !descriptor.initrd.present && !descriptor.runnable_verified);
    config = parse("mode=split\nkernel=Image.gz\nabi=linux-aarch64");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_OK);
    assert(descriptor.initrd.present && descriptor.dtb.present && descriptor.mode == XPL_PAYLOAD_SPLIT);
    assert(xpl_payload_select(&config, XPL_PAYLOAD_BOOTIMG, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_MODE_CONFLICT);
    config = parse("kernel=Image.gz\ninitrd=\ndtb=\nabi=bridge");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_OK);
    assert(!descriptor.initrd.present && !descriptor.dtb.present && descriptor.abi == XPL_PAYLOAD_ABI_BRIDGE && !descriptor.runnable_verified);
    config = parse("mode=bootimg\nkernel=boot.img\ndtb=other.dtb");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_MODE_CONFLICT);
    config = parse("kernel=missing\n");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_NOT_FOUND);
    assert(!strcmp(error.subject, "missing") && all_zero(&descriptor, sizeof(descriptor)));
    fs.entries[4] = (struct mock_entry){"Image", 2000}; fs.count = 5;
    assert(xpl_payload_discover(mock_stat, &fs, &inventory, &error) == XPL_PAYLOAD_OK);
    config = parse("mode=split");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_AMBIGUOUS);
    assert(error.field == XPL_PAYLOAD_FIELD_KERNEL && inventory.kernel_count == 2);
    fs.entries[1].bytes = 0; config = parse("kernel=Image.gz");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_EMPTY_FILE);
    fs.io_error = "Image.gz";
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_IO_ERROR);
    fs.io_error = NULL; fs.count = 0;
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_NOT_FOUND);
    assert(xpl_payload_discover(mock_stat, &fs, &inventory, &error) == XPL_PAYLOAD_OK);
    config = parse("");
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_NOT_FOUND);
    config.present = XPL_PAYLOAD_HAS(XPL_PAYLOAD_FIELD_KERNEL); memset(config.kernel, 'x', sizeof(config.kernel));
    unsigned calls = fs.calls;
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_VALUE_TOO_LONG);
    assert(fs.calls == calls);
    inventory.kernel_count = 4;
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, mock_stat, &fs, &descriptor, &error) == XPL_PAYLOAD_ARGUMENT);
}

static void put32(unsigned char *p, uint32_t value)
{
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}

static void header(unsigned char *h, uint32_t kernel, uint32_t initrd, uint32_t second, uint32_t page)
{
    memset(h, 0, 1632); memcpy(h, "ANDROID!", 8);
    put32(h + 8, kernel); put32(h + 16, initrd); put32(h + 24, second); put32(h + 36, page);
    put32(h + 12, 0x8000); put32(h + 20, 0x1000000);
}

static void layout_invariants(const struct xpl_android_v0_layout *layout, uint64_t file_bytes)
{
    const struct xpl_payload_span *parts[] = { &layout->kernel, &layout->ramdisk, &layout->second };
    uint64_t end = layout->page_bytes;
    assert(!layout->runnable_verified && layout->kernel.bytes);
    for (size_t i = 0; i < 3; ++i) {
        if (!parts[i]->bytes) { assert(parts[i]->offset == 0); continue; }
        assert(parts[i]->offset >= end && parts[i]->offset % layout->page_bytes == 0);
        assert(parts[i]->offset <= file_bytes && parts[i]->bytes <= file_bytes - parts[i]->offset);
        end = parts[i]->offset + parts[i]->bytes;
    }
    assert(end == layout->required_data_end && end <= file_bytes && layout->padded_payload_end >= end);
}

static void test_android(void)
{
    unsigned char h[1632]; struct xpl_android_v0_layout layout; struct xpl_payload_error error;
    header(h, 1, 3, 4, 4096);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 16384, &layout, &error) == XPL_PAYLOAD_OK);
    assert(layout.kernel.offset == 4096 && layout.ramdisk.offset == 8192 && layout.second.offset == 12288);
    assert(layout.required_data_end == 12292 && layout.padded_payload_end == 16384 && layout.trailing_bytes == 0);
    layout_invariants(&layout, 16384);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 12292, &layout, &error) == XPL_PAYLOAD_OK);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 12291, &layout, &error) == XPL_PAYLOAD_IMAGE_TRUNCATED);
    assert(all_zero(&layout, sizeof(layout)));
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 16400, &layout, &error) == XPL_PAYLOAD_OK && layout.trailing_bytes == 16);
    put32(h + 40, 1);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 16384, &layout, &error) == XPL_PAYLOAD_IMAGE_UNSUPPORTED);
    put32(h + 40, 376000); /* Old dt_size layout is not header v0. */
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 16384, &layout, &error) == XPL_PAYLOAD_IMAGE_UNSUPPORTED);
    header(h, 1, 0, 0, 4096);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 4097, &layout, &error) == XPL_PAYLOAD_OK && !layout.ramdisk.bytes);
    for (size_t n = 0; n < sizeof(h); ++n) {
        unsigned char *short_header = malloc(n ? n : 1); assert(short_header);
        memcpy(short_header, h, n);
        assert(xpl_payload_parse_android_v0(short_header, n, 4097, &layout, &error) == XPL_PAYLOAD_IMAGE_TRUNCATED);
        free(short_header);
    }
    header(h, UINT32_MAX, UINT32_MAX, UINT32_MAX, 16384);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), UINT64_MAX, &layout, &error) == XPL_PAYLOAD_OK);
    layout_invariants(&layout, UINT64_MAX);
    header(h, 0, 1, 0, 4096);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 99999, &layout, &error) == XPL_PAYLOAD_IMAGE_HEADER);
    header(h, 1, 1, 0, 4095);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 99999, &layout, &error) == XPL_PAYLOAD_IMAGE_UNSUPPORTED);
    h[0] = 0;
    assert(xpl_payload_parse_android_v0(h, sizeof(h), 99999, &layout, &error) == XPL_PAYLOAD_IMAGE_HEADER);
}

static uint32_t rng_state = 0x4c505858u;
static uint32_t random32(void)
{
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

static void fuzz(void)
{
    static const char *const seeds[] = {"", "mode=split\nkernel=Image.gz\ninitrd=initrd.gz\ndtb=dtb\nabi=linux-aarch64\ncmdline=a=b c=d\n",
        "mode=bootimg\nkernel=boot.img\n", "kernel=zImage\nabi=bridge\n", "kernel=a/../Image\n", "mode=split\nmode=bootimg\n"};
    struct mock_fs fs = { .entries = {{"boot.img", 4096}, {"Image.gz", 1024}, {"zImage", 4096}}, .count = 3 };
    struct xpl_payload_inventory inventory; struct xpl_payload_error error;
    assert(xpl_payload_discover(mock_stat, &fs, &inventory, &error) == XPL_PAYLOAD_OK);
    for (unsigned iteration = 0; iteration < 150000; ++iteration) {
        const char *seed = seeds[random32() % (sizeof(seeds) / sizeof(seeds[0]))];
        size_t seed_size = strlen(seed), length = (iteration % 3 == 0) ? random32() % 4100 : seed_size;
        unsigned char *data = malloc(length ? length : 1); assert(data);
        for (size_t i = 0; i < length; ++i) data[i] = i < seed_size ? (unsigned char)seed[i] : (unsigned char)(32 + random32() % 96);
        for (unsigned i = 0, n = random32() % 12; i < n && length; ++i) data[random32() % length] = random32();
        struct xpl_payload_config config; struct xpl_payload_descriptor descriptor;
        enum xpl_payload_status status = xpl_payload_parse_startup(data, length, &config, &error);
        if (status == XPL_PAYLOAD_OK) {
            assert(config.present <= 0x7e && strlen(config.kernel) < 256 && strlen(config.cmdline) < 2048);
            status = xpl_payload_select(&config, (enum xpl_payload_mode)(random32() % 3), &inventory, mock_stat, &fs, &descriptor, &error);
            if (status == XPL_PAYLOAD_OK) assert(descriptor.kernel.present && descriptor.kernel.bytes && !descriptor.runnable_verified);
            else assert(all_zero(&descriptor, sizeof(descriptor)));
        } else assert(all_zero(&config, sizeof(config)));
        free(data);
    }
    for (unsigned iteration = 0; iteration < 150000; ++iteration) {
        unsigned char h[1632]; struct xpl_android_v0_layout layout;
        const uint32_t pages[] = {2048, 4096, 8192, 16384, 0, 0xffffffff};
        header(h, random32(), random32(), random32(), pages[random32() % 6]);
        if (iteration % 5 == 0) h[random32() % sizeof(h)] = random32();
        if (iteration % 7 == 0) put32(h + 40, random32());
        uint64_t file_bytes = ((uint64_t)(random32() % 5) << 32) | random32();
        size_t available = iteration % 4 ? sizeof(h) : random32() % sizeof(h);
        enum xpl_payload_status status = xpl_payload_parse_android_v0(h, available, file_bytes, &layout, &error);
        if (status == XPL_PAYLOAD_OK) layout_invariants(&layout, file_bytes);
        else assert(all_zero(&layout, sizeof(layout)));
    }
    puts("PASS: 150000 malformed/structured configs + 150000 Android layouts under sanitizers");
}

static enum xpl_payload_fs_result host_stat(void *context, const char *path, uint64_t *bytes)
{
    char full[4096]; struct stat info;
    int length = snprintf(full, sizeof(full), "%s/%s", (const char *)context, path);
    if (length < 0 || (size_t)length >= sizeof(full)) return XPL_PAYLOAD_FS_ERROR;
    if (stat(full, &info)) return XPL_PAYLOAD_FS_MISSING;
    if (!S_ISREG(info.st_mode) || info.st_size < 0) return XPL_PAYLOAD_FS_ERROR;
    *bytes = (uint64_t)info.st_size;
    return XPL_PAYLOAD_FS_FILE;
}

static void integration(const char *folder)
{
    char path[4096]; unsigned char text[4097], h[1632]; size_t size;
    struct xpl_payload_config config, empty = parse(""); struct xpl_payload_inventory inventory;
    struct xpl_payload_descriptor descriptor; struct xpl_payload_error error;
    struct xpl_android_v0_layout layout;
    assert(snprintf(path, sizeof(path), "%s/startup.txt", folder) < (int)sizeof(path));
    FILE *file = fopen(path, "rb"); assert(file); size = fread(text, 1, sizeof(text), file); assert(!ferror(file)); fclose(file);
    assert(xpl_payload_parse_startup(text, size, &config, &error) == XPL_PAYLOAD_OK);
    assert(xpl_payload_discover(host_stat, (void *)folder, &inventory, &error) == XPL_PAYLOAD_OK);
    assert(xpl_payload_select(&config, XPL_PAYLOAD_AUTO, &inventory, host_stat, (void *)folder, &descriptor, &error) == XPL_PAYLOAD_OK);
    assert(descriptor.mode == XPL_PAYLOAD_SPLIT && !strcmp(descriptor.kernel.path, "Image.gz"));
    assert(descriptor.initrd.present && descriptor.dtb.present && !descriptor.runnable_verified);
    assert(xpl_payload_select(&empty, XPL_PAYLOAD_AUTO, &inventory, host_stat, (void *)folder, &descriptor, &error) == XPL_PAYLOAD_AMBIGUOUS);
    assert(xpl_payload_select(&empty, XPL_PAYLOAD_BOOTIMG, &inventory, host_stat, (void *)folder, &descriptor, &error) == XPL_PAYLOAD_OK);
    assert(descriptor.mode == XPL_PAYLOAD_BOOTIMG && !strcmp(descriptor.kernel.path, "boot.img"));
    assert(snprintf(path, sizeof(path), "%s/boot.img", folder) < (int)sizeof(path));
    file = fopen(path, "rb"); assert(file); assert(fread(h, 1, sizeof(h), file) == sizeof(h)); fclose(file);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), descriptor.kernel.bytes, &layout, &error) == XPL_PAYLOAD_OK);
    assert(layout.page_bytes == 4096 && layout.kernel.bytes == inventory.kernels[0].bytes + inventory.dtb.bytes);
    assert(layout.ramdisk.bytes == inventory.initrd.bytes && layout.padded_payload_end == descriptor.kernel.bytes);
    layout_invariants(&layout, descriptor.kernel.bytes);
    printf("PASS: actual17.1 split=%s, explicit bootimg, no-config ambiguity; boot bytes=%llu kernel=%llu ramdisk=%llu\n",
           inventory.kernels[0].path, (unsigned long long)descriptor.kernel.bytes,
           (unsigned long long)layout.kernel.bytes, (unsigned long long)layout.ramdisk.bytes);
}

static void actual_header(const char *path, uint64_t file_bytes, uint64_t kernel_bytes, uint64_t ramdisk_bytes)
{
    unsigned char h[1632]; struct xpl_android_v0_layout layout; struct xpl_payload_error error;
    FILE *file = fopen(path, "rb"); assert(file); assert(fread(h, 1, sizeof(h), file) == sizeof(h)); fclose(file);
    assert(xpl_payload_parse_android_v0(h, sizeof(h), file_bytes, &layout, &error) == XPL_PAYLOAD_OK);
    assert(layout.page_bytes == 4096 && layout.kernel.bytes == kernel_bytes && layout.ramdisk.bytes == ramdisk_bytes);
    assert(layout.padded_payload_end == file_bytes && !layout.trailing_bytes);
    layout_invariants(&layout, file_bytes);
    printf("PASS: actual19.1 header bounds: file=%llu kernel=%llu ramdisk=%llu\n",
           (unsigned long long)file_bytes, (unsigned long long)kernel_bytes, (unsigned long long)ramdisk_bytes);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--integration")) { integration(argv[2]); return 0; }
    if (argc == 6 && !strcmp(argv[1], "--android-header")) {
        actual_header(argv[2], strtoull(argv[3], NULL, 10), strtoull(argv[4], NULL, 10), strtoull(argv[5], NULL, 10)); return 0;
    }
    assert(argc == 1);
    test_configs(); test_selection(); test_android(); fuzz();
    puts("PASS: startup parser, explicit/ambiguous selection, path rejection and Android v0 spans");
    return 0;
}
