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

#include "dsa_encoder.h"

static void
rc_init(DSA_ENCODER *enc)
{
    int br, sr, bs, nc, block_ms;
    int res_len_ms = 50; /* length of reservoir in ms. TODO constant but could be tunable */

    br = enc->target_bitrate_kbps;
    sr = enc->sample_rate;
    bs = enc->block_size;
    nc = enc->num_channels;

    enc->target_bpb = (br * 1024 * bs) / sr;
    enc->target_bpb = MAX(enc->target_bpb, 256);
    /* approximate length (in milliseconds) of a block */
    block_ms = (bs * 1000) / (sr * nc);
    block_ms = MAX(block_ms, 1);

    enc->reservoir = 0;
    enc->max_reservoir = enc->target_bpb * res_len_ms / block_ms;
    enc->min_reservoir = -enc->target_bpb * res_len_ms / block_ms;

    enc->prev_budget = enc->target_bpb;
}

static int
rc_get_budget(DSA_ENCODER *enc)
{
    int budget, lo, hi, r;

    /* adjust budget based on number of bits available in reservoir and previous budget */
    r = ((enc->reservoir * enc->target_bpb) / (enc->max_reservoir * 4));

    budget = enc->target_bpb + r;
    budget = (2 * enc->prev_budget + budget) / 3;

    lo = (enc->target_bpb * 4) / 5;
    hi = (enc->target_bpb * 9) / 4;
    budget = CLAMP(budget, lo, hi);
    enc->prev_budget = budget;
    return (budget * enc->sample_rate) / (enc->block_size * 1024);
}

static void
rc_update_kbps(DSA_ENCODER *enc, int actual_kbps)
{
    int actual_bits;

    actual_bits = (actual_kbps * enc->block_size * 1024) / enc->sample_rate;

    enc->reservoir += enc->target_bpb - actual_bits;
    enc->reservoir = CLAMP(enc->reservoir, enc->min_reservoir, enc->max_reservoir);
}

static int
use_mid_side(DSA_ENCODER *enc, DSA_SAMPLE *pcm, int num_samples, int effort)
{
    uint32_t lr[2] = { 0, 0 };
    uint32_t ms[2] = { 0, 0 };
    int nzlr[2] = { 0, 0 }; /* count number zeros */
    int nzms[2] = { 0, 0 };
    int i, quant_guess = ~((1 << dsa_lb2(enc->avgquant)) - 1); /* guessing approximately what this new frame will be quantized with */

    effort = 2 << ((DSA_MAX_EFFORT - effort) / 3); /* skip over frames depending on effort */
    for (i = 0; i < num_samples; i += effort) {
        uint32_t l, r, m, s;
        DSA_SAMPLE left  = pcm[i + 0];
        DSA_SAMPLE right = pcm[i + 1];

        l = abs(left);
        r = abs(right);
        m = abs(left + right) >> 1;
        s = abs(left - right) >> 1;
        lr[0] += l;
        lr[1] += r;
        ms[0] += m;
        ms[1] += s;
        nzlr[0] += ((l & quant_guess) == 0);
        nzlr[1] += ((r & quant_guess) == 0);
        nzms[0] += ((m & quant_guess) == 0);
        nzms[1] += ((s & quant_guess) == 0);
    }
    /* pseudo-normalize */
    while (lr[0] > 65535 || lr[1] > 65535 ||
           ms[0] > 65535 || ms[1] > 65535) {
        lr[0] >>= 1;
        lr[1] >>= 1;
        ms[0] >>= 1;
        ms[1] >>= 1;
    }
    while (nzlr[0] > 255 || nzlr[1] > 255 ||
           nzms[0] > 255 || nzms[1] > 255) {
        nzlr[0] >>= 1;
        nzlr[1] >>= 1;
        nzms[0] >>= 1;
        nzms[1] >>= 1;
    }
    return (ms[0] * ms[1]) / (MAX(1, nzms[0]) * MAX(1, nzms[1])) <=
           (lr[0] * lr[1]) / (MAX(1, nzlr[0]) * MAX(1, nzlr[1]));
}

