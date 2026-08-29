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

#define OPTIMIZED_INVERSE 1

/* pos/neg reflect */
#define RP(i, n) ((i) >= (n) ? (2 * (n) - (i) - 2) : (i))
#define RN(i) ((i) < 0 ? -(i) : (i))

/*
 * fast integer transforms with dyadic rational coefficients:
 *
 * the I3/I6 transforms were found through a search algorithm
 *
 * these are biorthogonal 'wavelets'
 *
 * I tried a bunch of different transforms and ended up settling
 * on these as they seemed to have a winning combination
 * of good compressibility, audio quality, and complexity
 *
 * spectrally, they are not great at all, but they did sound
 * better than transforms that have superior frequency isolation
 *
 * NOTE: these implementations expect 'n' (length of signal) to always be even
 */
static void
filter_I3_fwd(DSA_SAMPLE *io, int n)
{
    int i;

    for (i = 1; i < n; i += 2) {
        io[i] -= ((io[RP(i + 1, n)] * 37) + (io[i - 1] * 12) -
                  (io[RN(i - 3)] + io[RP(i + 3, n)]) * 6) / 64;
    }

    for (i = 1; i < n; i += 2) {
        io[i - 1] += io[i];
        io[i] -= (io[i - 1] * 19 / 64);
    }

    for (i = 0; i < n; i += 2) {
        io[i] -= ((io[i + 1] * 51) - (io[RN(i - 1)] * 35) +
                  (io[RP(i + 3, n)] + io[RN(i - 3)]) * 6) / 64;
    }
}

static void
filter_I6_fwd(DSA_SAMPLE *io, int n)
{
    int i;

    for (i = 1; i < n; i += 2) {
        io[i] -= (io[i - 1] + io[RP(i + 1, n)]) * 9 / 8;
    }
    for (i = 0; i < n; i += 2) {
        io[i] -= (io[RN(i - 1)] + io[i + 1]) * 15 / 64;
    }
    for (i = 1; i < n; i += 2) {
        io[i] += (io[i - 1] + io[RP(i + 1, n)]) * 53 / 1024;
    }
    for (i = 0; i < n; i += 2) {
        io[i] += (io[RN(i - 1)] + io[i + 1]) / 16;
    }
    for (i = 1; i < n; i += 2) {
        io[i] += (io[i - 1] + io[RP(i + 1, n)]) * 3 / 8;
    }
    for (i = 0; i < n; i += 2) {
        io[i] += (io[RN(i - 1)] + io[i + 1]) * 3 / 4;
    }
}

#if OPTIMIZED_INVERSE
/* about 1.18x faster than the regular inverse on my machine */
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

/* about 1.14x faster than the regular inverse on my machine */
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

#else
static void
filter_I3_inv(DSA_SAMPLE *io, int n)
{
    int i;

    for (i = 0; i < n; i += 2) {
        io[i] += ((io[i + 1] * 51) - (io[RN(i - 1)] * 35) +
                  (io[RP(i + 3, n)] + io[RN(i - 3)]) * 6) / 64;
    }

    for (i = 1; i < n; i += 2) {
        io[i] += (io[i - 1] * 19 / 64);
        io[i - 1] -= io[i];
    }

    for (i = 1; i < n; i += 2) {
        io[i] += ((io[RP(i + 1, n)] * 37) + (io[i - 1] * 12) -
                  (io[RN(i - 3)] + io[RP(i + 3, n)]) * 6) / 64;
    }
}

static void
filter_I6_inv(DSA_SAMPLE *io, int n)
{
    int i;

    for (i = 0; i < n; i += 2) {
        io[i] -= (io[RN(i - 1)] + io[i + 1]) * 3 / 4;
    }
    for (i = 1; i < n; i += 2) {
        io[i] -= (io[i - 1] + io[RP(i + 1, n)]) * 3 / 8;
    }
    for (i = 0; i < n; i += 2) {
        io[i] -= (io[RN(i - 1)] + io[i + 1]) / 16;
    }
    for (i = 1; i < n; i += 2) {
        io[i] -= (io[i - 1] + io[RP(i + 1, n)]) * 53 / 1024;
    }
    for (i = 0; i < n; i += 2) {
        io[i] += (io[RN(i - 1)] + io[i + 1]) * 15 / 64;
    }
    for (i = 1; i < n; i += 2) {
        io[i] += (io[i - 1] + io[RP(i + 1, n)]) * 9 / 8;
    }
}
#endif

static void
dwt_fwd(DSA_SAMPLE *out, DSA_SAMPLE *in, int n, int transform_type)
{
    int i;
    DSA_SAMPLE *lo, *hi;

    if (n <= 8 || (transform_type == 1)) {
        filter_I6_fwd(in, n);
    } else {
        filter_I3_fwd(in, n);
    }

    lo = out;
    hi = out + (n / 2);
    for (i = 0; i < n; i += 2) {
        *lo++ = in[i + 0];
        *hi++ = in[i + 1];
    }
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

/* dyadic DWT with one extra decomposition on highpass subband
 *
 * the resulting subband tree looks like this:
 *
 *              S(t)               length=N         input signal
 *              /  \
 *            H(t)  L(t)           length=N/2       1st decomposition level
 *            / \      \
 *        HL(t) HH(t)  /\
 *                 H(t)  L(t)      length=N/4       2nd decomposition level
 *                 / \      \
 *             HL(t) HH(t)  /\
 *                      H(t)  L(t) length=N/8       3rd decomposition level
 *                      / \      \
 *                   HL(t) HH(t)  \
 *
 *                                ...etc...
 *
 * S(t) = original signal
 * L(t) = low pass
 * H(t) = highpass
 */

static void
fwd_1d(DSA_SAMPLE *out, DSA_SAMPLE *in, int n, int transform_type)
{
    int sw, lvls, l;

    lvls = dsa_lb2(n);
    for (l = 1; l <= lvls; l++) {
        sw = n >> (l - 1);
        dwt_fwd(out, in, sw, transform_type);
        memcpy(in, out, sw * sizeof(*in));
        if (sw >= 4) { /* decompose highpass subband */
            int h = sw >> 1;
            dwt_fwd(out + h, in + h, h, transform_type);
            memcpy(in + h, out + h, h * sizeof(*in));
        }
    }
}

static void
inv_1d(DSA_SAMPLE *out, DSA_SAMPLE *in, int n, int transform_type)
{
    int sw, lvls, l;

    lvls = dsa_lb2(n);
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

extern void
dsa_fwd_sbt(DSA_SAMPLE *samps, int nchan, DSA_SAMPLE *out, int transform_type)
{
    int c, samp_per_chan = (DSA_FRAME_SAMPLES / nchan);

    nchan *= samp_per_chan;
    for (c = 0; c < nchan; c += samp_per_chan) {
        fwd_1d(out + c, samps + c, samp_per_chan, transform_type);
    }
}

extern void
dsa_inv_sbt(DSA_SAMPLE *coefs, int nchan, DSA_SAMPLE *out, int transform_type)
{
    int c, coef_per_chan = (DSA_FRAME_SAMPLES / nchan);

    nchan *= coef_per_chan;
    for (c = 0; c < nchan; c += coef_per_chan) {
        inv_1d(out + c, coefs + c, coef_per_chan, transform_type);
    }
}
