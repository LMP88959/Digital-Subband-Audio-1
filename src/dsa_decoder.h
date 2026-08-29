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

#ifndef _DSA_DECODER_H_
#define _DSA_DECODER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dsa.h"

#define DSA_DECODER_VERSION 1

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

/* decode a buffer, returns the decoded frame in *pcm and the frame number in *fn */
extern int dsa_dec(DSA_DECODER *d, DSA_BUF *buf, DSA_PCM *pcm, DSA_FNUM *fn);

extern void dsa_dec_init(DSA_DECODER *d);

/* manually assign a buffer that the decoder will write the decoded samples to.
 * this buffer's memory management will be the users responsibility.
 *
 * NOTE 1: ensure outbuf has at least DSA_FRAME_SAMPLES worth of allocated DSA_SAMPLEs
 * NOTE 2: dsa_dec is permitted to do whatever it wants to the contents of outbuf, do not
 *         expect some form of persistent state in it between dsa_dec calls
 *
 * set outbuf to NULL to go back to the default strategy (reusing the decoder's internal buffer)
 */
extern void dsa_dec_set_outbuf(DSA_DECODER *d, DSA_SAMPLE *outbuf);

/* get the tags that were decoded. NOTE: if no tags have been decoded
 * yet, the returned struct will not contain any useful values. */
extern DSA_TAGS *dsa_dec_get_tags(DSA_DECODER *d);

/* free anything the decoder was holding on to */
extern void dsa_dec_free(DSA_DECODER *d);

#ifdef __cplusplus
}
#endif

#endif
