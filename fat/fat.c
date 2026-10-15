// fat.c - a small FAT32 driver: read, create/replace, delete, mkdir.
//
// Paths are '/'-separated from the root; names match case-insensitively
// against the long name (if any) or the 8.3 name. New entries get 8.3 names
// only (no long-name entries are written); an all-lowercase name is stored
// with the NT lowercase flags, so PCs show it in lowercase.
//
// One FAT sector is cached; writes go to every FAT copy. FSInfo's free count
// is set to "unknown" (0xffffffff) on the first write, which is valid FAT32
// and saves keeping it exact. Timestamps: none (no clock), the date is
// fixed at 2026-01-01.
//
// Shared by the bare-metal programs (like libc/ and usb/); see fat.h for what
// the program provides.

#include <string.h>
#include <stdint.h>

#include "kstb.h"                       // the program's: FAT_BUF
#include "fat.h"

#define EOC         0x0ffffff8u
#define ATTR_RO     0x01
#define ATTR_DIR    0x10
#define ATTR_VOLID  0x08
#define ATTR_LFN    0x0f
#define FIXED_DATE  ((46 << 9) | (1 << 5) | 1)      // 2026-01-01 (years since 1980)

static int mounted, fsinfo_dirty_done;
static uint32_t part, spc, fat_start, fat_sectors, nfats, data_start, root_cluster;
static uint32_t nclusters, fsinfo_sector, next_hint = 2;

// Work buffers in the program's RAM area (FAT_BUF, 64 KB)
static uint8_t *const clbuf = (uint8_t *)FAT_BUF;              // one cluster (up to 32 KB)
static uint8_t *const fatbuf = (uint8_t *)FAT_BUF + 0x8000;    // the cached FAT sector
static uint8_t *const secbuf = (uint8_t *)FAT_BUF + 0x8200;    // misc sector
static uint32_t fat_cached = ~0u;
static int fat_dirty;

static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint32_t cl_lba(uint32_t c) { return part + data_start + (c - 2) * spc; }

// ---------------- FAT ----------------

static int fat_flush(void)
{
    if (!fat_dirty)
        return 0;
    for (uint32_t i = 0; i < nfats; i++)
        if (fat_blk_write(part + fat_start + i * fat_sectors + fat_cached, 1, fatbuf))
            return FAT_EIO;
    fat_dirty = 0;
    return 0;
}

static int fat_load(uint32_t sector)
{
    if (sector == fat_cached)
        return 0;
    if (fat_flush())
        return FAT_EIO;
    if (fat_blk_read(part + fat_start + sector, 1, fatbuf)) {
        fat_cached = ~0u;
        return FAT_EIO;
    }
    fat_cached = sector;
    return 0;
}

static int fat_get(uint32_t c, uint32_t *v)
{
    if (fat_load(c / 128))
        return FAT_EIO;
    *v = rd32(fatbuf + c % 128 * 4) & 0x0fffffff;
    return 0;
}

static int fat_set(uint32_t c, uint32_t v)
{
    if (fat_load(c / 128))
        return FAT_EIO;
    uint8_t *p = fatbuf + c % 128 * 4;
    wr32(p, (rd32(p) & 0xf0000000) | (v & 0x0fffffff));
    fat_dirty = 1;
    return 0;
}

// First write: mark FSInfo's free count and hint unknown (both valid FAT32).
static int fsinfo_unknown(void)
{
    if (fsinfo_dirty_done || !fsinfo_sector || fsinfo_sector == 0xffff)
        return 0;
    if (fat_blk_read(part + fsinfo_sector, 1, secbuf))
        return FAT_EIO;
    if (rd32(secbuf) == 0x41615252 && rd32(secbuf + 484) == 0x61417272) {
        wr32(secbuf + 488, 0xffffffff);
        wr32(secbuf + 492, 0xffffffff);
        if (fat_blk_write(part + fsinfo_sector, 1, secbuf))
            return FAT_EIO;
    }
    fsinfo_dirty_done = 1;
    return 0;
}

