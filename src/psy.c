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

#include "dsa_encoder.h"

#define UABSDIF(a, b) (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))

#define NORM_P 6
#define MAX_IMP (1 << 10)
#define IMP_TO_BIN_RATIO (MAX_IMP / (1 << DSA_AQ_BITS))

static unsigned
iisqrt(unsigned n)
{
    unsigned pos, res, rem;
    if (n == 0) {
        return 0;
    }
    res = 0;
    pos = 1 << 30;
    rem = n;

    while (pos > rem) {
        pos >>= 2;
    }
    while (pos) {
        unsigned dif = res + pos;
        res >>= 1;
        if (rem >= dif) {
            rem -= dif;
            res += pos;
        }
        pos >>= 2;
    }
    return res;
}

static int
get_band_bounds(int len, int sb, int max_sb, int *beg, int *end)
{
    sb = (max_sb - 1 - sb);
    *beg = len >> (sb + 1);
    *end = len >> sb;

    if ((*end - *beg) <= 2) { /* too small to provide useful info */
        return 0;
    }
    return 1;
}

/* very approximate A-weighting function */
static uint32_t
a_weight_approx(uint32_t f)
{
    uint32_t f2, a, b;

    f >>= 4;
    f2 = f * f;
    a = f2 * 1024 / (f2 + 2);
    b = f2 * 1024 / (f2 + 581406);

    return (a * b) * 16384 / (f2 + 309);
}

static int
bitrate_priority_curve(int bitrate) {
    bitrate = CLAMP(bitrate, DSA_MIN_BITRATE, DSA_MAX_BITRATE);
    /* gives a nonlinear curve [0,100] */
    return (1024 / bitrate) - 2;
}

static void
calc_weights(DSA_PSYSTATE *psy, int sample_rate, int num_bands, int bitrate)
{
    int i, total = 0;
    int weights[DSA_MAX_FRAME_SUBBANDS];
    int priority = bitrate_priority_curve(bitrate);

    priority = (sample_rate * priority / 3000);

    for (i = 0; i < num_bands; i++) {
        int lo_freq, hi_freq;

        lo_freq = sample_rate / (1 << ((num_bands - i) + 1));
        hi_freq = sample_rate / (1 << ((num_bands - i) + 0));
        /* center frequency */
        weights[i] = a_weight_approx(((lo_freq + hi_freq) / 2) + priority);
        total += weights[i];
    }
    total = MAX(total, 1);
    /* 'normalize' */
    for (i = 0; i < num_bands; i++) {
        psy->a_weights[i] = (weights[i] * 512) / total;
    }
}

static uint32_t
avgL2(DSA_SAMPLE *x, int n, int *shift, int effort, uint32_t max_safe_thresh) {
    uint32_t maxv = 0;
    int i, nsamp = 0, sh = 0;
    uint32_t sum = 0;

    for (i = 0; i < n; i++) {
        uint32_t a = abs(x[i]);
        if (maxv < a) {
            maxv = a;
        }
    }
    /* shift so squares won't overflow int32 */
    while (maxv > max_safe_thresh) {
        maxv >>= 1;
        sh++;
    }

    *shift = sh;

    effort = 1 << ((DSA_MAX_EFFORT - effort) / 3); /* skip over coefs depending on effort */

    for (i = 0; i < n; i += effort) {
        DSA_SAMPLE v = x[i] / (1 << sh);
        sum += v * v;
        nsamp++;
    }

    return sum / MAX(nsamp, 1);
}

static uint32_t
avgL1(DSA_SAMPLE *coefs, int beg, int end)
{
    uint32_t sum = 0;
    int i;
    int n = end - beg;
    if (n <= 0) {
        return 1;
    }
    for (i = beg; i < end; i++) {
        sum += abs(coefs[i]);
    }
    return sum / n;
}

static uint32_t
oscratio(DSA_SAMPLE *x, int n, int shift, int32_t threshold)
{
    uint32_t energy = 0;
    int32_t corr = 0;
    uint32_t used = 0;
    int i;

    if (n < 2) {
        return 0;
    }
    for (i = 0; i < n - 1; i++) {
        int32_t a = x[i];
        int32_t b = x[i + 1];

        if (abs(a) < threshold || abs(b) < threshold) {
            continue;
        }
        a /= (1 << shift);
        b /= (1 << shift);
        energy += a * a;
        corr += a * b;
        used++;
    }
    corr = abs(corr);

    /* safeguard against potential overflowing */
    while (corr > (1 << (31 - 9)) || energy > (1 << (31 - 9))) {
        corr >>= 1;
        energy >>= 1;
    }
    if (used < 4 || energy == 0) {
        return 0;
    }
    corr = (corr * 512) / energy;
    return corr;
}