static void
encode_packet_hdr(DSA_BS *bs, int pkt_type)
{
    dsa_bs_put_bits(bs, 8, pkt_type);

    /* reserve space for link offsets */
    dsa_bs_put_bits(bs, 24, 0);
    dsa_bs_put_bits(bs, 24, 0);
}

static void
bs_put_string(DSA_BS *bs, uint16_t len, char *str)
{
    int i;

    dsa_bs_put_bits(bs, 16, len);
    for (i = 0; i < len; i++) {
        dsa_bs_put_bits(bs, 8, str[i]);
    }
}

static void
encode_tags(DSA_BUF *buf, DSA_TAGS *tags)
{
    DSA_BS bs;
    unsigned next_link;
    unsigned next_start = DSA_PACKET_NEXT_OFFSET;

    dsa_mk_buf(buf, 64 + (2 * (tags->track_sz + tags->artist_sz + tags->album_sz + tags->comment_sz)));

    dsa_bs_init(&bs, buf->data);

    encode_packet_hdr(&bs, DSA_PT_TAGS);

    dsa_bs_align(&bs);
    bs_put_string(&bs, tags->track_sz, tags->track);
    bs_put_string(&bs, tags->artist_sz, tags->artist);
    bs_put_string(&bs, tags->album_sz, tags->album);
    bs_put_string(&bs, tags->comment_sz, tags->comment);

    dsa_bs_put_bits(&bs, 16, tags->year);
    dsa_bs_put_bits(&bs, 8, tags->track_num);
    dsa_bs_put_bits(&bs, 8, tags->total_tracks);
    dsa_bs_put_bits(&bs, 8, tags->genre);
    dsa_bs_align(&bs);

    next_link = dsa_bs_ptr(&bs);
    buf->data[next_start + 0] = (next_link >> 16) & 0xff;
    buf->data[next_start + 1] = (next_link >>  8) & 0xff;
    buf->data[next_start + 2] = (next_link >>  0) & 0xff;

    buf->len = next_link; /* trim length to actual size */
}

static void
encode_id(DSA_BUF *buf)
{
    DSA_BS bs;
    unsigned next_link;
    unsigned next_start = DSA_PACKET_NEXT_OFFSET;

    dsa_mk_buf(buf, 64);

    dsa_bs_init(&bs, buf->data);

    encode_packet_hdr(&bs, DSA_PT_ID);

    dsa_bs_align(&bs);
    dsa_bs_put_bits(&bs, 8, DSA_FOURCC_0);
    dsa_bs_put_bits(&bs, 8, DSA_FOURCC_1);
    dsa_bs_put_bits(&bs, 8, DSA_FOURCC_2);
    dsa_bs_put_bits(&bs, 8, DSA_FOURCC_3);
    dsa_bs_put_bits(&bs, 8, DSA_VERSION_MINOR);
    dsa_bs_align(&bs);

    next_link = dsa_bs_ptr(&bs);
    buf->data[next_start + 0] = (next_link >> 16) & 0xff;
    buf->data[next_start + 1] = (next_link >>  8) & 0xff;
    buf->data[next_start + 2] = (next_link >>  0) & 0xff;

    buf->len = next_link; /* trim length to actual size */
}