static int alloc_cluster(uint32_t prev, uint32_t *out)
{
    uint32_t v;
    for (uint32_t n = 0; n < nclusters; n++) {
        uint32_t c = next_hint + n;
        if (c >= nclusters + 2)
            c -= nclusters;
        if (fat_get(c, &v))
            return FAT_EIO;
        if (v == 0) {
            if (fat_set(c, 0x0fffffff) || (prev && fat_set(prev, c)))
                return FAT_EIO;
            next_hint = c + 1 < nclusters + 2 ? c + 1 : 2;
            *out = c;
            return 0;
        }
    }
    return FAT_ENOSPC;
}

static int free_chain(uint32_t c)
{
    uint32_t next;
    while (c >= 2 && c < EOC) {
        if (fat_get(c, &next) || fat_set(c, 0))
            return FAT_EIO;
        if (c < next_hint)
            next_hint = c;
        c = next;
    }
    return 0;
}

// ---------------- mount ----------------

int fat_mount(uint32_t part_lba)
{
    mounted = 0;
    fat_cached = ~0u, fat_dirty = 0, fsinfo_dirty_done = 0;
    uint8_t *b = secbuf;
    if (fat_blk_read(part_lba, 1, b))
        return FAT_EIO;
    if (rd16(b + 510) != 0xaa55 || rd16(b + 11) != 512 || rd16(b + 17) != 0 || rd32(b + 36) == 0)
        return FAT_ENOFS;               // FAT32 has no fixed root directory and a 32-bit FAT size
    part = part_lba;
    spc = b[13];
    if (spc == 0 || spc > 64 || (spc & (spc - 1)))
        return FAT_ENOFS;
    fat_start = rd16(b + 14);
    nfats = b[16];
    fat_sectors = rd32(b + 36);
    root_cluster = rd32(b + 44);
    fsinfo_sector = rd16(b + 48);
    uint32_t total = rd32(b + 32);
    data_start = fat_start + nfats * fat_sectors;
    nclusters = (total - data_start) / spc;
    if (nclusters > fat_sectors * 128 - 2)
        nclusters = fat_sectors * 128 - 2;
    mounted = 1;
    next_hint = 2;
    return 0;
}

int fat_info(uint32_t *cluster_bytes, uint32_t *clusters, uint32_t *free_clusters)
{
    if (!mounted)
        return FAT_ENOFS;
    *cluster_bytes = spc * 512;
    *clusters = nclusters;
    uint32_t n = 0, v;
    for (uint32_t c = 2; c < nclusters + 2; c++) {
        if (fat_get(c, &v))
            return FAT_EIO;
        n += v == 0;
    }
    *free_clusters = n;
    return 0;
}

// ---------------- directories ----------------

// Walk a directory's entries. cb gets the parsed entry and where its 8.3
// record is (cluster + byte offset); it returns 1 to stop.
struct where { uint32_t cluster, offset, lfn_cluster, lfn_offset; int lfn_count; };

static void short_to_str(const uint8_t *d, char *out)
{
    int n = 0;
    for (int i = 0; i < 8 && d[i] != ' '; i++)
        out[n++] = (d[12] & 0x08) && d[i] >= 'A' && d[i] <= 'Z' ? d[i] + 32 : d[i];
    if (d[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && d[i] != ' '; i++)
            out[n++] = (d[12] & 0x10) && d[i] >= 'A' && d[i] <= 'Z' ? d[i] + 32 : d[i];
    }
    out[n] = 0;
    if ((uint8_t)out[0] == 0x05)
        out[0] = (char)0xe5;
}

typedef int (*walk_fn)(const struct fat_entry *e, const struct where *w, void *arg);

