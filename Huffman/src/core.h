#ifndef CORE_H
#define CORE_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHAR    256
#define BUFFER_SIZE 8192

/* 全局取消标志和进度 */
extern volatile int g_cancel;
extern volatile unsigned long g_progress_processed;
extern volatile unsigned long g_progress_total;

/* 压缩模式 */
enum { MODE_ZIP = 2, MODE_TAR = 3 };

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

int compress_zip(const char *files[], int count, const char *dst);
int compress_tar(const char *files[], int count, const char *dst);
int decompress_file(const char *src, const char *outdir);

#endif
