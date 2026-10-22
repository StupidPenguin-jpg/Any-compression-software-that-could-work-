#include "core.h"
#include <sys/stat.h>

volatile int g_cancel = 0;
volatile unsigned long g_progress_processed = 0;
volatile unsigned long g_progress_total = 0;
char g_password[256] = "";  /* 密码，空=不加密 */

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

/* XOR 加密/解密：用密码循环异或数据 */
void xor_data(unsigned char *data, size_t len)
{
    size_t pwlen = strlen(g_password);
    if (pwlen == 0) return;
    for (size_t i = 0; i < len; i++)
        data[i] ^= g_password[i % pwlen];
}

/* 简易密码哈希 */
static uint64_t calc_pw_hash(void)
{
    uint64_t h = 0;
    size_t l = strlen(g_password);
    if (l == 0) return 0;
    for (size_t i = 0; i < l; i++) {
        h = (h << 5) - h + (unsigned char)g_password[i];
    }
    return h;
}

int compress_single(const char *src, const char *dst)
{
    if (g_cancel) return -1;
    FILE *fin = fopen(src, "rb");
    if (!fin) return -1;
    fseek(fin, 0, SEEK_END); size_t sz = ftell(fin); rewind(fin);
    unsigned char *data = malloc(sz);
    if (!data) { fclose(fin); return -1; }
    fread(data, 1, sz, fin); fclose(fin);
    unsigned long freq[MAX_CHAR] = {0};
    for (size_t i = 0; i < sz; i++) freq[data[i]]++;
    if (g_cancel) { free(data); return -1; }
    unsigned char *comp = malloc(sz * 2 + 4096);
    if (!comp) { free(data); return -1; }
    size_t comp_len = 0;
    if (huff_encode(data, sz, freq, comp, &comp_len) < 0) { free(data); free(comp); return -1; }
    FILE *fout = fopen(dst, "wb");
    if (!fout) { free(data); free(comp); return -1; }
    FileHeader hdr;
    memcpy(hdr.magic, MAGIC, 4); hdr.version = 2; hdr.mode = MODE_SINGLE;
    hdr.file_count = 1; hdr.raw_size = sz; hdr.password_hash = calc_pw_hash();
    fwrite(&hdr, sizeof(hdr), 1, fout);
    const char *fn = strrchr(src, '/'); fn = fn ? fn + 1 : src;
    FileEntry fe; memset(&fe, 0, sizeof(fe));
    fe.name_len = strlen(fn); memcpy(fe.name, fn, fe.name_len);
    fe.raw_size = sz; fe.comp_offset = 0; fe.comp_size = comp_len;
    fwrite(&fe, sizeof(fe), 1, fout);
    fwrite(freq, sizeof(freq), 1, fout);
    xor_data(comp, comp_len);
    xor_data(comp, comp_len);
    fwrite(comp, 1, comp_len, fout);
    xor_data(comp, comp_len);
    xor_data(comp, comp_len); /* 还原 */
    free(data); free(comp); fclose(fout);
    return 0;
}

