/* Original xploader implementation, based on Microsoft exFAT specification:
 * https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification
 * Consulted 2026-10-13. Filesystem metadata checksums are format integrity
 * checks, unrelated to payload identity, hashes, or permission to boot. */
#include "xpl_exfat.h"

#define EOC UINT32_C(0xffffffff)
#define DIR_MAX UINT64_C(268435456)

static void clear(void *p, size_t n) {
    uint8_t *d = p; while (n--) *d++ = 0;
}
static void copy(void *d0, const void *s0, size_t n) {
    uint8_t *d = d0; const uint8_t *s = s0; while (n--) *d++ = *s++;
}
static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64(const uint8_t *p) {
    return u32(p) | ((uint64_t)u32(p + 4) << 32);
}
static uint32_t sum32(uint32_t s, uint8_t c) {
    return (s >> 1 | s << 31) + c;
}
static uint16_t sum16(uint16_t s, uint8_t c) {
    return (uint16_t)((s >> 1 | s << 15) + c);
}
static int cluster_ok(const xpl_exfat *f, uint32_t c) {
    return c >= 2 && (uint64_t)c < (uint64_t)f->cluster_count + 2;
}
static int sector(xpl_exfat *f, uint64_t rel, int bitmap, const uint8_t **out) {
    uint64_t *tag = bitmap ? &f->bitmap_cache_lba : &f->cache_lba;
    uint8_t *valid = bitmap ? &f->bitmap_cache_valid : &f->cache_valid;
    uint8_t *buf = bitmap ? f->bitmap_cache : f->cache;
    if (rel >= f->volume_count512) return XPL_EXFAT_EBOUNDS;
    if (!*valid || *tag != rel) {
        *valid = 0;
        if (f->read(f->read_ctx, f->volume_start512 + rel, 1, buf))
            return XPL_EXFAT_EIO;
        *tag = rel; *valid = 1;
    }
    *out = buf;
    return 0;
}
static uint64_t cluster_sector(const xpl_exfat *f, uint32_t c) {
    return f->heap_start512 + (uint64_t)(c - 2) * (f->cluster_bytes >> 9);
}
static int fat_next(xpl_exfat *f, uint32_t c, uint32_t *next) {
    const uint8_t *b; uint64_t off = (uint64_t)c * 4; int r;
    if (!cluster_ok(f, c)) return XPL_EXFAT_EBOUNDS;
    r = sector(f, f->fat_start512 + (off >> 9), 0, &b);
    if (r) return r;
    *next = u32(b + (off & 511));
    if (*next != EOC && !cluster_ok(f, *next)) return XPL_EXFAT_EFORMAT;
    return 0;
}

/* Brent's cycle detector needs no cluster-sized or volume-sized allocation. */
typedef struct { uint32_t anchor, span, power; } cycle;
static int cycle_step(cycle *s, uint32_t next) {
    if (next == s->anchor) return XPL_EXFAT_ELOOP;
    ++s->span;
    if (s->span == s->power) {
        s->anchor = next; s->span = 0;
        if (s->power <= UINT32_MAX / 2) s->power *= 2;
    }
    return 0;
}
static int allocated(xpl_exfat *f, uint32_t c) {
    uint64_t byte; uint32_t target, cur, idx; const uint8_t *b; int r;
    if (!cluster_ok(f, c)) return XPL_EXFAT_EBOUNDS;
    byte = (uint64_t)(c - 2) >> 3;
    if (byte >= f->bitmap_length) return XPL_EXFAT_EFORMAT;
    target = (uint32_t)(byte / f->cluster_bytes);
    cur = f->bitmap_cursor_cluster; idx = f->bitmap_cursor_index;
    if (!cur || target < idx) { cur = f->bitmap_cluster; idx = 0; }
    /* Bitmap's entire FAT chain was verified before enabling this function. */
    while (idx < target) {
        r = fat_next(f, cur, &cur);
        if (r) return r;
        if (cur == EOC) return XPL_EXFAT_EFORMAT;
        if (++idx > XPL_EXFAT_CHAIN_LIMIT) return XPL_EXFAT_ELIMIT;
    }
    f->bitmap_cursor_cluster = cur; f->bitmap_cursor_index = idx;
    r = sector(f, cluster_sector(f, cur) +
               ((byte % f->cluster_bytes) >> 9), 1, &b);
    if (r) return r;
    return (b[byte & 511] & (1u << ((c - 2) & 7))) ? 0 : XPL_EXFAT_EFORMAT;
}

