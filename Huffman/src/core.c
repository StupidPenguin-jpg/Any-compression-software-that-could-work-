#include "core.h"
#include <sys/stat.h>
#include <zip.h>
#include <archive.h>
#include <archive_entry.h>

volatile int g_cancel = 0;
volatile unsigned long g_progress_processed = 0;
volatile unsigned long g_progress_total = 0;

/* =============== Huffman 核心算法 (保留用于 TAR 模式) =============== */
static MinHeap *heap_create(int cap)
{
    MinHeap *h = malloc(sizeof(MinHeap));
    if (!h) return NULL;
    h->data = malloc(sizeof(HuffNode *) * cap);
    if (!h->data) { free(h); return NULL; }
    h->size = 0; h->capacity = cap;
    return h;
}
static void heap_free(MinHeap *h) { if (h) { free(h->data); free(h); } }
static void heap_swap(MinHeap *h, int i, int j)
{ HuffNode *t = h->data[i]; h->data[i] = h->data[j]; h->data[j] = t; }
static void heap_sift(MinHeap *h, int idx)
{
    int s = idx, l = 2*idx+1, r = 2*idx+2;
    if (l < h->size && h->data[l]->freq < h->data[s]->freq) s = l;
    if (r < h->size && h->data[r]->freq < h->data[s]->freq) s = r;
    if (s != idx) { heap_swap(h, idx, s); heap_sift(h, s); }
}
static void heap_push(MinHeap *h, HuffNode *n)
{
    int i = h->size++;
    h->data[i] = n;
    while (i > 0) {
        int p = (i-1)/2;
        if (h->data[p]->freq <= h->data[i]->freq) break;
        heap_swap(h, i, p); i = p;
    }
}
static HuffNode *heap_pop(MinHeap *h)
{
    if (!h->size) return NULL;
    HuffNode *n = h->data[0];
    h->data[0] = h->data[--h->size];
    heap_sift(h, 0);
    return n;
}
static HuffNode *build_tree(unsigned long *freq)
{
    MinHeap *h = heap_create(MAX_CHAR);
    if (!h) return NULL;
    for (int i = 0; i < MAX_CHAR; i++) if (freq[i]) {
        HuffNode *n = malloc(sizeof(HuffNode));
        n->ch = i; n->freq = freq[i]; n->left = n->right = NULL;
        heap_push(h, n);
    }
    if (h->size == 0) { heap_free(h); return NULL; }
    if (h->size == 1) {
        HuffNode *leaf = heap_pop(h);
        HuffNode *r = malloc(sizeof(HuffNode));
        r->ch = leaf->ch; r->freq = leaf->freq;
        r->left = leaf; r->right = NULL;
        heap_free(h); return r;
    }
    while (h->size > 1) {
        if (g_cancel) { heap_free(h); return NULL; }
        HuffNode *l = heap_pop(h), *r = heap_pop(h);
        HuffNode *p = malloc(sizeof(HuffNode));
        p->ch = 0; p->freq = l->freq + r->freq;
        p->left = l; p->right = r;
        heap_push(h, p);
    }
    HuffNode *r = heap_pop(h); heap_free(h); return r;
}
static void free_tree(HuffNode *r)
{ if (!r) return; free_tree(r->left); free_tree(r->right); free(r); }
static void build_codes(HuffNode *r, CodeItem *t, unsigned char *b, int d)
{
    if (!r) return;
    if (!r->left && !r->right) {
        t[r->ch].len = d;
        memcpy(t[r->ch].code, b, d);
        return;
    }
    if (r->left) { b[d] = 0; build_codes(r->left, t, b, d+1); }
    if (r->right) { b[d] = 1; build_codes(r->right, t, b, d+1); }
}
static int huff_encode(unsigned char *in, size_t in_len,
                       unsigned long *freq, unsigned char *out, size_t *out_len)
{
    if (g_cancel) return -1;
    HuffNode *root = build_tree(freq);
    if (!root) return -1;
    if (g_cancel) { free_tree(root); return -1; }
    CodeItem ct[MAX_CHAR] = {{0}};
    unsigned char tmp[MAX_CHAR];
    build_codes(root, ct, tmp, 0);
    size_t wp = 0;
    unsigned char buf = 0; int bp = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (g_cancel) { free_tree(root); return -1; }
        CodeItem *ci = &ct[in[i]];
        for (int j = 0; j < ci->len; j++) {
            buf = (buf << 1) | ci->code[j];
            if (++bp == 8) { out[wp++] = buf; buf = 0; bp = 0; }
        }
    }
    if (bp) { buf <<= (8 - bp); out[wp++] = buf; }
    out[wp++] = (unsigned char)bp;
    *out_len = wp;
    free_tree(root);
    return 0;
}
static int huff_decode(unsigned char *in, size_t in_len,
                       unsigned long *freq, unsigned char *out, size_t *out_len,
                       size_t expected)
{
    if (g_cancel) return -1;
    if (in_len == 0) { *out_len = 0; return 0; }
    if (in_len == 1) { *out_len = 0; return 0; }
    HuffNode *root = build_tree(freq);
    if (!root) return -1;
    if (g_cancel) { free_tree(root); return -1; }
    int padding = in[in_len - 1];
    size_t data_bytes = in_len - 1;
    size_t op = 0;
    HuffNode *cur = root;
    if (root->left && !root->right) {
        for (size_t i = 0; i < expected; i++) {
            if (g_cancel) { free_tree(root); return -1; }
            out[op++] = root->left->ch;
        }
    } else {
        unsigned char buf = 0; int bp = 0;
        size_t ip = 0;
        while (op < expected && ip < data_bytes) {
            if (g_cancel) { free_tree(root); return -1; }
            if (bp == 0) { buf = in[ip++]; bp = 8; }
            if (ip == data_bytes && bp > 0) {
                int bits_here = bp - padding;
                for (int i = 0; i < bits_here && op < expected; i++) {
                    int bit = (buf >> 7) & 1; buf <<= 1; bp--;
                    cur = bit ? cur->right : cur->left;
                    if (!cur) break;
                    if (!cur->left && !cur->right) { out[op++] = cur->ch; cur = root; }
                }
                break;
            }
            int bit = (buf >> 7) & 1; buf <<= 1; bp--;
            cur = bit ? cur->right : cur->left;
            if (!cur) break;
            if (!cur->left && !cur->right) { out[op++] = cur->ch; cur = root; }
        }
    }
    *out_len = op;
    free_tree(root);
    return 0;
}