static int walk(uint32_t dir, walk_fn fn, void *arg)
{
    static struct fat_entry e;
    struct where w = {0};
    char lfn[256];
    int lfn_ok = 0;
    uint32_t c = dir;
    while (c >= 2 && c < EOC) {
        if (fat_blk_read(cl_lba(c), spc, clbuf))
            return FAT_EIO;
        for (uint32_t off = 0; off < spc * 512; off += 32) {
            const uint8_t *d = clbuf + off;
            if (d[0] == 0)
                return 0;                       // end of directory
            if (d[0] == 0xe5) {
                lfn_ok = 0, w.lfn_count = 0;
                continue;
            }
            if (d[11] == ATTR_LFN) {
                int seq = d[0] & 0x1f;
                if (d[0] & 0x40) {              // last (first stored) part
                    memset(lfn, 0, sizeof lfn);
                    lfn_ok = 1;
                    w.lfn_cluster = c, w.lfn_offset = off, w.lfn_count = 0;
                }
                w.lfn_count++;
                static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                for (int i = 0; i < 13 && seq >= 1 && seq <= 19; i++) {
                    uint16_t ch = rd16(d + pos[i]);
                    int at = (seq - 1) * 13 + i;
                    if (at < 255 && ch != 0xffff && ch != 0)
                        lfn[at] = ch < 0x80 ? ch : '?';
                }
                continue;
            }
            if (d[11] & ATTR_VOLID) {
                lfn_ok = 0, w.lfn_count = 0;
                continue;
            }
            short_to_str(d, e.short_name);
            strcpy(e.name, lfn_ok && lfn[0] ? lfn : e.short_name);
            e.attr = d[11];
            e.size = rd32(d + 28);
            e.cluster = (uint32_t)rd16(d + 20) << 16 | rd16(d + 26);
            e.time = rd16(d + 22), e.date = rd16(d + 24);
            w.cluster = c, w.offset = off;
            if (!lfn_ok)
                w.lfn_count = 0;
            int r = fn(&e, &w, arg);
            if (r)
                return r;
            lfn_ok = 0, w.lfn_count = 0;
        }
        if (fat_get(c, &c))
            return FAT_EIO;
    }
    return 0;
}

static int name_eq(const char *a, const char *b, int n)
{
    int i = 0;
    for (; i < n && a[i]; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y)
            return 0;
    }
    return i == n && a[i] == 0;
}

struct find { const char *name; int len; struct fat_entry e; struct where w; int found; };

static int find_cb(const struct fat_entry *e, const struct where *w, void *arg)
{
    struct find *f = arg;
    if (name_eq(e->name, f->name, f->len) || name_eq(e->short_name, f->name, f->len)) {
        f->e = *e, f->w = *w, f->found = 1;
        return 1;
    }
    return 0;
}

// Resolve all but the last component of path to a directory cluster;
// *last points at the last component (may be empty).
static int resolve_parent(const char *path, uint32_t *dir, const char **last)
{
    if (!mounted)
        return FAT_ENOFS;
    uint32_t c = root_cluster;
    while (*path == '/')
        path++;
    for (;;) {
        const char *slash = strchr(path, '/');
        if (!slash) {
            *dir = c, *last = path;
            return 0;
        }
        struct find f = { path, (int)(slash - path) };
        if (f.len) {
            int r = walk(c, find_cb, &f);
            if (r < 0)
                return r;
            if (!f.found)
                return FAT_ENOENT;
            if (!(f.e.attr & ATTR_DIR))
                return FAT_ENOTDIR;
            c = f.e.cluster ? f.e.cluster : root_cluster;       // ".." of a top directory = 0
        }
        path = slash + 1;
    }
}

static int lookup(const char *path, struct find *f, uint32_t *parent)
{
    const char *last;
    memset(f, 0, sizeof *f);
    int r = resolve_parent(path, parent, &last);
    if (r)
        return r;
    f->name = last, f->len = strlen(last);
    if (f->len && last[f->len - 1] == '/')
        f->len--;
    if (f->len == 0) {                          // the directory itself
        f->found = 1;
        f->e.attr = ATTR_DIR;
        f->e.cluster = *parent;
        return 0;
    }
    r = walk(*parent, find_cb, f);
    if (r < 0)
        return r;
    return f->found ? 0 : FAT_ENOENT;
}

struct list { int (*cb)(const struct fat_entry *, void *); void *arg; };

static int list_cb(const struct fat_entry *e, const struct where *w, void *arg)
{
    (void)w;
    struct list *l = arg;
    return l->cb(e, l->arg);
}