int compress_zip(const char *files[], int count, const char *dst)
{
    if (g_cancel) return -1;
    FILE *fout = fopen(dst, "wb");
    if (!fout) return -1;
    FileHeader hdr;
    memcpy(hdr.magic, MAGIC, 4); hdr.version = 2; hdr.mode = MODE_ZIP;
    hdr.password_hash = calc_pw_hash();
    hdr.file_count = count; hdr.raw_size = 0;
    fwrite(&hdr, sizeof(hdr), 1, fout);
    FileEntry *entries = calloc(count, sizeof(FileEntry));
    uint64_t data_off = sizeof(hdr) + count * sizeof(FileEntry);
    fwrite(entries, sizeof(FileEntry), count, fout);
    for (int i = 0; i < count; i++) {
        if (g_cancel) { fclose(fout); free(entries); return -1; }
        FILE *fin = fopen(files[i], "rb");
        if (!fin) continue;
        fseek(fin, 0, SEEK_END); size_t sz = ftell(fin); rewind(fin);
        unsigned char *data = malloc(sz); fread(data, 1, sz, fin); fclose(fin);
        if (g_cancel) { free(data); fclose(fout); free(entries); return -1; }
        unsigned long freq[MAX_CHAR] = {0};
        for (size_t j = 0; j < sz; j++) freq[data[j]]++;
        unsigned long _before = g_progress_processed;
        unsigned char *comp = malloc(sz * 2 + 4096);
        size_t comp_len = 0;
        if (huff_encode(data, sz, freq, comp, &comp_len) < 0) { free(data); free(comp); fclose(fout); free(entries); return -1; }
        free(data);
        g_progress_processed = _before + sz; if (g_progress_processed > g_progress_total) g_progress_processed = g_progress_total;
        const char *fn = strrchr(files[i], '/'); fn = fn ? fn + 1 : files[i];
        entries[i].name_len = strlen(fn); memcpy(entries[i].name, fn, entries[i].name_len);
        entries[i].raw_size = sz; entries[i].comp_offset = data_off;
        entries[i].comp_size = sizeof(freq) + comp_len;
        fwrite(freq, sizeof(freq), 1, fout);
        xor_data(comp, comp_len);
    xor_data(comp, comp_len);
    fwrite(comp, 1, comp_len, fout);
    xor_data(comp, comp_len);
    xor_data(comp, comp_len); /* 还原 */
        data_off += entries[i].comp_size; hdr.raw_size += sz;
        free(comp);
    }
    rewind(fout); fwrite(&hdr, sizeof(hdr), 1, fout);
    fwrite(entries, sizeof(FileEntry), count, fout);
    fclose(fout); free(entries);
    return 0;
}

int compress_tar(const char *files[], int count, const char *dst)
{
    if (g_cancel) return -1;
    size_t *sizes = calloc(count, sizeof(size_t));
    unsigned char **datas = calloc(count, sizeof(unsigned char *));
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        if (g_cancel) { free(sizes); free(datas); return -1; }
        FILE *fin = fopen(files[i], "rb");
        if (!fin) continue;
        fseek(fin, 0, SEEK_END); sizes[i] = ftell(fin); rewind(fin);
        datas[i] = malloc(sizes[i]); fread(datas[i], 1, sizes[i], fin); fclose(fin);
        total += sizes[i];
        g_progress_processed += sizes[i];
    }
    unsigned char *merged = malloc(total);
    size_t off = 0;
    for (int i = 0; i < count; i++) {
        if (g_cancel) { free(merged); free(sizes); free(datas); return -1; }
        if (datas[i]) { memcpy(merged + off, datas[i], sizes[i]); off += sizes[i]; }
        free(datas[i]);
    }
    free(datas);
    unsigned long freq[MAX_CHAR] = {0};
    for (size_t i = 0; i < total; i++) freq[merged[i]]++;
    unsigned char *comp = malloc(total * 2 + 4096);
    size_t comp_len = 0;
    if (huff_encode(merged, total, freq, comp, &comp_len) < 0) { free(merged); free(comp); free(sizes); return -1; }
    free(merged);
    FILE *fout = fopen(dst, "wb");
    if (!fout) { free(comp); free(sizes); return -1; }
    FileHeader hdr;
    memcpy(hdr.magic, MAGIC, 4); hdr.version = 2; hdr.mode = MODE_TAR;
    hdr.password_hash = calc_pw_hash();
    hdr.file_count = count; hdr.raw_size = total;
    fwrite(&hdr, sizeof(hdr), 1, fout);
    FileEntry *entries = calloc(count, sizeof(FileEntry));
    size_t acc = 0;
    for (int i = 0; i < count; i++) {
        const char *fn = strrchr(files[i], '/'); fn = fn ? fn + 1 : files[i];
        entries[i].name_len = strlen(fn); memcpy(entries[i].name, fn, entries[i].name_len);
        entries[i].raw_size = sizes[i]; entries[i].comp_offset = acc; entries[i].comp_size = 0;
        acc += sizes[i];
    }
    fwrite(entries, sizeof(FileEntry), count, fout);
    fwrite(freq, sizeof(freq), 1, fout);
    xor_data(comp, comp_len);
    xor_data(comp, comp_len);
    fwrite(comp, 1, comp_len, fout);
    xor_data(comp, comp_len);
    xor_data(comp, comp_len); /* 还原 */
    fclose(fout); free(entries); free(comp); free(sizes);
    /* progress updated during read */
    return 0;
}

