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

#ifndef _DSA_ENCODER_H_
#define _DSA_ENCODER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dsa_internal.h"

#define DSA_ENCODER_VERSION 1

#define DSA_ENC_NUM_BUFS  0x07 /* mask */
#define DSA_ENC_FINISHED  0x08

#define DSA_MIN_EFFORT 0
#define DSA_MAX_EFFORT 10

#define DSA_RATE_CONTROL_ABR  0 /* average bitrate */
#define DSA_RATE_CONTROL_CQP  1 /* constant quantization parameter */

typedef struct {
    int target_bitrate_kbps;
    uint16_t explicit_quant;

    int effort; /* encoding effort [DSA_MIN_EFFORT..DSA_MAX_EFFORT] */

#define DSA_PSY_ADAPTIVE_QUANT      (1 << 0)
#define DSA_PSY_ENABLE_LR_STEREO    (1 << 1)
#define DSA_PSY_ENABLE_MS_STEREO    (1 << 2)

#define DSA_PSY_ALL 0xff
    int do_psy; /* BITFIELD enable psychoacoustic optimizations */

    /* rate control */
    int rc_mode;
    int target_bpb; /* bits per block that the rate control algorithm is targeting */

    int reservoir;
    int max_reservoir;
    int min_reservoir;

    int prev_budget;

    int n_quants;
    int avgquant;

    /* audio info */
    int sample_rate;
    int bits_per_samp;
    int overlapped;
    int block_size;
    int num_channels;

    /* output */
    uint16_t frame_quant;
    uint16_t frame_bitrate;

    DSA_PSYSTATE psy;
    DSA_SAMPLE *alloc;

    DSA_SAMPLE *fullframe;
    DSA_SAMPLE *tempframe;
    DSA_SAMPLE *tempcoefs;
    uint8_t *encodebuf;
    unsigned encodebufsz;

    struct DSA_STATS {
        unsigned lrnum; /* num stereo LR frames */
        unsigned msnum; /* num stereo MS frames */
        unsigned tnum[2]; /* num transform types */
    } stats;

    DSA_FNUM next_fnum;
    DSA_TAGS tags;
    int force_id;
    int force_tags;
    int prev_link;
} DSA_ENCODER;

extern void dsa_enc_init(DSA_ENCODER *enc);
extern void dsa_enc_free(DSA_ENCODER *enc);

extern void dsa_enc_start(DSA_ENCODER *enc);
extern void dsa_enc_set_tags(DSA_ENCODER *enc, DSA_TAGS *t);

/* returns number of buffers available in bufs ptr
 *
 * if pcm == NULL, an ID packet will be encoded
 * */
extern int dsa_enc(DSA_ENCODER *enc, DSA_SAMPLE *pcm, uint32_t nsamp, DSA_BUF *bufs);
extern void dsa_enc_end_of_stream(DSA_ENCODER *enc, DSA_BUF *bufs);

/* for debug logging of the psy model's statistics */
extern void dsa_logpsy(DSA_ENCODER *enc);

#ifdef __cplusplus
}
#endif

#endif