int fat_list(const char *dir, int (*cb)(const struct fat_entry *e, void *arg), void *arg)
{
    struct find f;
    uint32_t parent;
    int r = lookup(dir, &f, &parent);
    if (r)
        return r;
    if (!(f.e.attr & ATTR_DIR))
        return FAT_ENOTDIR;
    struct list l = { cb, arg };
    r = walk(f.e.cluster ? f.e.cluster : root_cluster, list_cb, &l);
    return r < 0 ? r : 0;
}

int fat_stat(const char *path, struct fat_entry *e)
{
    struct find f;
    uint32_t parent;
    int r = lookup(path, &f, &parent);
    if (!r)
        *e = f.e;
    return r;
}

// ---------------- reading ----------------

int fat_read(const char *path, void *buf, uint32_t max, uint32_t *size)
{
    struct find f;
    uint32_t parent;
    int r = lookup(path, &f, &parent);
    if (r)
        return r;
    if (f.e.attr & ATTR_DIR)
        return FAT_EISDIR;
    if (f.e.size > max)
        return FAT_ETOOBIG;
    uint32_t left = f.e.size, cbytes = spc * 512, c = f.e.cluster;
    uint8_t *p = buf;
    while (left) {
        if (c < 2 || c >= EOC)
            return FAT_EIO;                     // chain shorter than the size
        // Read a run of consecutive clusters in one go
        uint32_t run = 1, next, cc = c;
        for (;;) {
            if (fat_get(cc, &next))
                return FAT_EIO;
            if (next != cc + 1 || run * cbytes >= left)
                break;
            run++, cc = next;
        }
        uint32_t bytes = run * cbytes;
        if (bytes <= left) {
            if (fat_blk_read(cl_lba(c), run * spc, p))
                return FAT_EIO;
        } else {                                // the tail: whole sectors into p, the rest via clbuf
            uint32_t full = left / 512;
            if (full && fat_blk_read(cl_lba(c), full, p))
                return FAT_EIO;
            if (left % 512) {
                if (fat_blk_read(cl_lba(c) + full, 1, secbuf))
                    return FAT_EIO;
                memcpy(p + full * 512, secbuf, left % 512);
            }
            bytes = left;
        }
        p += bytes, left -= bytes;
        c = next;
    }
    *size = f.e.size;
    return 0;
}

// ---------------- writing ----------------

static int to_83(const char *name, int len, uint8_t out[11], uint8_t *case_flags)
{
    static const char bad[] = "\"*+,/:;<=>?[\\]| ";
    memset(out, ' ', 11);
    int dot = -1, lower = 0, upper = 0, elower = 0, eupper = 0;
    for (int i = 0; i < len; i++)
        if (name[i] == '.')
            dot = i;
    int blen = dot < 0 ? len : dot, elen = dot < 0 ? 0 : len - dot - 1;
    if (blen < 1 || blen > 8 || elen > 3 || (dot >= 0 && elen == 0))
        return FAT_ENAME;
    for (int i = 0; i < len; i++) {
        char ch = name[i];
        if (i == dot)
            continue;
        if ((uint8_t)ch < 0x21 || (uint8_t)ch > 0x7e || strchr(bad, ch) || ch == '.')
            return FAT_ENAME;
        int ext = dot >= 0 && i > dot;
        if (ch >= 'a' && ch <= 'z') {
            ext ? elower++ : lower++;
            ch -= 32;
        } else if (ch >= 'A' && ch <= 'Z') {
            ext ? eupper++ : upper++;
        }
        out[ext ? 8 + (i - dot - 1) : i] = ch;
    }
    if ((lower && upper) || (elower && eupper))
        return FAT_ENAME;                       // mixed case needs a long name
    *case_flags = (lower ? 0x08 : 0) | (elower ? 0x10 : 0);
    if (out[0] == 0xe5)
        out[0] = 0x05;
    return 0;
}

// Read-modify-write one 32-byte directory record.
static int dirent_update(uint32_t cluster, uint32_t offset, const uint8_t *rec)
{
    uint32_t lba = cl_lba(cluster) + offset / 512;
    if (fat_blk_read(lba, 1, secbuf))
        return FAT_EIO;
    memcpy(secbuf + offset % 512, rec, 32);
    return fat_blk_write(lba, 1, secbuf) ? FAT_EIO : 0;
}