int decompress_file(const char *src, const char *outdir)
{
    if (g_cancel) return -1;
    FILE *fin = fopen(src, "rb");
    if (!fin) return -1;
    FileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fin) != 1 || memcmp(hdr.magic, MAGIC, 4) != 0) { fclose(fin); return -1; }
    if (hdr.password_hash != 0 && hdr.password_hash != calc_pw_hash()) { fclose(fin); return -2; }
    if (g_cancel) { fclose(fin); return -1; }
    FileEntry *entries = calloc(hdr.file_count, sizeof(FileEntry));
    if (!entries) { fclose(fin); return -1; }
    fread(entries, sizeof(FileEntry), hdr.file_count, fin);
    if (hdr.mode == MODE_SINGLE || hdr.mode == MODE_TAR) {
        int is_single_file = (hdr.mode == MODE_SINGLE && hdr.file_count == 1);
        if (!is_single_file) mkdir(outdir, 0755);
        unsigned long freq[MAX_CHAR] = {0};
        fread(freq, sizeof(freq), 1, fin);
        if (g_cancel) { fclose(fin); free(entries); return -1; }
        fseek(fin, 0, SEEK_END);
        long fsz = ftell(fin);
        long hdr_end = (hdr.mode == MODE_SINGLE) ? sizeof(FileHeader) + sizeof(FileEntry) + sizeof(freq) : sizeof(FileHeader) + hdr.file_count * sizeof(FileEntry) + sizeof(freq);
        g_progress_total = hdr.raw_size;
        g_progress_processed = 0;
        long comp_bytes = fsz - hdr_end;
        fseek(fin, hdr_end, SEEK_SET);
        unsigned char *comp = malloc(comp_bytes); fread(comp, 1, comp_bytes, fin);
        xor_data(comp, comp_bytes);
        unsigned char *out = malloc(hdr.raw_size + 1);
        size_t out_len = 0;
        if (huff_decode(comp, comp_bytes, freq, out, &out_len, hdr.raw_size) < 0) { free(comp); free(out); fclose(fin); free(entries); return -1; }
        free(comp);
        if (is_single_file) {
            FILE *f = fopen(outdir, "wb");
            if (f) { fwrite(out, 1, out_len, f); fclose(f); }
        } else if (hdr.mode == MODE_SINGLE) {
            char path[1024]; snprintf(path, sizeof(path), "%s/%s", outdir, entries[0].name);
            FILE *f = fopen(path, "wb"); if (f) { fwrite(out, 1, out_len, f); fclose(f); }
        } else {
            size_t acc = 0;
            for (uint32_t i = 0; i < hdr.file_count; i++) {
                char path[1024]; snprintf(path, sizeof(path), "%s/%s", outdir, entries[i].name);
                FILE *f = fopen(path, "wb"); if (f) { fwrite(out + acc, 1, entries[i].raw_size, f); fclose(f); }
                acc += entries[i].raw_size;
            }
        }
        free(out);
        g_progress_processed = hdr.raw_size;
    } else if (hdr.mode == MODE_ZIP) {
        mkdir(outdir, 0755);
        for (uint32_t i = 0; i < hdr.file_count; i++) {
            if (g_cancel) { fclose(fin); free(entries); return -1; }
            fseek(fin, entries[i].comp_offset, SEEK_SET);
            unsigned long fq[MAX_CHAR] = {0}; fread(fq, sizeof(fq), 1, fin);
            g_progress_total = entries[i].raw_size;
            g_progress_processed = 0;
            long avail = entries[i].comp_size - sizeof(fq);
            if (avail <= 0) continue;
            unsigned char *comp = malloc(avail); fread(comp, 1, avail, fin);
            xor_data(comp, avail);
            if (g_cancel) { free(comp); fclose(fin); free(entries); return -1; }
            unsigned char *out = malloc(entries[i].raw_size + 1);
            size_t out_len = 0;
            if (huff_decode(comp, avail, fq, out, &out_len, entries[i].raw_size) < 0) { free(comp); free(out); fclose(fin); free(entries); return -1; }
            free(comp);
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", outdir, entries[i].name);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(out, 1, out_len, f); fclose(f); }
            free(out);
            g_progress_processed += entries[i].raw_size;
        }
    }
    fclose(fin); free(entries);
    return 0;
}