static int
encode_frame(DSA_BUF *buf, DSA_FRAME *frame, DSA_FNUM fnum)
{
    DSA_BS bs;
    unsigned next_link;
    unsigned next_start = DSA_PACKET_NEXT_OFFSET;

    dsa_mk_buf(buf, 128 + frame->enc_size);

    dsa_bs_init(&bs, buf->data);

    encode_packet_hdr(&bs, DSA_PT_FRAME);

    dsa_bs_align(&bs);
    dsa_bs_put_bits(&bs, 32, fnum);
    dsa_bs_put_bits(&bs, 16, frame->enc_size);
    dsa_bs_put_bits(&bs, 14, frame->quant);
    dsa_bs_put_bits(&bs, 2, frame->meta.mode & 0x3);
    dsa_bs_put_bits(&bs, 2, frame->meta.rate & 0x3);
    dsa_bs_put_bits(&bs, 1, frame->meta.depth & 0x1);
    dsa_bs_put_bits(&bs, 1, frame->meta.overlapped ? 1 : 0);
    dsa_bs_put_bits(&bs, 1, frame->meta.transform_type & 0x1);
    dsa_bs_put_bits(&bs, 1, 0); /* reserved bit is unused at the moment */
    if (frame->actual_pcm_size == DSA_FRAME_SAMPLES) {
        dsa_bs_put_bit(&bs, 0);
    } else if (frame->actual_pcm_size < DSA_FRAME_SAMPLES) {
        /* signal to decoder that the frame doesn't have DSA_FRAME_SAMPLES worth of samples */
        dsa_bs_put_bit(&bs, 1);
        dsa_bs_put_bits(&bs, 11, frame->actual_pcm_size);
    } else {
        DSA_ERROR(("strange input PCM size: %d", frame->actual_pcm_size));
        DSA_ASSERT(0);
        return 0;
    }
    dsa_bs_align(&bs);
    dsa_bs_concat(&bs, frame->data, frame->enc_size);
    dsa_bs_align(&bs);

    next_link = dsa_bs_ptr(&bs);
    buf->data[next_start + 0] = (next_link >> 16) & 0xff;
    buf->data[next_start + 1] = (next_link >>  8) & 0xff;
    buf->data[next_start + 2] = (next_link >>  0) & 0xff;

    buf->len = next_link; /* trim length to actual size */
    return 1;
}

static void
set_link_offsets(DSA_ENCODER *enc, DSA_BUF *buffer, int is_eos)
{
    uint8_t *data = buffer->data;
    unsigned next_link;
    unsigned prev_start = DSA_PACKET_PREV_OFFSET;
    unsigned next_start = DSA_PACKET_NEXT_OFFSET;

    next_link = is_eos ? 0 : buffer->len;

    data[prev_start + 0] = (enc->prev_link >> 16) & 0xff;
    data[prev_start + 1] = (enc->prev_link >>  8) & 0xff;
    data[prev_start + 2] = (enc->prev_link >>  0) & 0xff;

    data[next_start + 0] = (next_link >> 16) & 0xff;
    data[next_start + 1] = (next_link >>  8) & 0xff;
    data[next_start + 2] = (next_link >>  0) & 0xff;

    enc->prev_link = next_link;
}

extern void
dsa_enc_end_of_stream(DSA_ENCODER *enc, DSA_BUF *bufs)
{
    DSA_BS bs;

    dsa_mk_buf(&bufs[0], DSA_PACKET_HDR_SIZE);
    dsa_bs_init(&bs, bufs[0].data);

    encode_packet_hdr(&bs, DSA_PT_EOS);

    set_link_offsets(enc, &bufs[0], 1);
    DSA_INFO(("creating end of stream packet"));
}

extern void
dsa_enc_init(DSA_ENCODER *enc)
{
    memset(enc, 0, sizeof(*enc));
    enc->force_id = 1;
    enc->force_tags = 1;
    enc->alloc = dsa_alloc((1 + 1 + 1) * DSA_FRAME_SAMPLES * sizeof(*enc->alloc));
    enc->fullframe = enc->alloc + 0 * DSA_FRAME_SAMPLES;
    enc->tempframe = enc->alloc + 1 * DSA_FRAME_SAMPLES;
    enc->tempcoefs = enc->alloc + 2 * DSA_FRAME_SAMPLES;
    /* points to 2x DSA_FRAME_SAMPLES worth of data
     * encoding white noise at 512kbps didn't even reach
     * 1.5x the input size, so 2x should be enough
     */
    enc->encodebufsz = 2 * DSA_FRAME_SAMPLES * sizeof(*enc->encodebuf);
    enc->encodebuf = dsa_alloc(enc->encodebufsz);
}

extern void
dsa_enc_start(DSA_ENCODER *enc)
{
    rc_init(enc);

    enc->psy.have_prev[0] = -1;
    enc->psy.have_prev[1] = -1;
}

extern void
dsa_enc_free(DSA_ENCODER *enc)
{
    DSA_FREE_AND_NULL(enc->alloc);
    DSA_FREE_AND_NULL(enc->encodebuf);
    DSA_FREE_AND_NULL(enc->tags.track);
    DSA_FREE_AND_NULL(enc->tags.artist);
    DSA_FREE_AND_NULL(enc->tags.album);
    DSA_FREE_AND_NULL(enc->tags.comment);
}

