#include "util.h"

char *GetFileExtension(char *path)
{
    char *extension = path;

    while (*extension != 0)
        extension++;

    while (extension > path && *extension != '.' && *extension != '/')
    {
        extension--;
    }

    if (*extension == '/')
        return path;

    if (extension == path)
        return NULL;

    extension++;

    if (*extension == 0)
        return NULL;

    return extension;
}

unsigned char *ReadWholeFile(char *path, int *size)
{
    FILE *fp = fopen(path, "rb");

    if (fp == NULL)
        FATAL_ERROR("Failed to open \"%s\" for reading.\n", path);

    fseek(fp, 0, SEEK_END);

    *size = ftell(fp);

    unsigned char *buffer = malloc(*size);

    if (buffer == NULL)
        FATAL_ERROR("Failed to allocate memory for reading \"%s\".\n", path);

    rewind(fp);

    if (fread(buffer, *size, 1, fp) != 1)
        FATAL_ERROR("Failed to read \"%s\".\n", path);

    fclose(fp);

    return buffer;
}

// TODO: change read and write functions to do either BE or LE based on an input parameter
// Then give a little endian option to each file type
__uint8_t ReadU8(const unsigned char *ptr, const size_t offset) {
    return ptr[offset];
}

__uint16_t ReadU16_BE(const unsigned char *ptr, const size_t offset) {
    return (ptr[offset + 1] << 8) | ptr[offset];
}

__uint16_t ReadU16_LE(const unsigned char *ptr, const size_t offset) {
    return (ptr[offset] << 8) | ptr[offset + 1];
}

__uint32_t ReadU24_BE(const unsigned char *ptr, const size_t offset) {
    return (ptr[offset + 2] << 16) | (ptr[offset + 1] << 8) | ptr[offset];
}

__uint32_t ReadU24_LE(const unsigned char *ptr, const size_t offset) {
    return (ptr[offset] << 16) | (ptr[offset + 1] << 8) | ptr[offset + 2];
}

__uint32_t ReadU32_BE(const unsigned char *ptr, const size_t offset) {
    return (ptr[offset + 3] << 24) | (ptr[offset + 2] << 16) | (ptr[offset + 1] << 8) | ptr[offset];
}

__uint32_t ReadU32_LE(const unsigned char *ptr, const size_t offset) {
    return (ptr[offset] << 24) | (ptr[offset + 1] << 16) | (ptr[offset + 2] << 8) | ptr[offset + 3];
}

__uint32_t ReadVariableLength(const unsigned char *ptr, size_t *offset) {
    __uint32_t val = ptr[*offset] & 0x7F;
    while (ptr[*offset] & 0x80)
    {
        (*offset)++;
        val = (val << 7) + (ptr[*offset] & 0x7F);
    }
    (*offset)++;
    return val;
}

void WriteU8(unsigned char *ptr, const size_t offset, __uint8_t value) {
    ptr[offset] = value;
}

void WriteU16(unsigned char *ptr, const size_t offset, __uint16_t value) {
    ptr[offset] = value;
    ptr[offset + 1] = value >> 8;
}

void WriteU24(unsigned char *ptr, const size_t offset, __uint32_t value) {
    ptr[offset] = value;
    ptr[offset + 1] = value >> 8;
    ptr[offset + 2] = value >> 16;
}

void WriteU32(unsigned char *ptr, const size_t offset, __uint32_t value) {
    ptr[offset] = value;
    ptr[offset + 1] = value >> 8;
    ptr[offset + 2] = value >> 16;
    ptr[offset + 3] = value >> 24;
}

__uint8_t WriteVariableLength(unsigned char *ptr, size_t offset, __uint32_t value) {
    __uint8_t size = 0;
    for (int i = 3; i > 0; i--)
    {
        if (value >> (7 * i))
        {
            ptr[offset + size] = (value >> (7 * i)) & 0x7F;
            ptr[offset + size] |= 0x80;
            size++;
        }
    }
    ptr[offset + size] = value & 0x7F;
    size++;
    return size;
}

__uint8_t VariableLength(__uint32_t value)
{
    __uint8_t size = 1;
    for (int i = 3; i > 0; i--)
    {
        if (value >> (7 * i))
        {
            size++;
        }
    }
    return size;
}

char *JoinPaths(char *parent, char *child)
{
    int newLen = strlen(parent) + strlen(child) + 2;
    char *newPath = malloc(newLen);
    snprintf(newPath, newLen, "%s/%s", parent, child);
    return newPath;
}

struct StrVec *StrVec_New(size_t capacity)
{
    struct StrVec *vec = malloc(sizeof(struct StrVec));

    vec->s = malloc(capacity * sizeof(char *));
    vec->count = 0;
    vec->capacity = capacity;

    return vec;
}

int strcmp_q(const void *s1, const void *s2)
{
    char *const *a = s1;
    char *const *b = s2;
    return strcmp(*a, *b);
}

int U32cmp_q(const void *i1, const void *i2)
{
    const int a = *(uint32_t*)i1;
    const int b = *(uint32_t*)i2;
    return (a > b) - (a < b);
}
