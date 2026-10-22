#ifndef CORE_H
#define CORE_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHAR    256
#define BUFFER_SIZE 8192
#define MAGIC       "ZYX"

enum { MODE_SINGLE = 1, MODE_ZIP = 2, MODE_TAR = 3 };

extern volatile int g_cancel;
extern volatile unsigned long g_progress_processed;
extern volatile unsigned long g_progress_total;

/* 密码相关 */
extern char g_password[256];   /* 全局密码，空字符串表示不加密 */

typedef struct _huff_node {
    unsigned char ch;
    unsigned long freq;
    struct _huff_node *left, *right;
} HuffNode;

typedef struct {
    HuffNode **data;
    int size, capacity;
} MinHeap;

typedef struct {
    unsigned char code[MAX_CHAR];
    int len;
} CodeItem;

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint8_t  version;
    uint8_t  mode;
    uint32_t file_count;
    uint64_t raw_size;
    uint64_t password_hash;   /* 密码校验值，0=无密码 */
} FileHeader;

typedef struct __attribute__((packed)) {
    uint16_t name_len;
    char     name[256];
    uint64_t raw_size;
    uint64_t comp_offset;
    uint64_t comp_size;
} FileEntry;

void xor_data(unsigned char *data, size_t len);

int compress_single(const char *src, const char *dst);
int compress_zip(const char *files[], int count, const char *dst);
int compress_tar(const char *files[], int count, const char *dst);
int decompress_file(const char *src, const char *outdir);

#endif
