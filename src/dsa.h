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

#ifndef _DSA_H_
#define _DSA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
#endif

typedef uint32_t DSA_FNUM; /* frame number */
typedef int32_t DSA_SAMPLE; /* PCM samples or subband coeffs, made 32-bit to avoid clipping */

#define DSA_MIN_BITRATE 4
#define DSA_MAX_BITRATE 512

#define DSA_QUANT_STEP 2
#define DSA_MIN_QUANT 1
#define DSA_MAX_QUANT (((1 << 15) - 1) / DSA_QUANT_STEP)

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

extern int dsa_channels_for_mode(int mode);

#ifndef DSA_MEMORY_STATS
#define DSA_MEMORY_STATS 1
#endif

#define DSA_FREE_AND_NULL(ptr) \
    if (ptr) {                 \
        dsa_free(ptr);         \
        ptr = NULL;            \
    }

extern void *dsa_alloc(int32_t size);
extern void dsa_free(void *ptr);

extern void dsa_memory_report(void);

#define DSA_LEVEL_NONE    0
#define DSA_LEVEL_ERROR   1
#define DSA_LEVEL_WARNING 2
#define DSA_LEVEL_INFO    3
#define DSA_LEVEL_DEBUG   4

extern char *dsa_lvlname[DSA_LEVEL_DEBUG + 1];

#define DSA_LOG_LVL(level, x) \
    do { if (level <= dsa_get_log_level()) { \
      printf("[DSA][%s] ", dsa_lvlname[level]); \
      printf("%s: %s(%d): ", __FILE__,  __FUNCTION__, __LINE__); \
      printf x; \
      printf("\n"); \
    }} while(0)\

#define DSA_ERROR(x)   DSA_LOG_LVL(DSA_LEVEL_ERROR, x)
#define DSA_WARNING(x) DSA_LOG_LVL(DSA_LEVEL_WARNING, x)
#define DSA_INFO(x)    DSA_LOG_LVL(DSA_LEVEL_INFO, x)
#define DSA_DEBUG(x)   DSA_LOG_LVL(DSA_LEVEL_DEBUG, x)

#if 1
#define DSA_ASSERT(x) do {                  \
    if (!(x)) {                             \
        DSA_ERROR(("assert: " #x));         \
        exit(-1);                           \
    }                                       \
} while(0)
#else
#define DSA_ASSERT(x)
#endif
extern void dsa_set_log_level(int level);
extern int dsa_get_log_level(void);

typedef struct {
    uint8_t *data;
    uint32_t len;
} DSA_BUF;

extern void dsa_mk_buf(DSA_BUF *buf, int size);
extern void dsa_buf_free(DSA_BUF *buffer);

#ifdef __cplusplus
}
#endif

#endif