extern void
dsa_enc_set_tags(DSA_ENCODER *enc, DSA_TAGS *t)
{
    DSA_TAGS *tags = &enc->tags;

    DSA_FREE_AND_NULL(enc->tags.track);
    DSA_FREE_AND_NULL(enc->tags.artist);
    DSA_FREE_AND_NULL(enc->tags.album);
    DSA_FREE_AND_NULL(enc->tags.comment);
    memcpy(tags, t, sizeof(*tags));
    /* copy strings over */
    tags->track = dsa_alloc(tags->track_sz);
    memcpy(tags->track, t->track, tags->track_sz);

    tags->artist = dsa_alloc(tags->artist_sz);
    memcpy(tags->artist, t->artist, tags->artist_sz);

    tags->album = dsa_alloc(tags->album_sz);
    memcpy(tags->album, t->album, tags->album_sz);

    tags->comment = dsa_alloc(tags->comment_sz);
    memcpy(tags->comment, t->comment, tags->comment_sz);
}

/* split interleaved audio (LRLR...) into separate L and R buffers */
static void
split(DSA_SAMPLE *interleaved, int nsamp, int nchan, DSA_SAMPLE *splitL, DSA_SAMPLE *splitR)
{
    int i, j, c;
    int samp_per_chan;

    samp_per_chan = nsamp / nchan;
    for (c = 0; c < nchan; c++) {
        j = c;
        for (i = 0; i < samp_per_chan; i++) {
            (c == 0 ? splitL : splitR)[i] = interleaved[j];
            j += nchan;
        }
    }
}

static int
freq_for_rate(int rate)
{
    switch (rate) {
        case 0:
            return 11025;
        case 1:
            return 22050;
        case 2:
            return 44100;
        case 3:
            return 48000;
        default:
            DSA_ASSERT(0);
            return 44100;
    }
}

static int
analyze_frame(DSA_ENCODER *enc,
              DSA_FRAME *frame, DSA_SAMPLE **out_coefs,
              DSA_AQ *aq, int bitrate, int do_psy)
{
    DSA_AQ aqL = { 0 };
    DSA_AQ aqR = { 0 };
    int b, nchan;
    int samp_per_chan, samp_rate = freq_for_rate(frame->meta.rate);
    nchan = dsa_channels_for_mode(frame->meta.mode);
    samp_per_chan = DSA_FRAME_SAMPLES / nchan;

    memcpy(enc->tempframe, enc->fullframe, DSA_FRAME_SAMPLES * sizeof(*enc->tempframe));
    /* transform the time-domain samples in order to determine adaptive quantization */
#define ANALYSIS_TRANSFORM_TYPE 0
    dsa_fwd_sbt(enc->tempframe, nchan, enc->tempcoefs, ANALYSIS_TRANSFORM_TYPE);

#define AVG(x, y) (((x) + (y) + 1) / 2)
    switch (frame->meta.mode) {
        case DSA_MODE_MONO:
            dsa_psy(&enc->psy, enc->tempcoefs, samp_per_chan, samp_rate, &aqL, 0, do_psy, enc->effort, bitrate);
            break;
        case DSA_MODE_LR_STEREO:
            dsa_psy(&enc->psy, enc->tempcoefs + (0 * samp_per_chan), samp_per_chan, samp_rate, &aqL, 0, do_psy, enc->effort, bitrate);
            dsa_psy(&enc->psy, enc->tempcoefs + (1 * samp_per_chan), samp_per_chan, samp_rate, &aqR, 1, do_psy, enc->effort, bitrate);
            for (b = 0; b < DSA_MAX_FRAME_SUBBANDS; b++) {
                aqL.qoff[b] = AVG(aqL.qoff[b], aqR.qoff[b]);
            }
            aqL.transform_type |= aqR.transform_type;
            break;
        case DSA_MODE_MS_STEREO:
            dsa_psy(&enc->psy, enc->tempcoefs + (0 * samp_per_chan), samp_per_chan, samp_rate, &aqL, 2, do_psy, enc->effort, bitrate);
            dsa_psy(&enc->psy, enc->tempcoefs + (1 * samp_per_chan), samp_per_chan, samp_rate, &aqR, 3, do_psy, enc->effort, bitrate);
            for (b = 0; b < DSA_MAX_FRAME_SUBBANDS; b++) {
                aqL.qoff[b] = AVG(aqL.qoff[b], aqR.qoff[b]);
            }
            /* don't combine transform_type for MS */
            break;
        default:
            DSA_ERROR(("invalid mode!"));
            DSA_ASSERT(0);
            return 0;
    }
    memcpy(aq, &aqL, sizeof(*aq));
    frame->meta.transform_type = aq->transform_type;
    if (frame->meta.transform_type != ANALYSIS_TRANSFORM_TYPE) {
        /* recompute with correct transform type */
        dsa_fwd_sbt(enc->fullframe, nchan, enc->tempcoefs, frame->meta.transform_type);
    }
    *out_coefs = enc->tempcoefs;
    return 1;
}