static int32_t
inter_band_corr(
    DSA_SAMPLE *coefs,
    int band_beg, int band_end,
    int parent_beg, int parent_end, int pnorm,
    int child_beg, int child_end, int cnorm,
    uint32_t em,
    int use_child)
{
    int32_t diff, corr = 0;
    uint32_t ep = 0, ec = 0;
    uint32_t avg_neighbors;

    if (band_end <= band_beg ||
        parent_end <= parent_beg ||
        (use_child && child_end <= child_beg)) {
        return 0;
    }

    ep = avgL1(coefs, parent_beg, parent_end) * pnorm >> NORM_P;
    if (use_child) {
        ec = avgL1(coefs, child_beg, child_end) * cnorm >> NORM_P;
    } else {
        ec = ep;
    }
    avg_neighbors = (ep + ec) / 2;
    diff = UABSDIF(em, avg_neighbors); /* delta energy between current band and neighboring bands */
    /* safeguard against potential overflowing */
    while (diff > (1 << (31 - 8))) {
        diff >>= 1;
        avg_neighbors >>= 1;
    }
    corr = 256 - ((diff << 8) / (avg_neighbors + 1));
    return CLAMP(corr, 0, 256);
}

static int
compute_transform_type(uint32_t *rms_mags, int max_sb)
{
    uint32_t total = 0;
    uint32_t maxv = 0;
    int i, dominant_band = 0;

    for (i = 0; i < max_sb; i++) {
        total += rms_mags[i];
        if (maxv < rms_mags[i]) {
            maxv = rms_mags[i];
            dominant_band = i;
        }
    }

    if (total != 0) {
        int concentration_pct;
        uint32_t cluster = maxv;
        if (dominant_band > 0) {
            cluster += rms_mags[dominant_band - 1];
        }
        if (dominant_band + 1 < max_sb) {
            cluster += rms_mags[dominant_band + 1];
        }
        concentration_pct = (100 * cluster) / total;
        if (dominant_band <= (max_sb * 2 / 3) && concentration_pct >= 85) {
            return 1;
        }
    }
    return 0;
}