/* Validate exact allocation length and terminal FAT entry; a short read must
 * not mask an invalid remainder of the stream. check_bitmap=0 bootstraps the
 * root and allocation bitmap, before their allocation bits can be located. */
static int validate(xpl_exfat *f, uint32_t first, uint64_t length,
                    int contiguous, int check_bitmap) {
    uint64_t count = length / f->cluster_bytes + (length % f->cluster_bytes != 0);
    uint32_t cur = first, next; uint64_t i; int r;
    cycle cyc = { first, 0, 1 };
    if (!count) return first || contiguous ? XPL_EXFAT_EFORMAT : 0;
    if (!cluster_ok(f, first) || count > f->cluster_count)
        return XPL_EXFAT_EBOUNDS;
    if (count > XPL_EXFAT_CHAIN_LIMIT) return XPL_EXFAT_ELIMIT;
    if (contiguous && count > (uint64_t)f->cluster_count + 2 - first)
        return XPL_EXFAT_EBOUNDS;
    for (i = 0; i < count; ++i) {
        if (check_bitmap && (r = allocated(f, cur))) return r;
        if (contiguous) { ++cur; continue; }
        r = fat_next(f, cur, &next); if (r) return r;
        if (i + 1 == count) return next == EOC ? 0 : XPL_EXFAT_EFORMAT;
        if (next == EOC) return XPL_EXFAT_EFORMAT;
        r = cycle_step(&cyc, next); if (r) return r;
        cur = next;
    }
    return 0;
}
static int root_size(xpl_exfat *f) {
    uint32_t cur = f->root_cluster, next, count = 0; int r;
    cycle cyc = { cur, 0, 1 };
    do {
        if (++count > XPL_EXFAT_CHAIN_LIMIT) return XPL_EXFAT_ELIMIT;
        if ((uint64_t)count * f->cluster_bytes > DIR_MAX || count > f->cluster_count)
            return XPL_EXFAT_EBOUNDS;
        r = fat_next(f, cur, &next); if (r) return r;
        if (next != EOC) { r = cycle_step(&cyc, next); if (r) return r; }
        cur = next;
    } while (cur != EOC);
    f->root_length = (uint64_t)count * f->cluster_bytes;
    return 0;
}
static int node_check(xpl_exfat *f, const xpl_exfat_node *n) {
    if (n->no_fat_chain > 1 || n->is_root > 1 ||
        n->valid_data_length > n->data_length) return XPL_EXFAT_EFORMAT;
    if (n->attributes & XPL_EXFAT_ATTR_DIRECTORY) {
        if (n->valid_data_length != n->data_length || n->data_length > DIR_MAX ||
            n->data_length % f->cluster_bytes) return XPL_EXFAT_EFORMAT;
    }
    if (n->is_root && (!(n->attributes & XPL_EXFAT_ATTR_DIRECTORY) ||
        n->first_cluster != f->root_cluster || n->data_length != f->root_length ||
        n->no_fat_chain)) return XPL_EXFAT_EFORMAT;
    return validate(f, n->first_cluster, n->data_length, n->no_fat_chain, 1);
}
static void dir_init(xpl_exfat *f, xpl_exfat_dir *d, const xpl_exfat_node *n) {
    clear(d, sizeof *d); d->fs = f; d->length = n->data_length;
    d->first_cluster = n->first_cluster; d->cluster = n->first_cluster;
    d->no_fat_chain = n->no_fat_chain; d->is_root = n->is_root;
}
static int raw_entry(xpl_exfat_dir *d, uint8_t out[32]) {
    xpl_exfat *f = d->fs; const uint8_t *b; uint32_t idx; int r;
    if (d->position >= d->length) return XPL_EXFAT_END;
    idx = (uint32_t)(d->position / f->cluster_bytes);
    while (d->cluster_index < idx) {
        if (d->no_fat_chain) ++d->cluster;
        else { r = fat_next(f, d->cluster, &d->cluster); if (r) return r; }
        ++d->cluster_index;
        if (!cluster_ok(f, d->cluster)) return XPL_EXFAT_EFORMAT;
    }
    r = sector(f, cluster_sector(f, d->cluster) +
               ((d->position % f->cluster_bytes) >> 9), 0, &b);
    if (r) return r;
    copy(out, b + (d->position & 511), 32); d->position += 32;
    return 0;
}