/* =============== Huffman 自定义 TAR (内部模式) =============== */
int compress_tar(const char *files[], int count, const char *dst)
{
    if (g_cancel) return -1;
    int use_gz = 0;
    size_t _dlen = strlen(dst);
    if (_dlen > 3 && strcmp(dst + _dlen - 3, ".gz") == 0) use_gz = 1;

    struct archive *a = archive_write_new();
    if (!a) return -1;
    if (use_gz)
        archive_write_add_filter_gzip(a);
    else
        archive_write_add_filter_none(a);
    archive_write_set_format_pax(a);
    if (archive_write_open_filename(a, dst) != ARCHIVE_OK) {
        archive_write_free(a);
        return -1;
    }

    g_progress_processed = 0;
    g_progress_total = 0;
    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(files[i], &st) == 0) g_progress_total += st.st_size;
    }

    char buf[65536];
    for (int i = 0; i < count; i++) {
        if (g_cancel) { archive_write_close(a); archive_write_free(a); return -1; }
        struct stat st;
        if (stat(files[i], &st) < 0) continue;

        struct archive_entry *entry = archive_entry_new();
        const char *name = strrchr(files[i], '/');
        name = name ? name + 1 : files[i];
        archive_entry_set_pathname(entry, name);
        archive_entry_set_size(entry, st.st_size);
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);

        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            archive_entry_free(entry);
            continue;
        }

        FILE *f = fopen(files[i], "rb");
        if (f) {
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                if (g_cancel) { fclose(f); archive_entry_free(entry); archive_write_close(a); archive_write_free(a); return -1; }
                archive_write_data(a, buf, n);
            }
            fclose(f);
        }
        archive_entry_free(entry);
        g_progress_processed += st.st_size;
        if (g_progress_processed > g_progress_total) g_progress_processed = g_progress_total;
    }

    archive_write_close(a);
    archive_write_free(a);
    return 0;
}