static int zero_cluster(uint32_t c)
{
    memset(clbuf, 0, spc * 512);
    return fat_blk_write(cl_lba(c), spc, clbuf) ? FAT_EIO : 0;
}

struct slot { uint32_t cluster, offset, last; int found; };

static int find_free_slot(uint32_t dir, struct slot *s)
{
    uint32_t c = dir, next;
    s->found = 0;
    while (c >= 2 && c < EOC) {
        if (fat_blk_read(cl_lba(c), spc, clbuf))
            return FAT_EIO;
        for (uint32_t off = 0; off < spc * 512; off += 32)
            if (clbuf[off] == 0 || clbuf[off] == 0xe5) {
                s->cluster = c, s->offset = off, s->found = 1;
                return 0;
            }
        s->last = c;
        if (fat_get(c, &next))
            return FAT_EIO;
        c = next;
    }
    // Directory full: add a cluster
    uint32_t nc;
    int r = alloc_cluster(s->last, &nc);
    if (r)
        return r;
    if (zero_cluster(nc))
        return FAT_EIO;
    s->cluster = nc, s->offset = 0, s->found = 1;
    return 0;
}

static void make_rec(uint8_t *rec, const uint8_t name[11], uint8_t attr, uint8_t case_flags,
                     uint32_t cluster, uint32_t size)
{
    memset(rec, 0, 32);
    memcpy(rec, name, 11);
    rec[11] = attr;
    rec[12] = case_flags;
    wr16(rec + 16, FIXED_DATE);                 // created
    wr16(rec + 18, FIXED_DATE);                 // accessed
    wr16(rec + 20, cluster >> 16);
    wr16(rec + 24, FIXED_DATE);                 // written
    wr16(rec + 26, cluster & 0xffff);
    wr32(rec + 28, size);
}

// Mark an entry (and its long-name records) deleted.
static int delete_records(const struct where *w)
{
    uint8_t rec[32];
    if (w->lfn_count) {                         // long-name records sit just before (maybe across a cluster)
        uint32_t c = w->lfn_cluster, off = w->lfn_offset;
        for (int i = 0; i < w->lfn_count; i++) {
            uint32_t lba = cl_lba(c) + off / 512;
            if (fat_blk_read(lba, 1, secbuf))
                return FAT_EIO;
            secbuf[off % 512] = 0xe5;
            if (fat_blk_write(lba, 1, secbuf))
                return FAT_EIO;
            off += 32;
            if (off == spc * 512 && fat_get(c, &c))
                return FAT_EIO;
            if (off == spc * 512)
                off = 0;
        }
    }
    uint32_t lba = cl_lba(w->cluster) + w->offset / 512;
    if (fat_blk_read(lba, 1, secbuf))
        return FAT_EIO;
    memcpy(rec, secbuf + w->offset % 512, 32);
    rec[0] = 0xe5;
    return dirent_update(w->cluster, w->offset, rec);
}

static int write_data(const uint8_t *p, uint32_t size, uint32_t *first)
{
    uint32_t cbytes = spc * 512, prev = 0, c;
    *first = 0;
    while (size) {
        int r = alloc_cluster(prev, &c);
        if (r)
            return r;
        if (!*first)
            *first = c;
        uint32_t n = size < cbytes ? size : cbytes;
        if (n == cbytes) {
            if (fat_blk_write(cl_lba(c), spc, p))
                return FAT_EIO;
        } else {                                // last, partial cluster: pad with zeros
            memset(clbuf, 0, cbytes);
            memcpy(clbuf, p, n);
            if (fat_blk_write(cl_lba(c), (n + 511) / 512, clbuf))
                return FAT_EIO;
        }
        p += n, size -= n, prev = c;
    }
    return 0;
}