int xpl_exfat_mount(xpl_exfat *f, xpl_exfat_read_fn read, void *ctx,
                   uint64_t start, uint64_t count) {
    const uint8_t *b; uint8_t boot[120], ent[32]; uint64_t vol, fatlen, heap, clusters;
    uint32_t fat, csum = 0, upcluster = 0; uint64_t uplength = 0;
    uint32_t units, i, j; unsigned bitmaps = 0, upcases = 0;
    xpl_exfat_node root; xpl_exfat_dir d; int r;
    if (!f || !read) return XPL_EXFAT_EINVAL;
    clear(f, sizeof *f);
    if (count < 24 || start > UINT64_MAX - (count - 1)) return XPL_EXFAT_EBOUNDS;
    f->read = read; f->read_ctx = ctx; f->volume_start512 = start; f->volume_count512 = count;
    r = sector(f, 0, 0, &b); if (r) return r;
    if (u16(b + 510) != 0xaa55 || b[0] != 0xeb || b[1] != 0x76 || b[2] != 0x90)
        return XPL_EXFAT_EFORMAT;
    { static const uint8_t sig[8] = { 'E','X','F','A','T',' ',' ',' ' };
      for (i = 0; i < 8; ++i) if (b[3 + i] != sig[i]) return XPL_EXFAT_EFORMAT; }
    for (i = 11; i < 64; ++i) if (b[i]) return XPL_EXFAT_EFORMAT;
    copy(boot, b, sizeof boot);
    if (boot[108] < 9 || boot[108] > 12 || boot[109] > 25 - boot[108])
        return XPL_EXFAT_EFORMAT;
    units = 1u << (boot[108] - 9);
    if (count < 24u * units) return XPL_EXFAT_EBOUNDS;
    for (i = 0; i < 11u * units; ++i) {
        r = sector(f, i, 0, &b); if (r) return r;
        if (i >= units && i < 9u * units && (i + 1) % units == 0 &&
            u32(b + 508) != UINT32_C(0xaa550000)) return XPL_EXFAT_EFORMAT;
        for (j = 0; j < 512; ++j) {
            if (i == 0 && (j == 106 || j == 107 || j == 112)) continue;
            csum = sum32(csum, b[j]);
        }
    }
    for (i = 11u * units; i < 12u * units; ++i) {
        r = sector(f, i, 0, &b); if (r) return r;
        for (j = 0; j < 512; j += 4) if (u32(b + j) != csum) return XPL_EXFAT_EFORMAT;
    }
    if (boot[105] != 1) return XPL_EXFAT_EUNSUPPORTED;
    if (boot[104] > 99 || (boot[110] != 1 && boot[110] != 2) ||
        (boot[112] > 100 && boot[112] != 255)) return XPL_EXFAT_EFORMAT;
    f->sectors_shift = boot[108]; f->cluster_shift = boot[109];
    f->number_of_fats = boot[110]; f->volume_flags = u16(boot + 106);
    f->active_fat = (uint8_t)(f->volume_flags & 1);
    if (f->active_fat >= f->number_of_fats) return XPL_EXFAT_EFORMAT;
    vol = u64(boot + 72); fat = u32(boot + 80); fatlen = u32(boot + 84);
    heap = u32(boot + 88); f->cluster_count = u32(boot + 92);
    f->root_cluster = u32(boot + 96); f->serial = u32(boot + 100);
    if (vol < (UINT64_C(1) << (20 - boot[108])) || vol > count / units ||
        fat < 24 || !f->cluster_count || f->cluster_count > 0xfffffff5u ||
        !cluster_ok(f, f->root_cluster)) return XPL_EXFAT_EBOUNDS;
    if (fatlen * (UINT64_C(1) << boot[108]) < ((uint64_t)f->cluster_count + 2) * 4 ||
        (uint64_t)fat + fatlen * f->number_of_fats > heap || heap > vol)
        return XPL_EXFAT_EBOUNDS;
    clusters = (vol - heap) >> f->cluster_shift;
    if (clusters > 0xfffffff5u) clusters = 0xfffffff5u;
    if (clusters != f->cluster_count) return XPL_EXFAT_EFORMAT;
    f->volume_count512 = vol * units;
    f->fat_start512 = ((uint64_t)fat + fatlen * f->active_fat) * units;
    f->heap_start512 = heap * units;
    f->cluster_bytes = 1u << (f->sectors_shift + f->cluster_shift);
    r = root_size(f); if (r) return r;
    clear(&root, sizeof root); root.data_length = f->root_length;
    root.first_cluster = f->root_cluster; root.is_root = 1;
    dir_init(f, &d, &root);
    while (!(r = raw_entry(&d, ent))) {
        if (!ent[0]) break;
        if (!(ent[0] & 0x80)) continue;
        if (ent[0] == 0x81) {
            unsigned id = ent[1] & 1u;
            if (id >= f->number_of_fats || (bitmaps & (1u << id))) return XPL_EXFAT_EFORMAT;
            bitmaps |= 1u << id;
            if (id == f->active_fat) {
                f->bitmap_cluster = u32(ent + 20); f->bitmap_length = u64(ent + 24);
            }
        } else if (ent[0] == 0x82) {
            if (++upcases != 1) return XPL_EXFAT_EFORMAT;
            upcluster = u32(ent + 20); uplength = u64(ent + 24);
        } else if (ent[0] == 0x85 || (ent[0] & 0xe0) == 0xa0) {
            unsigned remain = ent[1];
            while (remain--) {
                r = raw_entry(&d, ent); if (r) return r < 0 ? r : XPL_EXFAT_EFORMAT;
                if ((ent[0] & 0xc0) != 0xc0) return XPL_EXFAT_EFORMAT;
            }
        } else if (ent[0] != 0x83) return XPL_EXFAT_EUNSUPPORTED;
    }
    if (r < 0) return r;
    if (bitmaps != ((1u << f->number_of_fats) - 1) || upcases != 1 ||
        f->bitmap_length < ((uint64_t)f->cluster_count + 7) / 8 ||
        !uplength || uplength > 131072 || (uplength & 1)) return XPL_EXFAT_EFORMAT;
    r = validate(f, f->bitmap_cluster, f->bitmap_length, 0, 0); if (r) return r;
    r = validate(f, f->bitmap_cluster, f->bitmap_length, 0, 1); if (r) return r;
    r = validate(f, f->root_cluster, f->root_length, 0, 1); if (r) return r;
    r = validate(f, upcluster, uplength, 0, 1); if (r) return r;
    f->read_limit_blocks = 1; f->mounted = 1;
    return 0;
}
int xpl_exfat_set_read_limit(xpl_exfat *f, unsigned blocks) {
    if (!f || !f->mounted || !blocks || blocks > 128u) return XPL_EXFAT_EINVAL;
    f->read_limit_blocks = (uint16_t)blocks; return 0;
}

