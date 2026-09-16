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

#include "dsa_decoder.h"
#include "dsa_internal.h"

static int
decode_packet_hdr(DSA_BS *bs)
{
    int pkt_type;

    pkt_type = dsa_bs_get_bits(bs, 8);
    DSA_DEBUG(("packet type %02x", pkt_type));
    /* link offsets */
    dsa_bs_get_bits(bs, 24);
    dsa_bs_get_bits(bs, 24);

    return pkt_type;
}

static void
bs_get_string(DSA_BS *bs, uint16_t *len, char **str)
{
    unsigned i;
    unsigned rlen;
    char *rstr;

    rlen = dsa_bs_get_bits(bs, 16);
    rstr = dsa_alloc(rlen + 1);
    for (i = 0; i < rlen; i++) {
        rstr[i] = dsa_bs_get_bits(bs, 8);
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

    dsa_bs_align(bs);
    bs_get_string(bs, &tags->track_sz, &tags->track);
    bs_get_string(bs, &tags->artist_sz, &tags->artist);
    bs_get_string(bs, &tags->album_sz, &tags->album);
    bs_get_string(bs, &tags->comment_sz, &tags->comment);

    tags->year = dsa_bs_get_bits(bs, 16);
    tags->track_num = dsa_bs_get_bits(bs, 8);
    tags->total_tracks = dsa_bs_get_bits(bs, 8);
    tags->genre = dsa_bs_get_bits(bs, 8);
    dsa_bs_align(bs);
    return 1;
}

static int
decode_id(DSA_BS *bs)
{
    int c0, c1, c2, c3;
    int ver_min;

    dsa_bs_align(bs);
    c0 = dsa_bs_get_bits(bs, 8);
    c1 = dsa_bs_get_bits(bs, 8);
    c2 = dsa_bs_get_bits(bs, 8);
    c3 = dsa_bs_get_bits(bs, 8);
    if (c0 != DSA_FOURCC_0 || c1 != DSA_FOURCC_1 || c2 != DSA_FOURCC_2 || c3 != DSA_FOURCC_3) {
        DSA_ERROR(("bad 4cc (%c %c %c %c)\n", c0, c1, c2, c3));
        return 0;
    }
    ver_min = dsa_bs_get_bits(bs, 8);
    DSA_DEBUG(("version 1.%d", ver_min));
    dsa_bs_align(bs);
    return 1;
}

static void
decode_frame(DSA_FRAME *frame, DSA_BS *bs, DSA_FNUM *fn)
{
    dsa_bs_align(bs);
    *fn = dsa_bs_get_bits(bs, 32);
    frame->enc_size = dsa_bs_get_bits(bs, 16);
    frame->quant = dsa_bs_get_bits(bs, 14) * DSA_QUANT_STEP;
    frame->meta.mode = dsa_bs_get_bits(bs, 2);
    frame->meta.rate = dsa_bs_get_bits(bs, 2);
    frame->meta.depth = dsa_bs_get_bit(bs);
    frame->meta.overlapped = dsa_bs_get_bit(bs);
    frame->meta.transform_type = dsa_bs_get_bit(bs);
    if (dsa_bs_get_bit(bs)) {
        frame->meta.reserved = dsa_bs_get_bits(bs, 15);
    } else {
        frame->meta.reserved = 0;
    }
    if (dsa_bs_get_bit(bs)) {
        frame->actual_pcm_size = dsa_bs_get_bits(bs, 11);
    } else {
        frame->actual_pcm_size = DSA_FRAME_SAMPLES;
    }
    dsa_bs_align(bs);
    frame->data = bs->start + dsa_bs_ptr(bs);
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
dsa_dec_init(DSA_DECODER *d)
{
    memset(d, 0, sizeof(*d));
    d->first = 1;
    d->alloc = dsa_alloc((1 + 2) * DSA_FRAME_SAMPLES * sizeof(*d->alloc));
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
dsa_dec_free(DSA_DECODER *d)
{
    DSA_FREE_AND_NULL(d->alloc);
    DSA_FREE_AND_NULL(d->tags.track);
    DSA_FREE_AND_NULL(d->tags.artist);
    DSA_FREE_AND_NULL(d->tags.album);
    DSA_FREE_AND_NULL(d->tags.comment);
}

extern void
dsa_dec_set_outbuf(DSA_DECODER *d, DSA_SAMPLE *outbuf)
{
    if (outbuf) {
        d->output_buf = outbuf;
    } else {
        d->output_buf = d->alloc + 0 * DSA_FRAME_SAMPLES;
    }
}

extern DSA_TAGS *
dsa_dec_get_tags(DSA_DECODER *d)
{
    DSA_TAGS *tags;

    tags = dsa_alloc(sizeof(*tags));
    memcpy(tags, &d->tags, sizeof(*tags));
    /* copy strings over */
    tags->track = dsa_alloc(tags->track_sz);
    memcpy(tags->track, d->tags.track, tags->track_sz);

    tags->artist = dsa_alloc(tags->artist_sz);
    memcpy(tags->artist, d->tags.artist, tags->artist_sz);

    tags->album = dsa_alloc(tags->album_sz);
    memcpy(tags->album, d->tags.album, tags->album_sz);

    tags->comment = dsa_alloc(tags->comment_sz);
    memcpy(tags->comment, d->tags.comment, tags->comment_sz);
    return tags;
}

extern int
dsa_dec(DSA_DECODER *d, DSA_BUF *buffer, DSA_PCM *out, DSA_FNUM *fn)
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
    *fn = ~(DSA_FNUM) 0;

    dsa_bs_init(&bs, buffer->data);
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

        merge(d->hist[0].L, d->hist[0].R, out->len, dsa_channels_for_mode(out->meta.mode), out->samples);

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

    channels = dsa_channels_for_mode(frame.meta.mode);
    /* setup right channel pointers */
    if (channels == 2) {
        d->hist[0].R = d->hist[0].L + (DSA_FRAME_SAMPLES / 2);
        d->hist[1].R = d->hist[1].L + (DSA_FRAME_SAMPLES / 2);
    }
    memset(d->output_buf, 0, DSA_FRAME_SAMPLES * sizeof(*d->output_buf));
    if (!dsa_decode(frame.data, d->output_buf, frame.enc_size, channels, frame.quant)) {
        ret = DSA_DEC_ERROR;
        goto cleanup;
    }

    dsa_inv_sbt(d->output_buf, channels, d->hist[1].LR, frame.meta.transform_type);

    if (frame.meta.mode == DSA_MODE_MS_STEREO) {
        dsa_jms_inv_transform(d->hist[1].LR, DSA_FRAME_SAMPLES);
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
    dsa_buf_free(buffer);
    return ret;
}
