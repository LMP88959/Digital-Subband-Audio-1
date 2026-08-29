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

#include "dsa_internal.h"

#define RUN_BITS 16
#define EOF_BITS 8
#define EOF_SYMBOL 0x44

#define START_K_RUN 0
#define START_K_VAL 5

#define GETRUN(bs) dsa_bs_get_erice(bs, &rk, &ravg)
#define GETVAL(bs) dsa_bs_get_rice(bs, &vk, &vavg)

#define PUTRUN(bs, x) dsa_bs_put_erice(bs, x, &rk, &ravg)
#define PUTVAL(bs, x) dsa_bs_put_rice(bs, x, &vk, &vavg)

static unsigned
s2u(int v)
{
    return (2 * v) ^ (v < 0 ? ~0 : 0);
}

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

static int
fwdrle(DSA_BS *bs, DSA_SAMPLE *in, int nchan, DSA_AQ *aq, int qf)
{
    unsigned rk = START_K_RUN, vk = START_K_VAL, ravg = 0, vavg = 0;
    int i, run = 0, nruns = 0;
    int quants[DSA_MAX_FRAME_SUBBANDS];
    int sbperc, chanmask, chanshift = (nchan == 1 ? 0 : 1);

    sbperc = dsa_lb2(DSA_FRAME_SAMPLES >> chanshift);
    chanmask = (1 << sbperc) - 1;
    dsa_bs_align(bs);
    for (i = 0; i < sbperc; i++) {
        int qoff = DSA_N_BINS - 1;
        /* lowest subbands do not have adaptive quant */
        if (i >= MAX(0, sbperc - DSA_AQ_SUBBANDS)) {
            qoff = aq->qoff[i] & (DSA_N_BINS - 1);
            dsa_bs_put_bits(bs, DSA_AQ_BITS, qoff);
        }
        quants[i] = compute_quant(i, sbperc, qf, qoff);
    }
    dsa_bs_align(bs);
    for (i = 0; i < DSA_FRAME_SAMPLES; i++) {
        int sv, sb, tqf, v = in[i];
        sb = find_band(i & chanmask);
        tqf = quants[sb];
        if (v < 0) {
            sv = 1;
            v = -v;
        } else {
            sv = 0;
        }
        /* quantize */
        v = (v + (tqf * 2 / 5)) / tqf;
        if (v) {
            PUTRUN(bs, run);
            PUTVAL(bs, s2u(sv ? -v : v) - 1);
            run = -1;
            nruns++;
        }
        run++;
    }
    dsa_bs_align(bs);
    return nruns;
}

static void
invrle(DSA_BS *bs, DSA_SAMPLE *out, uint32_t bufsz, int nchan, int runs, int qf)
{
    unsigned rk = START_K_RUN, vk = START_K_VAL, ravg = 0, vavg = 0;
    int i, run = 0;
    int quants[DSA_MAX_FRAME_SUBBANDS];
    int sbperc, chanmask, chanshift = (nchan == 1 ? 0 : 1);

    sbperc = dsa_lb2(DSA_FRAME_SAMPLES >> chanshift);
    chanmask = (1 << sbperc) - 1;
    dsa_bs_align(bs);
    for (i = 0; i < sbperc; i++) {
        int qoff = DSA_N_BINS - 1;
        if (i >= MAX(0, sbperc - DSA_AQ_SUBBANDS)) {
            qoff = dsa_bs_get_bits(bs, DSA_AQ_BITS);
        }
        quants[i] = compute_quant(i, sbperc, qf, qoff);
    }
    dsa_bs_align(bs);
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
    dsa_bs_align(bs);
}

extern uint32_t
dsa_encode(DSA_SAMPLE *coefs, uint8_t *out, int nchan, DSA_AQ *aq, int qf)
{
    DSA_BS bs;
    uint32_t startp, endp;
    int nruns = 0;
    dsa_bs_init(&bs, out);

    startp = dsa_bs_ptr(&bs);
    dsa_bs_put_bits(&bs, RUN_BITS, 0);
    dsa_bs_align(&bs);
    nruns = fwdrle(&bs, coefs, nchan, aq, qf);
    endp = dsa_bs_ptr(&bs);
    dsa_bs_set(&bs, startp);
    dsa_bs_put_bits(&bs, RUN_BITS, nruns);
    dsa_bs_set(&bs, endp);
    dsa_bs_align(&bs);
    dsa_bs_put_bits(&bs, EOF_BITS, EOF_SYMBOL); /* 'end of frame' symbol */
    dsa_bs_align(&bs);

    endp = dsa_bs_ptr(&bs);
    return endp - startp;
}

extern int
dsa_decode(uint8_t *encoded, DSA_SAMPLE *out, uint32_t comp_sz, int nchan, int qf)
{
    DSA_BS bs;
    int runs;
    int success = 1;

    dsa_bs_init(&bs, encoded);
    runs = dsa_bs_get_bits(&bs, RUN_BITS);
    dsa_bs_align(&bs);
    invrle(&bs, out, comp_sz, nchan, runs, qf);
    /* error detection */
    if (dsa_bs_get_bits(&bs, EOF_BITS) != EOF_SYMBOL) {
        DSA_ERROR(("bad eof, frame data incomplete and/or corrupt"));
        success = 0;
    }
    dsa_bs_align(&bs);

    return success;
}