int xpl_exfat_root(xpl_exfat *f, xpl_exfat_node *out) {
    if (!f || !out || !f->mounted) return XPL_EXFAT_EINVAL;
    clear(out, sizeof *out); out->first_cluster = f->root_cluster;
    out->data_length = out->valid_data_length = f->root_length;
    out->attributes = XPL_EXFAT_ATTR_DIRECTORY; out->is_root = 1; out->name[0] = '/';
    return 0;
}
int xpl_exfat_dir_open(xpl_exfat *f, xpl_exfat_dir *d, const xpl_exfat_node *n) {
    int r;
    if (!f || !d || !n || !f->mounted) return XPL_EXFAT_EINVAL;
    clear(d, sizeof *d); d->error = XPL_EXFAT_EINVAL;
    if (!(n->attributes & XPL_EXFAT_ATTR_DIRECTORY)) return XPL_EXFAT_ENOTDIR;
    r = node_check(f, n); if (r) return r;
    dir_init(f, d, n); return 0;
}
static int utf8_name(const uint16_t *in, unsigned n, char *out) {
    unsigned i, used = 0;
    for (i = 0; i < n; ++i) {
        uint32_t c = in[i];
        if (c < 32 || c == '"' || c == '*' || c == '/' || c == ':' ||
            c == '<' || c == '>' || c == '?' || c == '\\' || c == '|') return XPL_EXFAT_EFORMAT;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == n || in[i] < 0xdc00 || in[i] > 0xdfff) return XPL_EXFAT_EFORMAT;
            c = 0x10000 + ((c - 0xd800) << 10) + in[i] - 0xdc00;
        } else if (c >= 0xdc00 && c <= 0xdfff) return XPL_EXFAT_EFORMAT;
        if (c < 0x80) out[used++] = (char)c;
        else if (c < 0x800) {
            out[used++] = (char)(0xc0 | (c >> 6)); out[used++] = (char)(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out[used++] = (char)(0xe0 | (c >> 12)); out[used++] = (char)(0x80 | ((c >> 6) & 63));
            out[used++] = (char)(0x80 | (c & 63));
        } else {
            out[used++] = (char)(0xf0 | (c >> 18)); out[used++] = (char)(0x80 | ((c >> 12) & 63));
            out[used++] = (char)(0x80 | ((c >> 6) & 63)); out[used++] = (char)(0x80 | (c & 63));
        }
    }
    out[used] = 0;
    if ((used == 1 && out[0] == '.') || (used == 2 && out[0] == '.' && out[1] == '.'))
        return XPL_EXFAT_EFORMAT;
    return 0;
}
static int next_node(xpl_exfat_dir *d, xpl_exfat_node *n) {
    uint8_t e[32]; uint16_t chars[255], expected, checksum; unsigned count, i, j, namelen = 0;
    unsigned names = 0; int r;
    while (!(r = raw_entry(d, e))) {
        if (!e[0]) return XPL_EXFAT_END;
        if (!(e[0] & 0x80)) continue;
        if (e[0] == 0x85) break;
        if (e[0] == 0x81 || e[0] == 0x82 || e[0] == 0x83) {
            if (!d->is_root) return XPL_EXFAT_EFORMAT;
            continue;
        }
        if (!(e[0] & 0x20)) return XPL_EXFAT_EUNSUPPORTED;
        if (e[0] & 0x40) return XPL_EXFAT_EFORMAT;
        count = e[1];
        while (count--) {
            r = raw_entry(d, e); if (r) return r < 0 ? r : XPL_EXFAT_EFORMAT;
            if ((e[0] & 0xc0) != 0xc0) return XPL_EXFAT_EFORMAT;
        }
    }
    if (r) return r;
    clear(n, sizeof *n); count = e[1]; expected = u16(e + 2); checksum = 0;
    n->attributes = u16(e + 4);
    if (count < 2) return XPL_EXFAT_EFORMAT;
    for (j = 0; j < 32; ++j) if (j != 2 && j != 3) checksum = sum16(checksum, e[j]);
    for (i = 0; i < count; ++i) {
        r = raw_entry(d, e); if (r) return r < 0 ? r : XPL_EXFAT_EFORMAT;
        for (j = 0; j < 32; ++j) checksum = sum16(checksum, e[j]);
        if (i == 0) {
            if (e[0] != 0xc0 || !(e[1] & 1) || !e[3]) return XPL_EXFAT_EFORMAT;
            n->no_fat_chain = (uint8_t)((e[1] >> 1) & 1); namelen = e[3];
            names = (namelen + 14) / 15;
            if (count < names + 1) return XPL_EXFAT_EFORMAT;
            n->valid_data_length = u64(e + 8); n->first_cluster = u32(e + 20);
            n->data_length = u64(e + 24);
        } else if (i <= names) {
            if (e[0] != 0xc1 || (e[1] & 3)) return XPL_EXFAT_EFORMAT;
            for (j = 0; j < 15 && (i - 1) * 15 + j < namelen; ++j)
                chars[(i - 1) * 15 + j] = u16(e + 2 + j * 2);
        } else if ((e[0] & 0xe0) != 0xe0) return XPL_EXFAT_EUNSUPPORTED;
    }
    if (checksum != expected) return XPL_EXFAT_EFORMAT;
    r = utf8_name(chars, namelen, n->name); if (r) return r;
    return node_check(d->fs, n);
}
int xpl_exfat_dir_next(xpl_exfat_dir *d, xpl_exfat_node *out) {
    int r;
    if (!d || !out || !d->fs || !d->fs->mounted) return XPL_EXFAT_EINVAL;
    if (d->error) return d->error;
    if (d->ended) return XPL_EXFAT_END;
    r = next_node(d, out);
    if (r) { clear(out, sizeof *out); if (r == XPL_EXFAT_END) d->ended = 1; else d->error = r; }
    return r;
}
int xpl_exfat_lookup(xpl_exfat *f, const char *path, xpl_exfat_node *out) {
    xpl_exfat_node node; xpl_exfat_dir d; size_t len = 0, at = 0, begin, i; int r;
    if (!f || !path || !out || !f->mounted) return XPL_EXFAT_EINVAL;
    while (len < XPL_EXFAT_PATH_BYTES && path[len]) ++len;
    if (len == XPL_EXFAT_PATH_BYTES) return XPL_EXFAT_ELIMIT;
    r = xpl_exfat_root(f, &node); if (r) return r;
    while (at < len) {
        while (at < len && path[at] == '/') ++at;
        if (at == len) {
            if (!(node.attributes & XPL_EXFAT_ATTR_DIRECTORY)) return XPL_EXFAT_ENOTDIR;
            break;
        }
        begin = at; while (at < len && path[at] != '/') ++at;
        if (at - begin >= XPL_EXFAT_NAME_BYTES) return XPL_EXFAT_ELIMIT;
        if ((at - begin == 1 && path[begin] == '.') ||
            (at - begin == 2 && path[begin] == '.' && path[begin + 1] == '.')) return XPL_EXFAT_EINVAL;
        r = xpl_exfat_dir_open(f, &d, &node); if (r) return r;
        while (!(r = xpl_exfat_dir_next(&d, &node))) {
            for (i = 0; i < at - begin && node.name[i] == path[begin + i]; ++i) { }
            if (i == at - begin && !node.name[i]) break;
        }
        if (r) return r == XPL_EXFAT_END ? XPL_EXFAT_ENOTFOUND : r;
    }
    copy(out, &node, sizeof node); return 0;
}
int xpl_exfat_pread(xpl_exfat *f, const xpl_exfat_node *n, uint64_t off,
                   void *out, size_t len, size_t *got) {
    uint8_t *dst = out; const uint8_t *b; uint32_t cur, idx = 0, target;
    size_t take, done = 0; uint64_t remain; int r;
    if (got) *got = 0;
    if (!f || !n || !got || (!out && len) || !f->mounted) return XPL_EXFAT_EINVAL;
    if (n->attributes & XPL_EXFAT_ATTR_DIRECTORY) return XPL_EXFAT_EINVAL;
    r = node_check(f, n); if (r) return r;
    if (!len) return 0;
    if (off >= n->data_length) return XPL_EXFAT_END;
    remain = n->data_length - off; if (remain < len) len = (size_t)remain;
    cur = n->first_cluster;
    while (done < len) {
        if (off >= n->valid_data_length) { clear(dst + done, len - done); done = len; break; }
        target = (uint32_t)(off / f->cluster_bytes);
        if (n->no_fat_chain) cur = n->first_cluster + target;
        else while (idx < target) {
            r = fat_next(f, cur, &cur); if (r) { *got = done; return r; }
            if (!cluster_ok(f, cur)) { *got = done; return XPL_EXFAT_EFORMAT; }
            ++idx;
        }
        if (!(off & 511u) && f->read_limit_blocks > 1u && f->read_limit_blocks <= 128u) {
            uint64_t rel = cluster_sector(f, cur) + ((off % f->cluster_bytes) >> 9);
            uint64_t bytes = len - done;
            uint64_t cluster_left = f->cluster_bytes - off % f->cluster_bytes;
            if (bytes > n->valid_data_length - off) bytes = n->valid_data_length - off;
            if (bytes > cluster_left) bytes = cluster_left;
            uint32_t blocks = (uint32_t)(bytes / 512u);
            if (blocks > f->read_limit_blocks) blocks = f->read_limit_blocks;
            if (blocks > 1u) {
                if (rel >= f->volume_count512 || blocks > f->volume_count512 - rel) {
                    *got = done; return XPL_EXFAT_EBOUNDS;
                }
                if (f->read(f->read_ctx, f->volume_start512 + rel, blocks, dst + done)) {
                    *got = done; return XPL_EXFAT_EIO;
                }
                take = (size_t)blocks * 512u; off += take; done += take; continue;
            }
        }
        take = 512 - (size_t)(off & 511);
        if (take > len - done) take = len - done;
        if (take > n->valid_data_length - off) take = (size_t)(n->valid_data_length - off);
        r = sector(f, cluster_sector(f, cur) + ((off % f->cluster_bytes) >> 9), 0, &b);
        if (r) { *got = done; return r; }
        copy(dst + done, b + (off & 511), take); off += take; done += take;
    }
    *got = done; return 0;
}
int xpl_exfat_file_sector(xpl_exfat *f,const xpl_exfat_node *n,uint64_t offset,uint64_t *lba){
    if(!f||!n||!lba||!f->mounted||(offset&511u)||(n->attributes&0x11u)||
       offset>n->valid_data_length||512u>n->valid_data_length-offset)return XPL_EXFAT_EINVAL;
    int e=node_check(f,n);if(e)return e;
    uint32_t cluster=n->first_cluster,target=(uint32_t)(offset/f->cluster_bytes);
    if(n->no_fat_chain)cluster+=target;
    else for(uint32_t i=0;i<target;i++){if((e=fat_next(f,cluster,&cluster)))return e;}
    if((e=allocated(f,cluster)))return e;
    uint64_t sector=cluster_sector(f,cluster)+((offset%f->cluster_bytes)>>9);
    if(sector>=f->volume_count512||f->volume_start512>UINT64_MAX-sector)return XPL_EXFAT_EBOUNDS;
    *lba=f->volume_start512+sector;return 0;
}