/* assumes the subband coefs represent frequencies of 1 channel in order lowest -> highest in the modified DWT fashion described in sbt.c */
extern void
dsa_psy(DSA_PSYSTATE *psy, DSA_SAMPLE *coefs, int len, int sample_rate, DSA_AQ *aq, int mode, int do_psy, int effort, int bitrate)
{
    int sb, max_sb;
    int32_t imp_raw[DSA_MAX_FRAME_SUBBANDS];
    uint32_t oscs[DSA_MAX_FRAME_SUBBANDS];
    uint32_t rms_mags[DSA_MAX_FRAME_SUBBANDS];
    uint32_t corr[DSA_MAX_FRAME_SUBBANDS];
    int band_sizes[DSA_MAX_FRAME_SUBBANDS];
    int32_t norms[DSA_MAX_FRAME_SUBBANDS];
    int channel = mode & 1;
    uint32_t max_safe_thresh;
    uint32_t global_energy = 0;
    int norm = 1 << NORM_P;
    int nvsb = 0;

    max_sb = dsa_lb2(len);
    calc_weights(psy, sample_rate, max_sb, bitrate);

    memset(imp_raw, 0, sizeof(imp_raw));
    memset(oscs, 0, sizeof(oscs));
    memset(rms_mags, 0, sizeof(rms_mags));
    memset(corr, 0, sizeof(corr));
    memset(band_sizes, 0, sizeof(band_sizes));
    memset(norms, 0, sizeof(norms));
    max_safe_thresh = iisqrt(INT_MAX / (1 << DSA_MAX_FRAME_SUBBANDS));

    /* calculate approximate normalization factors,
     * this is to counter the coefficient scaling created by the forward transforms.
     *
     * normalizing their energies makes them approximately directly comparable
     */
    for (sb = max_sb - 1; sb >= 0; sb--) {
        norms[sb] = norm;
        norm = norm * 2 / 3;
    }
    for (sb = 0; sb < max_sb; sb++) {
        int beg, end;
        uint32_t avgsq = 0;
        int valid = get_band_bounds(len, sb, max_sb, &beg, &end);
        int shift = 0;
        int oscHL, oscHH;
        int half_band;

        band_sizes[sb] = end - beg;
        half_band = band_sizes[sb] / 2;
        if (!valid) {
            continue;
        }

        avgsq = avgL2(coefs + beg, band_sizes[sb], &shift, effort, max_safe_thresh);
        rms_mags[sb] = iisqrt(avgsq) << shift;
        /* osc is a ratio so the norm doesn't matter, NOTE it uses the unnormalized mags */
        oscHL = oscratio(coefs + beg, half_band, shift, rms_mags[sb] >> 2);
        oscHH = oscratio(coefs + beg + half_band, half_band, shift, rms_mags[sb] >> 2);
        oscs[sb] = MIN(oscHL, oscHH);
        oscs[sb] = oscs[sb] * oscs[sb] >> 9;
        rms_mags[sb] = rms_mags[sb] * norms[sb] >> NORM_P;
        global_energy += rms_mags[sb];
        nvsb++;
    }
    global_energy /= MAX(nvsb, 1);

    for (sb = 0; sb < max_sb; sb++) {
        aq->qoff[sb] = 0;
    }

    if (do_psy & DSA_PSY_ADAPTIVE_QUANT) {
        int32_t imp_min = INT32_MAX, imp_max = INT32_MIN;
        uint32_t energy_var = 0;

        for (sb = max_sb - 1; sb >= 0; sb--) {
            int beg = 0, end = 0;
            int parent_beg = 0, parent_end = 0;
            int child_beg = 0, child_end = 0;
            int use_child = (sb + 1 < max_sb);
            uint32_t avg_magHL, avg_magHH;
            int corrHL, corrHH;

            if (!get_band_bounds(len, sb, max_sb, &beg, &end)) {
                continue;
            }
            if (!get_band_bounds(len, sb - 1, max_sb, &parent_beg, &parent_end)) {
                continue;
            }
            if (use_child) {
                if (!get_band_bounds(len, sb + 1, max_sb, &child_beg, &child_end)) {
                    continue;
                }
            }

            avg_magHL = avgL1(coefs, beg, beg + (end - beg) / 2) * norms[sb] >> NORM_P;
            corrHL = inter_band_corr(
                    coefs,
                    beg, beg + (end - beg) / 2,
                    parent_beg, parent_beg + (parent_end - parent_beg) / 2, norms[sb - 1],
                    child_beg, child_beg + (child_end - child_beg) / 2, use_child ? norms[sb + 1] : 0,
                            avg_magHL,
                            use_child
            );
            avg_magHH = avgL1(coefs, beg + (end - beg) / 2, end) * norms[sb] >> NORM_P;
            corrHH = inter_band_corr(
                    coefs,
                    beg + (end - beg) / 2, end,
                    parent_beg + (parent_end - parent_beg) / 2, parent_end, norms[sb - 1],
                    child_beg + (child_end - child_beg) / 2, child_end, use_child ? norms[sb + 1] : 0,
                            avg_magHH,
                            use_child
            );
            corr[sb] = (corrHL + corrHH) / 2;
        }

        for (sb = 0; sb < max_sb; sb++) {
            int stab = 0;
            uint32_t prev, curr;
            uint32_t mask, n_mags;
            int32_t imp = 0;

            energy_var += UABSDIF(rms_mags[sb], global_energy);

            n_mags = rms_mags[MAX(sb - 1, 0)] * psy->a_weights[MAX(sb - 1, 0)] >> 7;
            if (sb == (max_sb - 1)) {
                n_mags *= 2;
            } else {
                n_mags += rms_mags[sb + 1] * psy->a_weights[sb + 1] >> 7;
            }
            mask = psy->a_weights[sb] * 2 * rms_mags[sb] / MAX(n_mags, 1);

            prev = psy->prev_oscs[channel][sb];
            curr = oscs[sb];
            psy->prev_oscs[channel][sb] = curr;

            if (psy->have_prev[channel] == mode && prev && curr) {
                uint32_t sum, dif;

                sum = MAX(prev + curr, 1);
                dif = UABSDIF(prev, curr);
                stab = 256 - ((dif << 8) / sum);
            }
            imp += mask;
            imp += oscs[sb];
            imp += stab;
            imp += corr[sb];
            /* these are marked unimportant because they will have little problem being represented after quantization */
            if (rms_mags[sb] > global_energy) {
                imp = 0;
            }
            psy->histsb[sb][1] += mask;
            psy->histsb[sb][2] += oscs[sb];
            psy->histsb[sb][3] += stab;
            psy->histsb[sb][4] += corr[sb];
            psy->histsb[sb][5] += rms_mags[sb];
            psy->histsb[sb][6] += global_energy;
            imp_raw[sb] = imp;
            imp_min = MIN(imp_min, imp);
            imp_max = MAX(imp_max, imp);
        }
        energy_var /= max_sb;
        for (sb = 0; sb < max_sb; sb++) {
            int bin, imp;

            imp = imp_raw[sb] * energy_var * 5 / (4 * MAX(1, imp_max - imp_min));
            imp = CLAMP(imp, 0, MAX_IMP);
            psy->histsb[sb][0] += imp;

            bin = (imp + (IMP_TO_BIN_RATIO / 2)) / IMP_TO_BIN_RATIO;

            aq->qoff[sb] = CLAMP(bin, 0, DSA_N_BINS - 1);
        }
    }

    psy->have_prev[channel] = mode;
    for (sb = 0; sb < max_sb; sb++) {
        psy->quant_hist[aq->qoff[sb]]++;
        psy->hist[sb] += aq->qoff[sb];
    }
    psy->hist[16] = max_sb;
    psy->hist[17]++;
    aq->transform_type = compute_transform_type(rms_mags, max_sb);
}