static int
pack_frame(DSA_ENCODER *enc,
           DSA_FRAME *frame, DSA_SAMPLE *coefs,
           DSA_AQ *aq, uint16_t explicit_quant, int bitrate)
{
    int actual_bitrate;
    int target_bitrate;
    int iter = 0;
    uint32_t size = 0;
    int lo, hi, tolerance, best_q, samp_rate = freq_for_rate(frame->meta.rate);
    int nchan = dsa_channels_for_mode(frame->meta.mode);

    target_bitrate = bitrate;
    actual_bitrate = target_bitrate;

    lo = DSA_MIN_QUANT * DSA_QUANT_STEP;
    hi = 4096 * DSA_MAX_BITRATE / bitrate;
    hi = MIN(hi, DSA_MAX_QUANT);
    best_q = hi;
    tolerance = target_bitrate / (4 + (enc->effort / 4));
    while (lo <= hi) {
        int bits_for_block, block_duration_ms;
        int mid;
        if (explicit_quant) {
            mid = explicit_quant;
        } else {
            mid = lo + ((hi - lo + 1) / 2);
        }
        mid &= ~(DSA_QUANT_STEP - 1);
        memset(enc->encodebuf, 0, enc->encodebufsz);
        size = dsa_encode(coefs, enc->encodebuf, nchan, aq, mid);
        best_q = mid;

        /* based on what gets encoded in encode_frame() */
#define FRAME_OVERHEAD 9
        bits_for_block = 8 * (DSA_PACKET_HDR_SIZE + FRAME_OVERHEAD + size);
        block_duration_ms = ((DSA_FRAME_SAMPLES - (frame->meta.overlapped ? DSA_FRAME_OVERLAP : 0)) * 1000) / (samp_rate * nchan);

        actual_bitrate = bits_for_block / block_duration_ms; /* kbps */

        iter++;

        if (explicit_quant || iter > 16) {
            break;
        }
        if (actual_bitrate > target_bitrate + tolerance) {
            lo = mid + DSA_QUANT_STEP;
            lo = CLAMP(lo, DSA_MIN_QUANT, DSA_MAX_QUANT);
        } else if (actual_bitrate < target_bitrate - tolerance) {
            hi = mid - DSA_QUANT_STEP;
            hi = CLAMP(hi, DSA_MIN_QUANT, DSA_MAX_QUANT);
        } else {
            break;
        }
    }
    DSA_DEBUG(("converged to bitrate [%d] after %d iters with quant %d", actual_bitrate, iter, best_q));
    frame->quant = best_q / DSA_QUANT_STEP;
    DSA_ASSERT(size < DSA_MAX_QUANT);
    frame->enc_size = size;
    frame->data = enc->encodebuf;
    return actual_bitrate;
}

