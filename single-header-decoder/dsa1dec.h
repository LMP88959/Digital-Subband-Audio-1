/*****************************************************************************/
/*
 * Digital Subband Audio 1 Single-Header Decoder Implementation
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
/*
 * This software was designed and written by EMMIR, 2025-2026 of Envel Graphics
 * If you release anything with it, a comment in your code/README saying
 * where you got this code would be a nice gesture but it’s not mandatory.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _DSA1DEC_H_
#define _DSA1DEC_H_

#ifdef __cplusplus
extern "C" {
#endif
/*
 * HOW TO INCLUDE THE DSA 1.0 DECODER IN YOUR PROGRAM:
 *
 * In one translation unit (usually .c/.cpp file), do the following:
 * #define _DSA1_IMPL_
 * #include "dsa1dec.h"
 *
 * if you need access to DSA1 types, defines, or declarations in another file,
 * all you need to do is include dsa1dec.h:
 * #include "dsa1dec.h"
 *
 *
 * OPTIONS:
 *
 * _DSA1_NO_ASSERT_ - define to remove asserts in the code
 *
 * _DSA1_NO_STDIO_ - define to omit stdio based functions, you will need to
 *                   provide your version of the following macro:
 *
 *        DSA_LOG_LVL(level, x)
 *              level - level of the log call
 *              x - printf style parameters ("format", data0, data1, etc..)
 *
 *
 * _DSA1_NO_STDINT_ - define if you don't have stdint.h available. You will
 *                    need to provide your own typedefs.
 *
 * _DSA1_NO_ALLOC_ - define to provide your own memory allocation and freeing
 *                   functions, you will need to provide your version of the
 *                   following macros:
 *
 *        DSA1_ALLOC_FUNC(num_bytes)
 *        DSA1_FREE_FUNC(pointer)
 *
 *                   !!!!! NOTE !!!!!
 *                   The alloc function MUST return a pointer to a block of
 *                   ZEROED OUT memory that is "num_bytes" in size.
 *                   If the memory returned by your alloc is not zeroed,
 *                   the behavior of this DSA1 implementation will be undefined.
 *
 *
 * _DSA1_MEMORY_STATS_ - define to enable memory counting and basic statistics
 *
 ******************************************************************************
 *
 * DECODING
 *
 * The best reference is the dsa1_dec_main.c program,
 * but a quick synopsis via pseudocode is given here:
 *
 * zero DSA_DECODER struct memory
 *
 * while (1) {
 *    DSA_BUF packet_buf;
 *
 *    hdr = read DSA_PACKET_HDR_SIZE bytes from source stream
 *    if (dsa1_mk_packet_buf(hdr, DSA_PACKET_HDR_SIZE, &buffer, &packet_type) < 0) {
 *        - packet reading error
 *        break;
 *    }
 *    read (packet_buf.len - DSA_PACKET_HDR_SIZE) bytes from source stream
 *        into packet_buf.data + DSA_PACKET_HDR_SIZE
 *
 *    code = dsa1_dec(&dec, &packet_buf, &pcm, &frameno);
 *    if (code == DSA_DEC_GOT_TAGS) {
 *        -- do something with the tags (read them into a DSA_TAGS struct via dsa1_dec_get_tags())
 *    } else if (code == DSA_DEC_GOT_ID) {
 *        -- got ID packet, only really used for 4CC / version validation
 *    } else if (code == DSA_DEC_NEED_NEXT) {
 *        -- skip, the decoder needs the next frame in order to properly decode the audio
 *        -- if you are keeping track, increment frame counter because a frame was read
 *    } else {
 *        if (code != DSA_DEC_OK && code != DSA_DEC_EOS) {
 *            continue;
 *        }
 *        if (code == DSA_DEC_EOS) {
 *            - end of stream
 *            break;
 *        }
 *
 *        - do what you need to do to decoded frame
 *
 *    }
 * }
 * dsa1_dec_free
 */

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/*********************** BEGINNING OF PUBLIC INTERFACE ***********************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#ifndef _DSA1_NO_STDINT_
#include <stdint.h>
#endif
#ifndef _DSA1_NO_STDIO_
#include <stdio.h>
#endif

#define DSA_FOURCC_0     'D'
#define DSA_FOURCC_1     'S'
#define DSA_FOURCC_2     'A'
#define DSA_FOURCC_3     '1'
#define DSA_VERSION_MINOR 0
#define DSA_VERSION_BUILD 0

#define DSA_PT_TAGS       0
#define DSA_PT_ID         1
#define DSA_PT_FRAME      2
#define DSA_PT_EOS        3

#define DSA_PACKET_HDR_SIZE (1 + 3 + 3) /* packet type + link offsets */
#define DSA_PACKET_TYPE_OFFSET 0
#define DSA_PACKET_PREV_OFFSET 1
#define DSA_PACKET_NEXT_OFFSET 4

typedef uint32_t DSA_FNUM; /* frame number */
typedef int32_t DSA_SAMPLE; /* PCM samples or subband coeffs, made 32-bit to avoid clipping */

#define DSA_QUANT_STEP 2