int fat_write(const char *path, const void *buf, uint32_t size)
{
    struct find f;
    uint32_t parent;
    int r = lookup(path, &f, &parent);
    if (r && r != FAT_ENOENT)
        return r;
    if (r == FAT_ENOENT && f.len == 0)
        return r;
    if (!r && (f.e.attr & ATTR_DIR))
        return FAT_EISDIR;
    uint8_t name[11], cf = 0, rec[32];
    struct slot s;
    if (fsinfo_unknown())
        return FAT_EIO;
    if (!r) {                                   // replace: keep the record (and its name), free the data
        if (free_chain(f.e.cluster))
            return FAT_EIO;
        s.cluster = f.w.cluster, s.offset = f.w.offset;
        if (fat_blk_read(cl_lba(s.cluster) + s.offset / 512, 1, secbuf))
            return FAT_EIO;
        memcpy(name, secbuf + s.offset % 512, 11);
        cf = secbuf[s.offset % 512 + 12];
    } else {
        if ((r = to_83(f.name, f.len, name, &cf)))
            return r;
        if ((r = find_free_slot(parent, &s)))
            return r;
    }
    uint32_t first;
    if ((r = write_data(buf, size, &first))) {
        fat_flush();
        return r;
    }
    make_rec(rec, name, 0x20, cf, first, size);  // 0x20 = archive
    if (fat_flush() || dirent_update(s.cluster, s.offset, rec))
        return FAT_EIO;
    return 0;
}

static int empty_cb(const struct fat_entry *e, const struct where *w, void *arg)
{
    (void)w, (void)arg;
    return strcmp(e->short_name, ".") && strcmp(e->short_name, "..") ? 1 : 0;
}

int fat_remove(const char *path)
{
    struct find f;
    uint32_t parent;
    int r = lookup(path, &f, &parent);
    if (r)
        return r;
    if (f.len == 0)
        return FAT_EISDIR;                      // the root
    if (f.e.attr & ATTR_DIR) {
        r = walk(f.e.cluster, empty_cb, NULL);
        if (r < 0)
            return r;
        if (r)
            return FAT_ENOTEMPTY;
    }
    if (fsinfo_unknown() || free_chain(f.e.cluster) || fat_flush() || delete_records(&f.w))
        return FAT_EIO;
    return 0;
}

int fat_mkdir(const char *path)
{
    struct find f;
    uint32_t parent;
    int r = lookup(path, &f, &parent);
    if (!r)
        return FAT_EEXIST;
    if (r != FAT_ENOENT || f.len == 0)
        return r;
    uint8_t name[11], cf, rec[32];
    if ((r = to_83(f.name, f.len, name, &cf)))
        return r;
    struct slot s;
    if (fsinfo_unknown() || (r = find_free_slot(parent, &s)))
        return r ? r : FAT_EIO;
    uint32_t c;
    if ((r = alloc_cluster(0, &c)))
        return r;
    memset(clbuf, 0, spc * 512);
    static const uint8_t dot[11] = ".          ", dotdot[11] = "..         ";
    make_rec(clbuf, dot, ATTR_DIR, 0, c, 0);
    make_rec(clbuf + 32, dotdot, ATTR_DIR, 0, parent == root_cluster ? 0 : parent, 0);
    if (fat_blk_write(cl_lba(c), spc, clbuf))
        return FAT_EIO;
    make_rec(rec, name, ATTR_DIR, cf, c, 0);
    if (fat_flush() || dirent_update(s.cluster, s.offset, rec))
        return FAT_EIO;
    return 0;
}

const char *fat_strerror(int r)
{
    switch (r) {
    case FAT_EIO:       return "eMMC read/write failed";
    case FAT_ENOFS:     return "no FAT32 file system";
    case FAT_ENOENT:    return "no such file or directory";
    case FAT_ENOTDIR:   return "not a directory";
    case FAT_EISDIR:    return "is a directory";
    case FAT_EEXIST:    return "already exists";
    case FAT_ENAME:     return "new names must be 8.3 (up to 8 + 3 characters, one case)";
    case FAT_ENOSPC:    return "no space left";
    case FAT_ETOOBIG:   return "file too large for the buffer";
    case FAT_ENOTEMPTY: return "directory not empty";
    }
    return "error";
}
