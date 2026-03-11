#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/stat.h>

#define UNUSED

#define FATAL_ERROR(format, ...)            \
do {                                        \
    fprintf(stderr, format, ##__VA_ARGS__); \
    exit(1);                                \
} while (0)

#define PADDINGSIZE(length, padTo) (padTo - length) % padTo

struct DataFile {
    struct DataFile *next;
    unsigned char *data;
    uint32_t size;
};

struct DataPackage {
    struct DataFile *head;
    struct DataFile *tail;
    uint32_t count;
    uint32_t size;
};

struct StrVec {
    char **s;
    size_t count;
    size_t capacity;
};

char *GetFileExtension(char *path);
unsigned char *ReadWholeFile(char *path, int *size);
__uint8_t ReadU8(const unsigned char *ptr, const size_t offset);
__uint16_t ReadU16_BE(const unsigned char *ptr, const size_t offset);
__uint16_t ReadU16_LE(const unsigned char *ptr, const size_t offset);
__uint32_t ReadU24_BE(const unsigned char *ptr, const size_t offset);
__uint32_t ReadU24_LE(const unsigned char *ptr, const size_t offset);
__uint32_t ReadU32_BE(const unsigned char *ptr, const size_t offset);
__uint32_t ReadU32_LE(const unsigned char *ptr, const size_t offset);
__uint32_t ReadVariableLength(const unsigned char *ptr, size_t *offset);
void WriteU8(unsigned char *ptr, const size_t offset, __uint8_t value);
void WriteU16(unsigned char *ptr, const size_t offset, __uint16_t value);
void WriteU24(unsigned char *ptr, const size_t offset, __uint32_t value);
void WriteU32(unsigned char *ptr, const size_t offset, __uint32_t value);
__uint8_t WriteVariableLength(unsigned char *ptr, size_t offset, __uint32_t value);
__uint8_t VariableLength(__uint32_t value);
char *JoinPaths(char *parent, char *child);
struct StrVec *StrVec_New(size_t capacity);
int strcmp_q(const void *s1, const void *s2);
int U32cmp_q(const void *i1, const void *i2);

#endif //UTIL_H