#define DSA_MAX_FRAME_SUBBANDS 11  /* number of subbands in a mono frame */
/* number of either mono or stereo (L + R) samples in a frame */
#define DSA_FRAME_SAMPLES (1 << DSA_MAX_FRAME_SUBBANDS)
/* the number of samples each frame overlaps with the previous frame
 * (if overlapping is enabled) */
#define DSA_FRAME_OVERLAP (44 * 2)

typedef struct {
#define DSA_MODE_MONO      0 /* single channel (mono) */
#define DSA_MODE_LR_STEREO 1 /* dual channel left-right stereo */
#define DSA_MODE_MS_STEREO 2 /* dual channel joint (mid-side) stereo */
    uint8_t mode;
    /* rate:
     * 0 = 11025 Hz
     * 1 = 22050 Hz
     * 2 = 44100 Hz
     * 3 = 48000 Hz
     */
    uint8_t rate;
    uint8_t depth; /* 0 = unsigned 8-bit audio, 1 = signed 16-bit audio */
    /* 0 = tell decoder to expect non-overlapped frames
     * 1 = tell decoder to expect overlapped frames that likely need extra processing
     */
    uint8_t overlapped;
    uint8_t transform_type;
    uint8_t reserved; /* reserved bits for future expansion */
} DSA_META;

typedef struct {
    void *data;
    DSA_META meta;
    uint16_t enc_size;
    uint16_t actual_pcm_size;
    uint16_t quant;
} DSA_FRAME;

typedef struct {
    DSA_SAMPLE *samples;
    DSA_META meta;
    uint32_t len; /* number of DSA_SAMPLEs */
} DSA_PCM;

/* tags for the whole stream/file */
typedef struct {
    uint16_t track_sz; /* string length */
    char *track;
    uint16_t artist_sz;
    char *artist;
    uint16_t album_sz;
    char *album;
    uint16_t comment_sz;
    char *comment;
    uint16_t year;
    uint8_t track_num;
    uint8_t total_tracks;
    uint8_t genre;
} DSA_TAGS;

/*********************************** DECODER **********************************/

typedef struct {
    DSA_TAGS tags;
    int got_tags;

    /* used internally */
    int first;
    DSA_PCM last_pcm;
    DSA_FNUM last_fnum;
    DSA_SAMPLE *output_buf;

    DSA_SAMPLE *alloc;
    struct {
        unsigned len;
        DSA_SAMPLE *LR;
        DSA_SAMPLE *L;
        DSA_SAMPLE *R;
    } hist[2];

    uint16_t ipol_table[DSA_FRAME_OVERLAP];
} DSA_DECODER;

#define DSA_DEC_OK        0
#define DSA_DEC_ERROR     1
#define DSA_DEC_EOS       2
#define DSA_DEC_GOT_ID    3
#define DSA_DEC_GOT_TAGS  4
#define DSA_DEC_NEED_NEXT 5

#define DSA_PKT_ERR_EOF -1
#define DSA_PKT_ERR_OOB -2 /* out of bytes */
#define DSA_PKT_ERR_PSZ -3 /* bad packet size */
#define DSA_PKT_ERR_4CC -4 /* bad 4cc */

typedef struct {
    uint8_t *data;
    uint32_t len;
} DSA_BUF;

extern int dsa1_mk_packet_buf(uint8_t *hdr, int hdrlen, DSA_BUF *rb, int *packet_type);

/* decode a buffer, returns the decoded frame in *pcm and the frame number in *fn */
extern int dsa1_dec(DSA_DECODER *d, DSA_BUF *buf, DSA_PCM *pcm, DSA_FNUM *fn);

extern void dsa1_dec_init(DSA_DECODER *d);

/* manually assign a buffer that the decoder will write the decoded samples to.
 * this buffer's memory management will be the users responsibility.
 *
 * NOTE 1: ensure outbuf has at least DSA_FRAME_SAMPLES worth of allocated DSA_SAMPLEs
 * NOTE 2: dsa1_dec is permitted to do whatever it wants to the contents of outbuf, do not
 *         expect some form of persistent state in it between dsa1_dec calls
 *
 * set outbuf to NULL to go back to the default strategy (reusing the decoder's internal buffer)
 */
extern void dsa1_dec_set_outbuf(DSA_DECODER *d, DSA_SAMPLE *outbuf);

/* get the tags that were decoded. NOTE: if no tags have been decoded
 * yet, the returned struct will not contain any useful values. */
extern DSA_TAGS *dsa1_dec_get_tags(DSA_DECODER *d);

/* free anything the decoder was holding on to */
extern void dsa1_dec_free(DSA_DECODER *d);

extern int dsa1_channels_for_mode(int mode);

#define DSA_FREE_AND_NULL(ptr) \
    if (ptr) {                 \
        dsa1_free(ptr);         \
        ptr = NULL;            \
    }

extern void *dsa1_alloc(int32_t size);
extern void dsa1_free(void *ptr);

extern void dsa1_memory_report(void);

#define DSA_LEVEL_NONE    0
#define DSA_LEVEL_ERROR   1
#define DSA_LEVEL_WARNING 2
#define DSA_LEVEL_INFO    3
#define DSA_LEVEL_DEBUG   4