/* =============== 标准 ZIP 压缩 =============== */
int compress_zip(const char *files[], int count, const char *dst)
{
    if (g_cancel) return -1;
    int err;
    zip_t *z = zip_open(dst, ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!z) return -1;

    g_progress_processed = 0;
    g_progress_total = 0;
    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(files[i], &st) == 0) g_progress_total += st.st_size;
    }

    for (int i = 0; i < count; i++) {
        if (g_cancel) { zip_close(z); return -1; }
        FILE *f = fopen(files[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
        char *data = malloc(sz);
        if (!data) { fclose(f); continue; }
        fread(data, 1, sz, f);
        fclose(f);

        const char *name = strrchr(files[i], '/');
        name = name ? name + 1 : files[i];

        zip_source_t *src = zip_source_buffer_create(data, sz, 1, NULL);
        if (!src) { free(data); continue; }
        if (zip_file_add(z, name, src, ZIP_FL_OVERWRITE) < 0) {
            zip_source_free(src);
            free(data);
            continue;
        }
        g_progress_processed += sz;
        if (g_progress_processed > g_progress_total) g_progress_processed = g_progress_total;
    }
    zip_close(z);
    return 0;
}

/* =============== 标准 ZIP 解压 =============== */
static int extract_zip(const char *src, const char *outdir)
{
    int err;
    zip_t *z = zip_open(src, ZIP_RDONLY, &err);
    if (!z) return -1;

    zip_int64_t num = zip_get_num_entries(z, 0);
    if (num <= 0) { zip_close(z); return -1; }

    g_progress_total = 0;
    g_progress_processed = 0;
    for (zip_int64_t i = 0; i < num; i++) {
        struct zip_stat st;
        if (zip_stat_index(z, i, 0, &st) == 0) g_progress_total += st.size;
    }
    mkdir(outdir, 0755);

    for (zip_int64_t i = 0; i < num; i++) {
        if (g_cancel) { zip_close(z); return -1; }
        struct zip_stat st;
        if (zip_stat_index(z, i, 0, &st) < 0) continue;

        zip_file_t *zf = zip_fopen_index(z, i, 0);
        if (!zf) continue;

        char *data = malloc(st.size + 1);
        if (!data) { zip_fclose(zf); continue; }
        zip_int64_t n = zip_fread(zf, data, st.size);
        if (n <= 0) { free(data); zip_fclose(zf); continue; }
        data[n] = 0;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", outdir, st.name);
        char *p = strrchr(fullpath, '/');
        if (p) {
            *p = 0;
            char tmp[1024];
            snprintf(tmp, sizeof(tmp), "mkdir -p \"%s\" 2>/dev/null", fullpath);
            system(tmp);
            *p = '/';
        }
        FILE *f = fopen(fullpath, "wb");
        if (f) { fwrite(data, 1, n, f); fclose(f); }
        free(data);
        zip_fclose(zf);
        g_progress_processed += st.size;
    }
    zip_close(z);
    return 0;
}

/* =============== TAR/TAR.GZ 解压 (libarchive) =============== */
static int extract_tar(const char *src, const char *outdir)
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, src, 65536) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    g_progress_total = 0;
    g_progress_processed = 0;
    /* 扫描总大小 */
    {
        struct archive *a2 = archive_read_new();
        archive_read_support_filter_all(a2);
        archive_read_support_format_all(a2);
        if (archive_read_open_filename(a2, src, 65536) == ARCHIVE_OK) {
            struct archive_entry *e;
            while (archive_read_next_header(a2, &e) == ARCHIVE_OK)
                g_progress_total += archive_entry_size(e);
            archive_read_close(a2);
        }
        archive_read_free(a2);
    }
    if (g_progress_total == 0) g_progress_total = 1;

    mkdir(outdir, 0755);
    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (g_cancel) { archive_read_close(a); archive_read_free(a); return -1; }
        const char *name = archive_entry_pathname(entry);
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", outdir, name);

        char *p = strrchr(fullpath, '/');
        if (p) {
            *p = 0;
            char tmp[1024];
            snprintf(tmp, sizeof(tmp), "mkdir -p \"%s\" 2>/dev/null", fullpath);
            system(tmp);
            *p = '/';
        }
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            mkdir(fullpath, 0755);
        } else {
            FILE *f = fopen(fullpath, "wb");
            if (f) {
                char buf[65536];
                la_ssize_t n;
                while ((n = archive_read_data(a, buf, sizeof(buf))) > 0) {
                    if (g_cancel) { fclose(f); archive_read_close(a); archive_read_free(a); return -1; }
                    fwrite(buf, 1, n, f);
                }
                fclose(f);
            }
        }
        g_progress_processed += archive_entry_size(entry);
    }
    archive_read_close(a);
    archive_read_free(a);
    return 0;
}

/* =============== 统一解压入口 =============== */
int decompress_file(const char *src, const char *outdir)
{
    size_t l = strlen(src);
    if (l >= 4 && strcmp(src + l - 4, ".zip") == 0)
        return extract_zip(src, outdir);
    if (l >= 4 && strcmp(src + l - 4, ".tar") == 0)
        return extract_tar(src, outdir);
    if (l >= 3 && strcmp(src + l - 3, ".gz") == 0) {
        /* .tar.gz 或 .gz */
        if (l >= 7 && strcmp(src + l - 7, ".tar.gz") == 0)
            return extract_tar(src, outdir);
        /* 纯 .gz 文件: 用 libarchive 自动识别 */
        return extract_tar(src, outdir);
    }
    if (l >= 4 && strcmp(src + l - 4, ".tgz") == 0)
        return extract_tar(src, outdir);
    return -1;
}