extern int
dsa_enc(DSA_ENCODER *enc, DSA_SAMPLE *pcm, uint32_t nsamp, DSA_BUF *bufs)
{
    DSA_FNUM i;
    int nbuf = 0, nchan;
    DSA_FRAME frame;
    DSA_SAMPLE *coefs = NULL;
    DSA_AQ aq = { 0 };

    if (pcm == NULL) {
        DSA_ERROR(("null or empty (nsamp == 0) pcm passed to encoder!"));
        return 0;
    }
    if (bufs == NULL) {
        DSA_ERROR(("null buffer list passed to encoder!"));
        return 0;
    }
    if (enc->force_id) {
        encode_id(&bufs[nbuf++]);
        set_link_offsets(enc, &bufs[nbuf - 1], 0);
        enc->force_id = 0;
    }
    if (enc->force_tags) {
        encode_tags(&bufs[nbuf++], &enc->tags);
        set_link_offsets(enc, &bufs[nbuf - 1], 0);
        enc->force_tags = 0;
    }
    i = enc->next_fnum++;

    if (enc->explicit_quant == 0) {
        enc->target_bitrate_kbps = rc_get_budget(enc);
    }
    enc->target_bitrate_kbps = CLAMP(enc->target_bitrate_kbps, DSA_MIN_BITRATE, DSA_MAX_BITRATE);
    if (nsamp > DSA_FRAME_SAMPLES) {
        DSA_ERROR(("PCM length too long! expected max of %u, got %u instead", DSA_FRAME_SAMPLES, nsamp));
        DSA_ASSERT(0);
        return 0;
    }

    if (enc->num_channels == 1) {
        frame.meta.mode = DSA_MODE_MONO;
    } else {
        if ((enc->do_psy & (DSA_PSY_ENABLE_LR_STEREO | DSA_PSY_ENABLE_MS_STEREO)) == (DSA_PSY_ENABLE_LR_STEREO | DSA_PSY_ENABLE_MS_STEREO)) {
            frame.meta.mode = use_mid_side(enc, pcm, nsamp, enc->effort) ? DSA_MODE_MS_STEREO : DSA_MODE_LR_STEREO;
        } else {
            frame.meta.mode = ((enc->do_psy & DSA_PSY_ENABLE_MS_STEREO) ? DSA_MODE_MS_STEREO : DSA_MODE_LR_STEREO);
        }
    }
    frame.meta.rate = (enc->sample_rate == 48000) ? 3 : dsa_lb2(enc->sample_rate / 11025);
    frame.meta.depth = (enc->bits_per_samp == 16);
    frame.meta.overlapped = enc->overlapped;
    frame.meta.reserved = 0;
    frame.actual_pcm_size = nsamp;

    memcpy(enc->fullframe, pcm, nsamp * sizeof(*enc->fullframe));
    /* fill rest with zero */
    if (nsamp < DSA_FRAME_SAMPLES) {
        memset(enc->fullframe + nsamp, 0, (DSA_FRAME_SAMPLES - nsamp) * sizeof(*enc->fullframe));
    }
    nchan = dsa_channels_for_mode(frame.meta.mode);
    split(pcm, nsamp, nchan, enc->fullframe + 0 * (DSA_FRAME_SAMPLES / nchan),
                             enc->fullframe + 1 * (DSA_FRAME_SAMPLES / nchan));
    if (frame.meta.mode == DSA_MODE_MS_STEREO) {
        dsa_jms_fwd_transform(enc->fullframe, DSA_FRAME_SAMPLES);
        enc->stats.msnum++;
    } else {
        enc->stats.lrnum++;
    }

    if (!analyze_frame(enc, &frame, &coefs, &aq, enc->target_bitrate_kbps, enc->do_psy)) {
        DSA_ERROR(("failed to analyze frame!"));
        DSA_ASSERT(0);
        return 0;
    }
    enc->stats.tnum[frame.meta.transform_type]++;
    enc->frame_bitrate = pack_frame(enc, &frame, coefs, &aq, enc->explicit_quant, enc->target_bitrate_kbps);
    enc->frame_quant = frame.quant;
    enc->n_quants = MIN(enc->n_quants + 1, 32);
    enc->avgquant += ((int) enc->frame_quant - enc->avgquant) / enc->n_quants;

    if (!encode_frame(&bufs[nbuf++], &frame, i)) {
        DSA_ERROR(("failed to encode frame!"));
        DSA_ASSERT(0);
        return 0;
    }

    set_link_offsets(enc, &bufs[nbuf - 1], 0);

    rc_update_kbps(enc, enc->frame_bitrate);
    return nbuf;
}