extern char *dsa1_lvlname[DSA_LEVEL_DEBUG + 1];

#ifndef _DSA1_NO_STDIO_
#define DSA_LOG_LVL(level, x) \
    do { if (level <= dsa1_get_log_level()) { \
      printf("[DSA][%s] ", dsa1_lvlname[level]); \
      printf("%s: %s(%d): ", __FILE__,  __FUNCTION__, __LINE__); \
      printf x; \
      printf("\n"); \
    }} while(0)
#endif

#define DSA_ERROR(x)   DSA_LOG_LVL(DSA_LEVEL_ERROR, x)
#define DSA_WARNING(x) DSA_LOG_LVL(DSA_LEVEL_WARNING, x)
#define DSA_INFO(x)    DSA_LOG_LVL(DSA_LEVEL_INFO, x)
#define DSA_DEBUG(x)   DSA_LOG_LVL(DSA_LEVEL_DEBUG, x)

#ifndef _DSA1_NO_ASSERT_
#define DSA_ASSERT(x) do {                  \
    if (!(x)) {                             \
        DSA_ERROR(("assert: " #x));         \
        exit(-1);                           \
    }                                       \
} while(0)
#else
#define DSA_ASSERT(x)
#endif
extern void dsa1_set_log_level(int level);
extern int dsa1_get_log_level(void);

extern void dsa1_mk_buf(DSA_BUF *buf, int size);
extern void dsa1_buf_free(DSA_BUF *buffer);

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/*************************** END OF PUBLIC INTERFACE **************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

#ifdef _DSA1_IMPL_
#ifndef _DSA1_IMPL_GUARD_
#define _DSA1_IMPL_GUARD_

/********************************** INTERNAL **********************************/

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#endif

typedef struct {
    uint8_t *start;
    uint32_t pos;
} DSA_BS;

/* macros for really simple operations */
#define bsa_aligned(bs) (((bs)->pos & 7) == 0)
#define bsa_ptr(bs) ((bs)->pos / 8)

/*********************************** GENERAL **********************************/

char *dsa1_lvlname[DSA_LEVEL_DEBUG + 1] = {
    "NONE",
    "ERROR",
    "WARNING",
    "INFO",
    "DEBUG"
};

static int dsa1_loglvl = DSA_LEVEL_ERROR;

extern void
dsa1_set_log_level(int level)
{
    dsa1_loglvl = level;
}

extern int
dsa1_get_log_level(void)
{
    return dsa1_loglvl;
}

#ifndef _DSA1_NO_ALLOC_
#define DSA1_ALLOC_FUNC(num_bytes) calloc(1, num_bytes)
#define DSA1_FREE_FUNC(pointer) free(pointer)
#endif

#ifdef _DSA1_MEMORY_STATS_
static unsigned allocated = 0;
static unsigned freed = 0;
static unsigned allocated_bytes = 0;
static unsigned freed_bytes = 0;
static unsigned peak_alloc = 0;

#define DSA_ALIGNMENT 64

static void *
dsa1_aligned_calloc(int32_t size)
{
    uint8_t *a = NULL;
    uint8_t *b = DSA1_ALLOC_FUNC(size + (DSA_ALIGNMENT - 1) + sizeof(void**));
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
dsa1_aligned_free(void *p)
{
    void *ptr;
    memcpy(&ptr, (void*) ((char*) p - sizeof(void*)), sizeof(void*));
    DSA1_FREE_FUNC(ptr);
}

extern void *
dsa1_alloc(int32_t size)
{
    void *p;

    p = dsa1_aligned_calloc(size + DSA_ALIGNMENT);
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
dsa1_free(void *ptr)
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
    dsa1_aligned_free(p);
}

extern void
dsa1_memory_report(void)
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
dsa1_alloc(int32_t size)
{
    return DSA1_ALLOC_FUNC(size);
}

extern void
dsa1_free(void *ptr)
{
    DSA1_FREE_FUNC(ptr);
}

extern void
dsa1_memory_report(void)
{
    DSA_DEBUG(("memory stats are disabled"));
}
#endif

extern int
dsa1_mk_packet_buf(uint8_t *hdr, int hdrlen, DSA_BUF *rb, int *packet_type)
{
    int size;

    if (hdrlen == 0) {
        DSA_ERROR(("no data"));
        return DSA_PKT_ERR_EOF;
    }
    if (hdrlen < DSA_PACKET_HDR_SIZE) {
        DSA_ERROR(("not enough bytes"));
        return DSA_PKT_ERR_OOB;
    }
    size = (hdr[DSA_PACKET_NEXT_OFFSET + 0] << 16) |
           (hdr[DSA_PACKET_NEXT_OFFSET + 1] << 8) |
           (hdr[DSA_PACKET_NEXT_OFFSET + 2]);
    if (size == 0) {
        size = DSA_PACKET_HDR_SIZE;
    }
    if (size < DSA_PACKET_HDR_SIZE) {
        DSA_ERROR(("bad packet size"));
        return DSA_PKT_ERR_PSZ;
    }
    *packet_type = hdr[DSA_PACKET_TYPE_OFFSET];
    dsa1_mk_buf(rb, size);
    memcpy(rb->data, hdr, DSA_PACKET_HDR_SIZE);
    return 1;
}

extern void
dsa1_buf_free(DSA_BUF *buf)
{
    if (buf->data) {
        dsa1_free(buf->data);
        buf->data = NULL;
    }
}

extern void
dsa1_mk_buf(DSA_BUF *buf, int size)
{
    memset(buf, 0, sizeof(*buf));
    buf->data = (uint8_t *) dsa1_alloc(size);
    buf->len = size;
}

extern int
dsa1_channels_for_mode(int mode)
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

static int
dsa1_lb2(unsigned n)
{
    unsigned log2 = 0;

    n -= (n != 0);
    while (n > 0) {
        log2++;
        n >>= 1;
    }
    return log2;
}

static void
dsa1_jms_inv_transform(DSA_SAMPLE *spc, unsigned len)
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

/********************************** BITSTREAM *********************************/

static void
bsa_init(DSA_BS *s, uint8_t *buffer)
{
    s->start = buffer;
    s->pos = 0;
}

static void
bsa_align(DSA_BS *s)
{
    if (bsa_aligned(s)) {
        return; /* already aligned */
    }
    s->pos = ((s->pos + 7) & ((unsigned) (~0) << 3)); /* byte align */
}

static unsigned
bsa_get_bit(DSA_BS *s)
{
    unsigned out;

    out = s->start[bsa_ptr(s)] >> (7 - (s->pos & 7));
    s->pos++;

    return out & 1;
}

static unsigned
bsa_get_bits(DSA_BS *s, unsigned n)
{
    unsigned rem, bit, out = 0;

    while (n > 0) {
        rem = 8 - (s->pos & 7);
        rem = MIN(n, rem);
        bit = (7 - (s->pos & 7)) - rem + 1;
        out <<= rem;
        out |= (s->start[bsa_ptr(s)] & (((1 << rem) - 1) << bit)) >> bit;
        n -= rem;
        s->pos += rem;
    }
    return out;
}

static unsigned
bsa_update_rice_k(unsigned avg)
{
    unsigned k = 0;
    avg >>= 2;
    while (avg >>= 1) {
        k++;
    }
    return k;
}

static unsigned
bsa_update_rice_state(unsigned ravg, unsigned v)
{
    return ravg - (ravg >> 2) + v;
}

static unsigned
bsa_get_rice(DSA_BS *bs, unsigned *rk, unsigned *avg)
{
    unsigned k = *rk;
    unsigned v, q = 0;

    while (!bsa_get_bit(bs)) {
        q++;
    }
    v = (q << k) | bsa_get_bits(bs, k);

    *avg = bsa_update_rice_state(*avg, v);
    *rk = bsa_update_rice_k(*avg);
    return v;
}

/* escaped adaptive Rice coding */
#define RICE_ESCAPE_Q (DSA_MAX_FRAME_SUBBANDS * 2 / 3)

static unsigned
bsa_get_erice(DSA_BS *bs, unsigned *rk, unsigned *avg)
{
    unsigned k = *rk;
    unsigned v, q = 0;

    while (!bsa_get_bit(bs)) {
        q++;
    }
    if (q == RICE_ESCAPE_Q) {
        q = 0;
        while (!bsa_get_bit(bs)) {
            q++;
        }
        q = RICE_ESCAPE_Q - 1 + (((unsigned) 1 << q) | bsa_get_bits(bs, q));
    }
    v = (q << k) | bsa_get_bits(bs, k);

    *avg = bsa_update_rice_state(*avg, v);
    *rk = bsa_update_rice_k(*avg);
    return v;
}

/************************************ SBT *************************************/

static void
filter_I3_inv(DSA_SAMPLE *io, int n)
{
    DSA_SAMPLE *p, *end;

    io[0] += (io[1] * 4 + io[3] * 3) / 16;
    io[2] += (io[3] * 51 - io[1] * 29 + io[5] * 6) / 64;
    p = io + 4;
    end = io + n - 3;
    while (p + 2 < end) { /* two at a time */
        p[0] += (p[1] * 51 - p[-1] * 35 + (p[3] + p[-3]) * 6) / 64;
        p[2] += (p[3] * 51 - p[ 1] * 35 + (p[5] + p[-1]) * 6) / 64;
        p += 4;
    }
    *p += (p[1] * 51 - p[-1] * 35 + (p[3] + p[-3]) * 6) / 64; /* do the last one */
    io[n - 2] += (io[n - 1] * 51 - io[n - 3] * 29 + io[n - 5] * 6) / 64;

    p = io + 1;
    end = io + n;
    while (p < end) {
        DSA_SAMPLE x, y;

        x = p[-1];
        y = *p + x * 19 / 64;

        *p = y;
        p[-1] = x - y;
        p += 2;
    }

    io[1] += (io[2] * 37 + io[0] * 12 - (io[2] + io[4]) * 6) / 64;
    p = io + 3;
    end = io + n - 4;
    while (p < end) {
        *p += (p[1] * 37 + p[-1] * 12 - (p[-3] + p[3]) * 6) / 64;
        p += 2;
    }
    io[n - 3] += (io[n - 2] * 31 + io[n - 4] * 12 - io[n - 6] * 6) / 64;
    io[n - 1] += (io[n - 2] * 49 - io[n - 4] * 12) / 64;
}

static void
filter_I6_inv(DSA_SAMPLE *io, int n)
{
    DSA_SAMPLE *p, *end_0, *end_1;

    end_0 = io + n;
    end_1 = io + n - 1;

    io[0] -= io[1] * 3 / 2;
    for (p = io + 2; p < end_0; p += 2) {
        p[0] -= (p[-1] + p[1]) * 3 / 4;
    }
    for (p = io + 1; p < end_1; p += 2) {
        p[0] -= (p[-1] + p[1]) * 3 / 8;
    }
    io[n - 1] -= io[n - 2] * 3 / 4;

    io[0] -= io[1] / 8;
    for (p = io + 2; p < end_0; p += 2) {
        p[0] -= (p[-1] + p[1]) / 16;
    }
    for (p = io + 1; p < end_1; p += 2) {
        p[0] -= (p[-1] + p[1]) * 53 / 1024;
    }
    io[n - 1] -= io[n - 2] * 53 / 512;

    io[0] += io[1] * 15 / 32;
    for (p = io + 2; p < end_0; p += 2) {
        p[0] += (p[-1] + p[1]) * 15 / 64;
    }
    for (p = io + 1; p < end_1; p += 2) {
        p[0] += (p[-1] + p[1]) * 9 / 8;
    }
    io[n - 1] += io[n - 2] * 9 / 4;
}

static void
dwt_inv(DSA_SAMPLE *out, DSA_SAMPLE *in, int n, int transform_type)
{
    int i;
    DSA_SAMPLE *lo, *hi;

    lo = in;
    hi = in + (n / 2);
    for (i = 0; i < n; i += 2) {
        out[i + 0] = *lo++;
        out[i + 1] = *hi++;
    }

    if (n <= 8 || (transform_type == 1)) {
        filter_I6_inv(out, n);
    } else {
        filter_I3_inv(out, n);
    }
}

static void
inv_1d(DSA_SAMPLE *out, DSA_SAMPLE *in, int n, int transform_type)
{
    int sw, lvls, l;

    lvls = dsa1_lb2(n);
    for (l = lvls; l >= 1; l--) {
        sw = n >> (l - 1);
        if (sw >= 4) {
            int h = sw >> 1;
            dwt_inv(out + h, in + h, h, transform_type);
            memcpy(in + h, out + h, h * sizeof(*in));
        }
        dwt_inv(out, in, sw, transform_type);
        memcpy(in, out, sw * sizeof(*in));
    }
}

static void
dsa1_inv_sbt(DSA_SAMPLE *coefs, int nchan, DSA_SAMPLE *out, int transform_type)
{
    int c, coef_per_chan = (DSA_FRAME_SAMPLES / nchan);

    nchan *= coef_per_chan;
    for (c = 0; c < nchan; c += coef_per_chan) {
        inv_1d(out + c, coefs + c, coef_per_chan, transform_type);
    }
}

/************************************ CODING ************************************/

#define DSA_AQ_SUBBANDS 8
#define DSA_AQ_BITS 2
#define DSA_N_BINS (1 << DSA_AQ_BITS)
#define DSA_COMPUTE_AQ(base_q, qoff) ((2 * (base_q)) - (((base_q) * (qoff)) / DSA_N_BINS))

#define RUN_BITS 16
#define EOF_BITS 8
#define EOF_SYMBOL 0x44

#define START_K_RUN 0
#define START_K_VAL 5

#define GETRUN(bs) bsa_get_erice(bs, &rk, &ravg)
#define GETVAL(bs) bsa_get_rice(bs, &vk, &vavg)

static int
u2s(unsigned uv)
{
    return (uv >> 1) ^ (-(uv & 1));
}

/*
 * this looks slow but should be quick on most modern compilers/platforms as they
 * are clever enough to automatically convert this to a BSR/CLZ implementation
 */
static int
find_band(unsigned x)
{
    int band = -1;
    if (x == 0) {
        return 0;
    }
    while (x) {
        x >>= 1;
        band++;
    }
    return band;
}

static int
compute_quant(int sb, int sbperc, int qf, int qoff)
{
    if (qoff && sb >= (sbperc - 2)) {
        qf = qf * (DSA_N_BINS - qoff) / (DSA_N_BINS + 1 - qoff);
    }
    qf = DSA_COMPUTE_AQ(qf, qoff);
    return MAX(1, qf);
}

static void
invrle(DSA_BS *bs, DSA_SAMPLE *out, uint32_t bufsz, int nchan, int runs, int qf)
{
    unsigned rk = START_K_RUN, vk = START_K_VAL, ravg = 0, vavg = 0;
    int i, run = 0;
    int quants[DSA_MAX_FRAME_SUBBANDS];
    int sbperc, chanmask, chanshift = (nchan == 1 ? 0 : 1);

    sbperc = dsa1_lb2(DSA_FRAME_SAMPLES >> chanshift);
    chanmask = (1 << sbperc) - 1;
    bsa_align(bs);
    for (i = 0; i < sbperc; i++) {
        int qoff = DSA_N_BINS - 1;
        if (i >= MAX(0, sbperc - DSA_AQ_SUBBANDS)) {
            qoff = bsa_get_bits(bs, DSA_AQ_BITS);
        }
        quants[i] = compute_quant(i, sbperc, qf, qoff);
    }
    bsa_align(bs);
    run = (runs-- > 0) ? GETRUN(bs) : INT_MAX;
    bufsz *= 8; /* convert from bytes to bits */
    i = 0;
    while (i < DSA_FRAME_SAMPLES && runs >= 0) {
        unsigned v;
        if (run != 0) {
            i += run;
            run = 0;
        }
        v = GETVAL(bs) + 1;
        run = (runs-- > 0) ? GETRUN(bs) : INT_MAX;
        if (bs->pos >= bufsz) {
            return;
        }
        out[i] = u2s(v) * quants[find_band(i & chanmask)];
        i++;
    }
    bsa_align(bs);
}

static int
dsa1_decode(uint8_t *encoded, DSA_SAMPLE *out, uint32_t comp_sz, int nchan, int qf)
{
    DSA_BS bs;
    int runs;
    int success = 1;

    bsa_init(&bs, encoded);
    runs = bsa_get_bits(&bs, RUN_BITS);
    bsa_align(&bs);
    invrle(&bs, out, comp_sz, nchan, runs, qf);
    /* error detection */
    if (bsa_get_bits(&bs, EOF_BITS) != EOF_SYMBOL) {
        DSA_ERROR(("bad eof, frame data incomplete and/or corrupt"));
        success = 0;
    }
    bsa_align(&bs);

    return success;
}

/********************************** DECODER ***********************************/

static int
decode_packet_hdr(DSA_BS *bs)
{
    int pkt_type;

    pkt_type = bsa_get_bits(bs, 8);
    DSA_DEBUG(("packet type %02x", pkt_type));
    /* link offsets */
    bsa_get_bits(bs, 24);
    bsa_get_bits(bs, 24);

    return pkt_type;
}

static void
bs_get_string(DSA_BS *bs, uint16_t *len, char **str)
{
    unsigned i;
    unsigned rlen;
    char *rstr;

    rlen = bsa_get_bits(bs, 16);
    rstr = (char *) dsa1_alloc(rlen + 1);
    for (i = 0; i < rlen; i++) {
        rstr[i] = bsa_get_bits(bs, 8);
    }
    rstr[rlen] = '\0';
    *len = rlen;
    DSA_FREE_AND_NULL(*str);
    *str = rstr;
}

static int
decode_tags(DSA_DECODER *d, DSA_BS *bs)
{
    DSA_TAGS *tags = &d->tags;

    bsa_align(bs);
    bs_get_string(bs, &tags->track_sz, &tags->track);
    bs_get_string(bs, &tags->artist_sz, &tags->artist);
    bs_get_string(bs, &tags->album_sz, &tags->album);
    bs_get_string(bs, &tags->comment_sz, &tags->comment);

    tags->year = bsa_get_bits(bs, 16);
    tags->track_num = bsa_get_bits(bs, 8);
    tags->total_tracks = bsa_get_bits(bs, 8);
    tags->genre = bsa_get_bits(bs, 8);
    bsa_align(bs);
    return 1;
}

static int
decode_id(DSA_BS *bs)
{
    int c0, c1, c2, c3;
    int ver_min;

    bsa_align(bs);
    c0 = bsa_get_bits(bs, 8);
    c1 = bsa_get_bits(bs, 8);
    c2 = bsa_get_bits(bs, 8);
    c3 = bsa_get_bits(bs, 8);
    if (c0 != DSA_FOURCC_0 || c1 != DSA_FOURCC_1 || c2 != DSA_FOURCC_2 || c3 != DSA_FOURCC_3) {
        DSA_ERROR(("bad 4cc (%c %c %c %c)\n", c0, c1, c2, c3));
        return 0;
    }
    ver_min = bsa_get_bits(bs, 8);
    DSA_DEBUG(("version 1.%d", ver_min));
    bsa_align(bs);
    return 1;
}

static void
decode_frame(DSA_FRAME *frame, DSA_BS *bs, DSA_FNUM *fn)
{
    bsa_align(bs);
    *fn = bsa_get_bits(bs, 32);
    frame->enc_size = bsa_get_bits(bs, 16);
    frame->quant = bsa_get_bits(bs, 14) * DSA_QUANT_STEP;
    frame->meta.mode = bsa_get_bits(bs, 2);
    frame->meta.rate = bsa_get_bits(bs, 2);
    frame->meta.depth = bsa_get_bit(bs);
    frame->meta.overlapped = bsa_get_bit(bs);
    frame->meta.transform_type = bsa_get_bit(bs);
    if (bsa_get_bit(bs)) {
        frame->meta.reserved = bsa_get_bits(bs, 15);
    } else {
        frame->meta.reserved = 0;
    }
    if (bsa_get_bit(bs)) {
        frame->actual_pcm_size = bsa_get_bits(bs, 11);
    } else {
        frame->actual_pcm_size = DSA_FRAME_SAMPLES;
    }
    bsa_align(bs);
    frame->data = bs->start + bsa_ptr(bs);
}

#define IPOL_P 15
static void
init_ipol_lut(DSA_DECODER *d)
{
    int i;

    for (i = 0; i < DSA_FRAME_OVERLAP; i++) {
        int32_t t, t2, w;
        /* cubic */
        t = (i << IPOL_P) / DSA_FRAME_OVERLAP;
        t2 = (t * t) >> IPOL_P;
        w = (t2 * ((3 << IPOL_P) - 2 * t)) >> IPOL_P;
        d->ipol_table[i] = MIN(w, (1 << IPOL_P));
    }
}

static void
apply_overlap(DSA_DECODER *d, DSA_SAMPLE *prev, DSA_SAMPLE *curr, int length, int overlap)
{
    uint32_t lutshift = 0;
    int i;

    if (overlap <= 0 || length < overlap || !prev || !curr) {
        return;
    }
    switch (DSA_FRAME_OVERLAP / overlap) {
        case 1:
            lutshift = 0;
            break;
        case 2:
            lutshift = 1;
            break;
        default:
            DSA_ERROR(("bad overlap? %d", overlap));
            DSA_ASSERT(0);
            return;
    }
    for (i = 0; i < overlap; i++) {
        DSA_SAMPLE ps, cs;
        int32_t w;

        ps = prev[length - overlap + i];
        cs = curr[i];
        w = d->ipol_table[i << lutshift];
        curr[i] = (ps * ((1 << IPOL_P) - w) + cs * w + (1 << (IPOL_P - 1))) >> IPOL_P;
    }
}

/* merge separate L and R buffers into interleaved audio (LRLR...) */
static void
merge(DSA_SAMPLE *splitL, DSA_SAMPLE *splitR, int nsamp, int nchan, DSA_SAMPLE *interleaved)
{
    int i, j, c;
    int samp_per_chan;

    samp_per_chan = nsamp / nchan;
    for (c = 0; c < nchan; c++) {
        j = c;
        for (i = 0; i < samp_per_chan; i++) {
            interleaved[j] = (c == 0 ? splitL : splitR)[i];
            j += nchan;
        }
    }
}

extern void
dsa1_dec_init(DSA_DECODER *d)
{
    memset(d, 0, sizeof(*d));
    d->first = 1;
    d->alloc = (DSA_SAMPLE *) dsa1_alloc((1 + 2) * DSA_FRAME_SAMPLES * sizeof(*d->alloc));
    d->output_buf = d->alloc + 0 * DSA_FRAME_SAMPLES;
    d->hist[0].LR = d->alloc + 1 * DSA_FRAME_SAMPLES;
    d->hist[1].LR = d->alloc + 2 * DSA_FRAME_SAMPLES;
    d->hist[0].L = d->hist[0].LR;
    d->hist[0].R = d->hist[0].LR;
    d->hist[1].L = d->hist[1].LR;
    d->hist[1].R = d->hist[1].LR;
    init_ipol_lut(d);
}

extern void
dsa1_dec_free(DSA_DECODER *d)
{
    DSA_FREE_AND_NULL(d->alloc);
    DSA_FREE_AND_NULL(d->tags.track);
    DSA_FREE_AND_NULL(d->tags.artist);
    DSA_FREE_AND_NULL(d->tags.album);
    DSA_FREE_AND_NULL(d->tags.comment);
}

extern void
dsa1_dec_set_outbuf(DSA_DECODER *d, DSA_SAMPLE *outbuf)
{
    if (outbuf) {
        d->output_buf = outbuf;
    } else {
        d->output_buf = d->alloc + 0 * DSA_FRAME_SAMPLES;
    }
}

extern DSA_TAGS *
dsa1_dec_get_tags(DSA_DECODER *d)
{
    DSA_TAGS *tags;

    tags = (DSA_TAGS *) dsa1_alloc(sizeof(*tags));
    memcpy(tags, &d->tags, sizeof(*tags));
    /* copy strings over */
    tags->track = (char *) dsa1_alloc(tags->track_sz);
    memcpy(tags->track, d->tags.track, tags->track_sz);

    tags->artist = (char *) dsa1_alloc(tags->artist_sz);
    memcpy(tags->artist, d->tags.artist, tags->artist_sz);

    tags->album = (char *) dsa1_alloc(tags->album_sz);
    memcpy(tags->album, d->tags.album, tags->album_sz);

    tags->comment = (char *) dsa1_alloc(tags->comment_sz);
    memcpy(tags->comment, d->tags.comment, tags->comment_sz);
    return tags;
}

extern int
dsa1_dec(DSA_DECODER *d, DSA_BUF *buffer, DSA_PCM *out, DSA_FNUM *fn)
{
    DSA_BS bs;
    int pkt_type;
    DSA_FRAME frame;
    DSA_PCM pcm;
    int channels;
    char temp[sizeof(d->hist[0])]; /* for swapping */
    int ret = DSA_DEC_OK;

    if (fn == NULL) {
        DSA_ERROR(("FNUM 'fn' pointer was null!"));
        ret = DSA_DEC_ERROR;
        goto cleanup;
    }
    *fn = -1;

    bsa_init(&bs, buffer->data);
    pkt_type = decode_packet_hdr(&bs);

    if (pkt_type != DSA_PT_FRAME && pkt_type != DSA_PT_EOS) {
        ret = DSA_DEC_ERROR;
        if (pkt_type == DSA_PT_TAGS) {
            DSA_DEBUG(("decoding tags"));
            if (decode_tags(d, &bs)) {
                d->got_tags = 1;
                ret = DSA_DEC_GOT_TAGS;
            }
        } else if (pkt_type == DSA_PT_ID) {
            DSA_DEBUG(("decoding ID"));
            if (decode_id(&bs)) {
                ret = DSA_DEC_GOT_ID;
            }
        } else {
            DSA_ERROR(("unrecognized/invalid packet type? 0x%x [%d]", pkt_type, pkt_type));
        }
        goto cleanup;
    }
    if (out == NULL) {
        DSA_ERROR(("PCM 'out' pointer was null!"));
        ret = DSA_DEC_ERROR;
        goto cleanup;
    }
    /* flush final frame */
    if (pkt_type == DSA_PT_EOS) {
        DSA_DEBUG(("decoding end of stream"));
        memcpy(out, &d->last_pcm, sizeof(*out));
        out->samples = d->output_buf;
        *fn = d->last_fnum + 1;

        merge(d->hist[0].L, d->hist[0].R, out->len, dsa1_channels_for_mode(out->meta.mode), out->samples);

        ret = DSA_DEC_EOS;
        goto cleanup;
    }
    memset(&frame, 0, sizeof(frame));
    memset(&pcm, 0, sizeof(pcm));
    decode_frame(&frame, &bs, fn);
    if (frame.enc_size == 0) {
        DSA_ERROR(("frame.enc_size was zero"));
        ret = DSA_DEC_ERROR;
        goto cleanup;
    }
    if (frame.actual_pcm_size > DSA_FRAME_SAMPLES) {
        DSA_ERROR(("frame.actual_pcm_size was %u, should actually be <= %d", frame.actual_pcm_size, DSA_FRAME_SAMPLES));
        ret = DSA_DEC_ERROR;
        goto cleanup;
    }

    channels = dsa1_channels_for_mode(frame.meta.mode);
    /* setup right channel pointers */
    if (channels == 2) {
        d->hist[0].R = d->hist[0].L + (DSA_FRAME_SAMPLES / 2);
        d->hist[1].R = d->hist[1].L + (DSA_FRAME_SAMPLES / 2);
    }
    memset(d->output_buf, 0, DSA_FRAME_SAMPLES * sizeof(*d->output_buf));
    if (!dsa1_decode(frame.data, d->output_buf, frame.enc_size, channels, frame.quant)) {
        ret = DSA_DEC_ERROR;
        goto cleanup;
    }

    dsa1_inv_sbt(d->output_buf, channels, d->hist[1].LR, frame.meta.transform_type);

    if (frame.meta.mode == DSA_MODE_MS_STEREO) {
        dsa1_jms_inv_transform(d->hist[1].LR, DSA_FRAME_SAMPLES);
    }

    pcm.samples = d->output_buf;
    pcm.meta = frame.meta;
    pcm.len = frame.actual_pcm_size;
    /* we want to store the last PCM with the actual size */
    memcpy(&d->last_pcm, &pcm, sizeof(d->last_pcm));
    pcm.len = DSA_FRAME_SAMPLES;
    if (pcm.meta.overlapped) {
        pcm.len -= DSA_FRAME_OVERLAP;
    }
    *out = pcm;

    if (!d->first) {
        int samps, overl;

        samps = DSA_FRAME_SAMPLES / channels;
        overl = DSA_FRAME_OVERLAP / channels;
        if (pcm.meta.overlapped) {
            apply_overlap(d, d->hist[0].L, d->hist[1].L, samps, overl);
            if (channels == 2) {
                apply_overlap(d, d->hist[0].R, d->hist[1].R, samps, overl);
            }
        }
        merge(d->hist[0].L, d->hist[0].R, DSA_FRAME_SAMPLES, channels, pcm.samples);
    }
    memcpy(temp, &d->hist[0], sizeof(temp));
    memcpy(&d->hist[0], &d->hist[1], sizeof(d->hist[0]));
    memcpy(&d->hist[1], temp, sizeof(d->hist[1]));

    d->last_fnum = *fn;
    if (d->first) {
        d->first = 0;
        ret = DSA_DEC_NEED_NEXT;
    }
cleanup:
    dsa1_buf_free(buffer);
    return ret;
}

#endif /* dsa1 impl guard */
#endif /* dsa1 impl */

#ifdef __cplusplus
}
#endif

#endif
