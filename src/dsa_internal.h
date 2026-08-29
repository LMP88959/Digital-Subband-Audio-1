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

#ifndef _DSA_INTERNAL_H_
#define _DSA_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dsa.h"

typedef struct {
    uint8_t *start;
    uint32_t pos;
} DSA_BS;

extern void dsa_bs_init(DSA_BS *bs, uint8_t *buffer);

extern void dsa_bs_align(DSA_BS *bs);

/* macros for really simple operations */
#define dsa_bs_aligned(bs) (((bs)->pos & 7) == 0)
#define dsa_bs_ptr(bs) ((bs)->pos / 8)
#define dsa_bs_set(bs, ptr) ((bs)->pos = (ptr) * 8)
#define dsa_bs_skip(bs, n_bytes) ((bs)->pos += (n_bytes) * 8)

extern void dsa_bs_concat(DSA_BS *bs, uint8_t *data, int len);

extern void dsa_bs_put_bit(DSA_BS *bs, int value);
extern unsigned dsa_bs_get_bit(DSA_BS *bs);

extern void dsa_bs_put_bits(DSA_BS *bs, unsigned n, unsigned value);
extern unsigned dsa_bs_get_bits(DSA_BS *bs, unsigned n);

extern unsigned dsa_bs_get_rice(DSA_BS *bs, unsigned *rk, unsigned *avg);
extern void dsa_bs_put_rice(DSA_BS *bs, unsigned x, unsigned *rk, unsigned *avg);
extern unsigned dsa_bs_get_erice(DSA_BS *bs, unsigned *rk, unsigned *avg);
extern void dsa_bs_put_erice(DSA_BS *bs, unsigned x, unsigned *rk, unsigned *avg);

extern int dsa_lb2(unsigned n);

extern void dsa_jms_fwd_transform(DSA_SAMPLE *pcm, unsigned len);
extern void dsa_jms_inv_transform(DSA_SAMPLE *pcm, unsigned len);

extern void dsa_fwd_sbt(DSA_SAMPLE *samps, int nchan, DSA_SAMPLE *out, int transform_type);
extern void dsa_inv_sbt(DSA_SAMPLE *coefs, int nchan, DSA_SAMPLE *out, int transform_type);

#define DSA_AQ_SUBBANDS 8
#define DSA_AQ_BITS 2
#define DSA_N_BINS (1 << DSA_AQ_BITS)
#define DSA_COMPUTE_AQ(base_q, qoff) ((2 * (base_q)) - (((base_q) * (qoff)) / DSA_N_BINS))

/* adaptive quantization info */
typedef struct {
    int qoff[DSA_MAX_FRAME_SUBBANDS];
    int transform_type;
} DSA_AQ;

/* persistent state for the psychoacoustic model */
typedef struct {
    int hist[18];
    int histsb[16][18];
    uint32_t prev_oscs[2][DSA_MAX_FRAME_SUBBANDS];
    int have_prev[2];
    int quant_hist[DSA_N_BINS];
    int a_weights[DSA_MAX_FRAME_SUBBANDS];
} DSA_PSYSTATE;

extern void dsa_psy(DSA_PSYSTATE *psy, DSA_SAMPLE *coefs, int len, int sample_rate, DSA_AQ *aq, int mode, int do_psy, int effort, int bitrate);

extern uint32_t dsa_encode(DSA_SAMPLE *coefs, uint8_t *out, int nchan, DSA_AQ *aq, int qf);
extern int dsa_decode(uint8_t *encoded, DSA_SAMPLE *out, uint32_t comp_sz, int nchan, int qf);

#ifdef __cplusplus
}
#endif

#endif
