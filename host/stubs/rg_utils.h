/* Host stub: avoid clashing with Darwin fortified strlcpy/strlcat macros. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RG_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RG_MAX(a, b) ((a) > (b) ? (a) : (b))
#define RG_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t rg_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n;
    if (!dst || size == 0)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    n = strlen(src);
    if (n >= size) {
        memcpy(dst, src, size - 1);
        dst[size - 1] = '\0';
        return size - 1;
    }
    memcpy(dst, src, n + 1);
    return n;
}

static inline size_t rg_strlcat(char *dst, const char *src, size_t size)
{
    size_t dlen;
    if (!dst || size == 0)
        return 0;
    dlen = strlen(dst);
    if (dlen >= size)
        return dlen;
    return dlen + rg_strlcpy(dst + dlen, src ? src : "", size - dlen);
}

/* Hide system macros so accidental calls still compile. */
#undef strlcpy
#undef strlcat
#define strlcpy rg_strlcpy
#define strlcat rg_strlcat

#ifdef __cplusplus
}
#endif
