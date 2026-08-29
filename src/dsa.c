/*****************************************************************************/
/*
 * Digital Subband Audio 1
 *   DSA-1
 *
 *     -
 *    =--  2025-2026 EMMIR
 *   ==---  Envel Graphics
 *  ===----
 *
 *   GitHub : https://github.com/LMP88959
 *   YouTube: https://www.youtube.com/@EMMIR_KC/videos
 *   Discord: https://discord.com/invite/hdYctSmyQJ
 */
/*****************************************************************************/

#include "dsa.h"

char *dsa_lvlname[DSA_LEVEL_DEBUG + 1] = {
    "NONE",
    "ERROR",
    "WARNING",
    "INFO",
    "DEBUG"
};

static int lvl = DSA_LEVEL_ERROR;

extern void
dsa_set_log_level(int level)
{
    lvl = level;
}

extern int
dsa_get_log_level(void)
{
    return lvl;
}

#if DSA_MEMORY_STATS
static unsigned allocated = 0;
static unsigned freed = 0;
static unsigned allocated_bytes = 0;
static unsigned freed_bytes = 0;
static unsigned peak_alloc = 0;

#define DSA_ALIGNMENT 64

static void *
dsa_aligned_calloc(int32_t size)
{
    uint8_t *a = NULL;
    uint8_t *b = calloc(size + (DSA_ALIGNMENT - 1) + sizeof(void**), 1);
    if (!b) {
        DSA_ERROR(("failed to allocate memory"));
        return NULL;
    }
    a = b + (DSA_ALIGNMENT - 1) + sizeof(void**);
    a -= (intptr_t) a & (DSA_ALIGNMENT - 1);
    memcpy((void*) ((char *) a - sizeof(void*)), &b, sizeof(void*));
    return a;
}

static void
dsa_aligned_free(void *p)
{
    void *ptr;
    memcpy(&ptr, (void*) ((char*) p - sizeof(void*)), sizeof(void*));
    free(ptr);
}

extern void *
dsa_alloc(int32_t size)
{
    void *p;

    p = dsa_aligned_calloc(size + DSA_ALIGNMENT);
    if (!p) {
        return NULL;
    }
    *((int32_t *) p) = size;
    allocated++;
    allocated_bytes += size;
    if (peak_alloc < (allocated_bytes - freed_bytes)) {
        peak_alloc = (allocated_bytes - freed_bytes);
    }
    return (uint8_t *) p + DSA_ALIGNMENT;
}

extern void
dsa_free(void *ptr)
{
    uint8_t *p;
    int32_t nbytes;

    if (ptr == NULL) {
        DSA_ERROR(("attempting to free null pointer!"));
        return;
    }
    freed++;
    p = ((uint8_t *) ptr) - DSA_ALIGNMENT;
    memcpy(&nbytes, p, sizeof(int32_t));
    freed_bytes += nbytes;

    if (peak_alloc < (allocated_bytes - freed_bytes)) {
        peak_alloc = (allocated_bytes - freed_bytes);
    }
    dsa_aligned_free(p);
}

extern void
dsa_memory_report(void)
{
    DSA_DEBUG(("n alloc: %u", allocated));
    DSA_DEBUG(("n freed: %u", freed));
    DSA_DEBUG(("alloc bytes: %u", allocated_bytes));
    DSA_DEBUG(("freed bytes: %u", freed_bytes));
    DSA_DEBUG(("bytes not freed: %d", allocated_bytes - freed_bytes));
    DSA_DEBUG(("peak alloc: %u", peak_alloc));
}
#else
extern void *
dsa_alloc(int32_t size)
{
    return calloc(1, size);
}

extern void
dsa_free(void *ptr)
{
    free(ptr);
}

extern void
dsa_memory_report(void)
{
    DSA_DEBUG(("memory stats are disabled"));
}
#endif

extern void
dsa_buf_free(DSA_BUF *buf)
{
    if (buf->data) {
        dsa_free(buf->data);
        buf->data = NULL;
    }
}

extern void
dsa_mk_buf(DSA_BUF *buf, int size)
{
    memset(buf, 0, sizeof(*buf));
    buf->data = dsa_alloc(size);
    buf->len = size;
}

extern int
dsa_channels_for_mode(int mode)
{
    switch (mode) {
        case DSA_MODE_MONO:
            return 1;
        case DSA_MODE_LR_STEREO:
        case DSA_MODE_MS_STEREO:
            return 2;
        default:
            DSA_ERROR(("invalid mode!"));
            return 1;
    }
}

extern int
dsa_lb2(unsigned n)
{
    unsigned log2 = 0;

    n -= (n != 0);
    while (n > 0) {
        log2++;
        n >>= 1;
    }
    return log2;
}

/* joint (mid/side) encoding transform */
extern void
dsa_jms_fwd_transform(DSA_SAMPLE *spc, unsigned len)
{
    unsigned i;
    DSA_SAMPLE *Ls, *Rs;

    len /= 2;
    Ls = spc;
    Rs = spc + len;
    for (i = 0; i < len; i++) {
        int L, R;

        L = Ls[i];
        R = Rs[i];
        Ls[i] = (L + R);
        Rs[i] = (L - R);
    }
}

extern void
dsa_jms_inv_transform(DSA_SAMPLE *spc, unsigned len)
{
    unsigned i;
    DSA_SAMPLE *Ms, *Ss;

    len /= 2;
    Ms = spc;
    Ss = spc + len;
    for (i = 0; i < len; i++) {
        int M, S;

        M = Ms[i];
        S = Ss[i];
        Ms[i] = ((M + S) >> 1);
        Ss[i] = ((M - S) >> 1);
    }
}
