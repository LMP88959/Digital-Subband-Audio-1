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
#include "dsa_decoder.h"
#include "tagfile.h"
#include "wav_utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define DRV_HEADER "Envel Graphics DSA v1.%d codec by EMMIR 2025-2026. "\
                   "encoder v%d. "  \
                   "decoder v%d.\n", \
                    DSA_VERSION_MINOR, \
                    DSA_ENCODER_VERSION, DSA_DECODER_VERSION

static int encoding = 0;
static char *progname = NULL;
static int dooverwrite = 1;
static int verbose = 0;

#define AUTO_BITRATE 0

#define USE_STDIO_CHAR '-'

struct PARAM {
   char *prefix;
   int value;
   int min, max;
   int (*convert)(int);
   char *desc;
   char *extra;
};

static struct PARAM enc_params[] = {
    { "kbps", AUTO_BITRATE, AUTO_BITRATE, DSA_MAX_BITRATE, NULL,
            "ONLY FOR ABR RATE CONTROL: bitrate in kilobits per second. 0 = use CQP if quant is specified, or will default to a bitrate that is considered to provide 'good' quality. 0 = default",
            ""},
    { "quant", 0, 0, DSA_MAX_QUANT, NULL,
            "quantization parameter (ONLY USED IN CQP RATE CONTROL MODE), larger value = worse quality + smaller file, smaller value = better quality + larger file. 0 = auto. 0 = default",
            "it's generally better to do ABR but CQP can be interesting to play around with as well"},
    { "effort", DSA_MAX_EFFORT, DSA_MIN_EFFORT, DSA_MAX_EFFORT, NULL,
            "encoder effort. 0 = least effort, 10 = most effort. higher value -> better audio, slower encoding. default = 10",
            "does not change decoding speed"},
    { "chan", 0, 0, 2, NULL,
            "determines both number of channels (if raw PCM) and/or encoding mode. 0 = auto, 1 = mono, 2 = stereo. 0 = default",
            "best to keep it auto unless input is raw PCM or you're absolutely sure you need a certain number of channels"},
    { "freq", 0, 0, 48000, NULL,
            "sampling frequency in Hz, must be either 11025, 22050, 44100, or 48000. 0 = auto (only if input is not raw PCM). 0 = default",
            "best to keep it auto unless input is raw PCM or you're absolutely sure you need a certain sample rate"},
    { "bitd", 0, 8, 16, NULL,
            "bit depth of each sample, 0 = auto, 8 = unsigned 8-bit audio, 16 = signed 16-bit audio. 0 = default ",
            "best to keep it auto unless input is raw PCM or you're absolutely sure you need a certain bit depth"},
    { "nfr", -1, -1, INT_MAX, NULL,
            "number of frames to compress. -1 means as many as possible. -1 = default",
            "also used by the encoder to avoid adding an intra frame near the end"},
    { "noeos", 0, 0, 1, NULL,
            "do not write EOS packet at the end of the compressed stream. 0 = default",
            "useful for multithreaded encoding via concatenation"},
    { "rc_mode", DSA_RATE_CONTROL_ABR, DSA_RATE_CONTROL_ABR, DSA_RATE_CONTROL_CQP, NULL,
            "rate control mode. 0 = average bitrate (ABR), 1 = constant quantization parameter (CQP). 0 = default",
            "ABR is recommended for hitting a target file size"},
    { "psy", DSA_PSY_ALL, 0, DSA_PSY_ALL, NULL,
           "enable/disable psychoacoustic optimizations. 255 = default",
           "can hurt or help depending on content. can be beneficial to try different combinations and see which is better.\n"
           "\t\tcurrently defined bits (bit OR together to get multiple at the same time):\n"
           "\t\t1 = adaptive quantization\n"
           "\t\t2 = enable LR stereo\n"
           "\t\t4 = enable MS stereo\n"
           "\t\tNOTE: if LR and MS are both enabled, the encoder will decide which is best for each frame. If both are disabled, only LR will be used.\n"
            },
    { "wav", 1, 0, 1, NULL,
            "set to 1 if input is in WAVE (WAV) format, 0 if raw PCM. 1 = default",
            "not all metadata will be passed through, WAV parser is not a complete parser and some inputs could result in error"},
    { "overlap", 1, 0, 1, NULL,
            "enable/disable frame overlapping to reduce boundary artifacts. 1 = enable, 0 = disable. 1 = default",
            "more important at lower bitrates, recommended to keep enabled (especially under 256kbps)"},
    { NULL, 0, 0, 0, NULL, "", "" }
};

static struct PARAM dec_params[] = {
    /* decoder has no params currently */
    { NULL, 0, 0, 0, NULL, "", "" }
};

static struct {
    char *inp; /* input file path */
    char *out; /* output file path */

    char *tagfile; /* for encoding, the file containing the tags */
} opts;

static int
get_optval(struct PARAM *pars, char *name)
{
    int i;
    for (i = 0; pars[i].prefix != NULL; i++) {
        struct PARAM *par = &pars[i];
        if (strcmp(par->prefix, name) == 0) {
            return par->value;
        }
    }
    return 0;
}
static void
usage_general(void)
{
    char *p = progname;

    printf(DRV_HEADER);
    printf("usage: %s <e|d> [options]\n", p);
    printf("for more information about running the encoder: %s e help\n", p);
    printf("for more information about running the decoder: %s d help\n", p);
    printf("for verbose information about encoder parameters: %s e vhelp\n", p);
    printf("for verbose information about decoder parameters: %s d vhelp\n", p);
}

static void
print_params(struct PARAM *pars, int extra)
{
    int i;

    printf("------------------------------------------------------------\n");
    for (i = 0; pars[i].prefix != NULL; i++) {
        struct PARAM *par = &pars[i];

        printf("\t-%s : %s\n", par->prefix, par->desc);
        printf("\t      [min = %d, max = %d]\n", par->min, par->max);
        if (extra && par->extra) {
            printf("\textra info: %s\n\n", par->extra);
        }
    }
    printf("\t-inp= : input file. NOTE: if not specified, defaults to stdin\n");
    printf("\t-out= : output file. NOTE: if not specified, defaults to stdout\n");
    printf("\t-y : do not prompt for confirmation when potentially overwriting an existing file\n");
    printf("\t-l<n> : set logging level to n (0 = none, 1 = error, 2 = warning, 3 = info, 4 = debug/all)\n");
    printf("\t-v : set verbose\n");
}

static void
usage_encoder(int extra)
{
    char *p = progname;

    printf(DRV_HEADER);
    printf("usage: %s e [options]\n", p);
    printf("sample usage: %s e -v -kbps=128 -wav=1 -tagfile=tags.txt -overlap=1 -effort=10 -inp=input.wav -out=out.dsa\n", p);
    printf("\t-tagfile= : text file containing tag info to be embedded in the encoded stream\n");
    print_params(enc_params, extra);
}

static void
usage_decoder(int extra)
{
    char *p = progname;

    printf(DRV_HEADER);
    printf("usage: %s d [options]\n", p);
    printf("sample usage: %s d -v -inp=out.dsa -out=recon.wav\n", p);
    print_params(dec_params, extra);
}

static void
usage(int extra)
{
    if (encoding) {
        usage_encoder(extra);
    } else {
        usage_decoder(extra);
    }
}

static int
stoint(char *s, int *err)
{
    char *tail;
    long val;

    errno = 0;
    *err = 0;
    val = strtol(s, &tail, 10);
    if (errno == ERANGE) {
        printf("integer out of integer range\n");
        *err = 1;
    } else if (errno != 0) {
        printf("bad string: %s\n", strerror(errno));
        *err = 1;
    } else if (*tail != '\0') {
        printf("integer contained non-numeric characters\n");
        *err = 1;
    }
    return val;
}

static int
fileexist(char *n)
{
    FILE *fp = fopen(n, "r");
    if (fp) {
        fclose(fp);
        return 1;
    }
    return 0;
}

static int
promptoverwrite(char *fn)
{
    if (dooverwrite && fileexist(fn)) {
        do {
            char c = 0;
            printf("\n--- file (%s) already exists, overwrite? (y/n)\n", fn);
            scanf(" %c", &c);
            if (c == 'y' || c == 'Y') {
                return 1;
            }
            if (c == 'n' || c == 'N') {
                return 0;
            }
        } while (1);
    }
    return 1;
}

static int
prefixcmp(char *pref, char **s)
{
    int plen = strlen(pref);
    if (!strncmp(pref, *s, plen)) {
        *s += plen;
        return 1;
    }
    return 0;
}

static int
get_param(char *argv)
{
    int i;
    char *p = argv;
    int err = 0;
    struct PARAM *params;

    if (strcmp("vhelp", p) == 0) {
        return -1;
    }
    if (strcmp("help", p) == 0) {
        return 0;
    }
    if (*p != '-') {
        printf("strange argument: %s\n", p);
        return 0;
    }

    p++;
    if (strcmp("v", p) == 0) {
        verbose = 1;
        return 1;
    }
    if (strcmp("y", p) == 0) {
        dooverwrite = 0;
        return 1;
    }
    if (prefixcmp("l", &p)) {
        int lvl = stoint(p, &err);
        if (err) {
            printf("error reading argument: l\n");
            return 0;
        }
        lvl = CLAMP(lvl, 0, 4);
        dsa_set_log_level(lvl);
        return 1;
    }
    if (prefixcmp("inp=", &p)) {
        opts.inp = p;
        return 1;
    }
    if (prefixcmp("out=", &p)) {
        opts.out = p;
        return 1;
    }

    if (encoding) {
        if (prefixcmp("tagfile=", &p)) {
            opts.tagfile = p;
            return 1;
        }
        params = enc_params;
    } else {
        params = dec_params;
    }
    for (i = 0; params[i].prefix != NULL; i++) {
        struct PARAM *par = &params[i];
        char buf[512];
        snprintf(buf, sizeof(buf) - 1, "%s=", par->prefix);
        if (!prefixcmp(buf, &p)) {
            continue;
        }
        par->value = stoint(p, &err);
        par->value = CLAMP(par->value, par->min, par->max);
        if (par->convert) {
            par->value = par->convert(par->value);
        }
        if (err) {
            printf("error reading argument: %s\n", par->prefix);
            return 0;
        }
        return 1;
    }
    printf("unrecognized argument(s)\n");
    return 0;
}

static int
init_params(int argc, char **argv)
{
    int i;

    if (argc == 1) {
        printf("not enough args!\n");
        usage(0);
        return 0;
    }
    argc--;
    argv++;
    while (argc > 0) {
        i = get_param(*argv);
        if (i == 0) {
            usage(0);
            return 0;
        }
        if (i == -1) {
            usage(1);
            return 0;
        }
        argv += i;
        argc -= i;
    }
    return 1;
}

static uint8_t *enc_buf = NULL;
static unsigned bufsz = 0;

static uint8_t *
mrealloc(uint8_t *p, unsigned sz)
{
    if (p == NULL) {
        return malloc(sz);
    }
    return realloc(p, sz);
}

static void
savebuffer(void *data, uint32_t len)
{
    enc_buf = mrealloc(enc_buf, bufsz + len);
    memcpy(enc_buf + bufsz, data, len);
    bufsz += len;
}

static int
writefile(char *n, unsigned char *buf, long len)
{
    FILE *fp;

    if (n[0] == USE_STDIO_CHAR) {
        fp = stdout;
    } else {
        fp = fopen(n, "wb");
        if (fp == NULL) {
            perror("unable to open file");
            return 0;
        }
    }
    if (fwrite(buf, 1, len, fp) != (unsigned) len) {
        perror("unable to write file");
        goto err;
    }

    if (n[0] != USE_STDIO_CHAR) {
        fclose(fp);
    }
    return 1;
err:
    if (n[0] != USE_STDIO_CHAR) {
        fclose(fp);
    }
    return 0;
}

/* scales unsigned 8-bit audio to signed 16-bit audio.
 * this will need to be undone after decoding for a 1:1 comparison
 */
static int
read_frame_as_s16(int channels, int bits, uint8_t *bytes, int num_bytes, DSA_SAMPLE *samples)
{
    uint8_t *end = bytes + num_bytes;
    while (bytes < end) {
        if (channels == 2) {
            switch (bits) {
                case 8:
                    samples[0] = ((int) bytes[0]) * 257 - 32768;
                    samples[1] = ((int) bytes[1]) * 257 - 32768;
                    bytes += 2;
                    break;
                case 16:
                    samples[0] = (int16_t) (bytes[0] | (bytes[1] << 8));
                    bytes += 2;
                    samples[1] = (int16_t) (bytes[0] | (bytes[1] << 8));
                    bytes += 2;
                    break;
                default:
                    DSA_ASSERT(0);
                    return 0;
            }
            samples += 2;
        } else {
            switch (bits) {
                case 8:
                    samples[0] = ((int) bytes[0]) * 257 - 32768;
                    bytes++;
                    break;
                case 16:
                    samples[0] = (int16_t) (bytes[0] | (bytes[1] << 8));
                    bytes += 2;
                    break;
                default:
                    DSA_ASSERT(0);
                    return 0;
            }
            samples++;
        }
    }
    return 1;
}

static void
print_bins(DSA_ENCODER *enc)
{
    int i, total = 0;
    int wsum = 0;
    int nempty = 0;
    int *hist = enc->psy.quant_hist;

    for (i = 0; i < DSA_N_BINS; i++) {
        nempty += (hist[i] == 0);
        total += hist[i];
        wsum += i * hist[i];
    }
    for (i = 0; i < DSA_N_BINS; i++) {
        printf("bin %d = %d (%d%%)\n", i, hist[i], hist[i] * 100 / total);
    }
    if (total > 0) {
        printf("w-avg bin: %d, empty bins: %d/%d\n", wsum / total, nempty, DSA_N_BINS);
    }
}

static void
print_psystats(DSA_ENCODER *enc)
{
    int i, j;

    for (i = 0; i < enc->psy.hist[16]; i++) {
        printf("HIST %2d:\tavg= %2d", i, enc->psy.hist[i] / enc->psy.hist[17]);
        printf("\tIMPs ");
        for (j = 0; j < 8; j++) {
            printf("%c:avg= %4d ", "Ifoscmgd"[j], enc->psy.histsb[i][j] / enc->psy.hist[17]);
        }
        printf("\n");
    }
    print_bins(enc);
}

static void
free_tags(DSA_TAGS *tags)
{
    dsa_free(tags->track);
    dsa_free(tags->artist);
    dsa_free(tags->album);
    dsa_free(tags->comment);
}

static int
encode(void)
{
    DSA_BUF bufs[8];
    DSA_ENCODER enc;
    int i, run;
    int maxframe;
    unsigned frno = 0;
    int nfr;
    int write_eos = 1;
    int no_more_data = 0;
    int bytes_per_samp;
    unsigned min_br = UINT_MAX, max_br = 0;
    unsigned min_q = UINT_MAX, max_q = 0;
    unsigned total_br = 0, total_q = 0;
    int state;
    FILE *inpfile;
    int bitrate;
    uint8_t *rawbytes;
    DSA_WAV_DATA wavdat;
    int is_wav = 0;
    int frame_stride;
    DSA_SAMPLE *samples;
    int first_block = 1;
    size_t totalb = 0;
    size_t overlap_bytes;

    if (verbose) {
        printf(DRV_HEADER);
        printf("\n");
    }

    dsa_enc_init(&enc);

    if (opts.inp[0] == USE_STDIO_CHAR) {
        inpfile = stdin;
        if (verbose) {
            printf("reading from stdin\n");
        }
    } else {
        inpfile = fopen(opts.inp, "rb");
        if (inpfile == NULL) {
            printf("error opening input file %s\n", opts.inp);
            return EXIT_FAILURE;
        }
    }

    if (opts.tagfile) {
        DSA_TAGS tags;
        FILE *tagf = fopen(opts.tagfile, "rb");
        if (tagf == NULL) {
            printf("error opening tag file %s\n", opts.tagfile);
            return EXIT_FAILURE;
        }
        memset(&tags, 0, sizeof(tags));
        if (tagfile_read(&tags, tagf)) {
            if (verbose) {
                printf("successfully read tag file '%s':\n", opts.tagfile);
                printf("track: %s\n", tags.track);
                printf("artist: %s\n", tags.artist);
                printf("album: %s\n", tags.album);
                printf("comment: %s\n", tags.comment);
                printf("year: %d\n", tags.year);
                printf("track_num: %d\n", tags.track_num);
                printf("total_tracks: %d\n", tags.total_tracks);
                printf("genre: %d\n", tags.genre);
            }
            dsa_enc_set_tags(&enc, &tags);
            free_tags(&tags);
        } else {
            DSA_ERROR(("failed to read tag file [%s]", opts.tagfile));
        }
        fclose(tagf);
    }

    is_wav = get_optval(enc_params, "wav");
    if (is_wav) {
        if (!wav_read(&wavdat, inpfile)) {
            return EXIT_FAILURE;
        }
    }

    bitrate = get_optval(enc_params, "kbps");
    enc.overlapped = get_optval(enc_params, "overlap");
    frame_stride = DSA_FRAME_SAMPLES;
    if (enc.overlapped) {
        frame_stride -= DSA_FRAME_OVERLAP;
    }
    enc.effort = get_optval(enc_params, "effort");
    enc.num_channels = get_optval(enc_params, "chan");
    enc.sample_rate = get_optval(enc_params, "freq");
    enc.bits_per_samp = get_optval(enc_params, "bitd");
    enc.do_psy = get_optval(enc_params, "psy");

    enc.rc_mode = get_optval(enc_params, "rc_mode");
    enc.explicit_quant = get_optval(enc_params, "quant");
    if (bitrate == AUTO_BITRATE) {
        /* if no bitrate was specified but a quant was specified, then set to CQP mode */
        if (enc.explicit_quant != 0) {
            enc.rc_mode = DSA_RATE_CONTROL_CQP;
        } else {
            bitrate = 256; /* default */
            enc.rc_mode = DSA_RATE_CONTROL_ABR;
        }
    }
    if (enc.rc_mode == DSA_RATE_CONTROL_ABR) {
        enc.explicit_quant = 0;
    }
    if (enc.rc_mode == DSA_RATE_CONTROL_CQP && enc.explicit_quant == 0) {
        DSA_ERROR(("a quant of zero (0) is not a valid quant for CQP mode"));
        return EXIT_FAILURE;
    }
    enc.target_bitrate_kbps = bitrate;
    enc.block_size = frame_stride;
    if (is_wav) {
        enc.sample_rate = (enc.sample_rate == 0 ? wavdat.freq : enc.sample_rate);
        enc.num_channels = (enc.num_channels == 0 ? wavdat.channels : enc.num_channels);
        enc.bits_per_samp = (enc.bits_per_samp == 0 ? wavdat.bits : enc.bits_per_samp);
    } else {
        if (enc.num_channels == 0) {
            DSA_ERROR(("DSA1 does not support 'auto' for parameter 'chan' when input is raw PCM"));
            return EXIT_FAILURE;
        }
        if (enc.sample_rate == 0) {
            DSA_ERROR(("DSA1 does not support 'auto' for parameter 'freq' when input is raw PCM"));
            return EXIT_FAILURE;
        }
    }
    switch (enc.sample_rate) {
        case 11025:
        case 22050:
        case 44100:
        case 48000:
            break;
        default:
            DSA_ERROR(("DSA1 only supports 11025Hz, 22050Hz, 44100Hz, and 48000Hz audio. got: %d", enc.sample_rate));
            return EXIT_FAILURE;
    }

    if (enc.bits_per_samp != 8 && enc.bits_per_samp != 16) {
        DSA_ERROR(("DSA1 only supports 8 and 16 bit-per-sample audio. got: %d", enc.bits_per_samp));
        return EXIT_FAILURE;
    }
    frno = 0;
    nfr = get_optval(enc_params, "nfr");
    write_eos = !get_optval(enc_params, "noeos");
    if (nfr > 0) {
        maxframe = frno + nfr;
    } else {
        maxframe = -1;
    }

    DSA_INFO(("starting encoder"));
    dsa_enc_start(&enc);
    run = 1;

    samples = dsa_alloc(DSA_FRAME_SAMPLES * sizeof(DSA_SAMPLE));

    bytes_per_samp = (enc.bits_per_samp / 8);

    rawbytes = dsa_alloc(DSA_FRAME_SAMPLES * bytes_per_samp);

    overlap_bytes = (enc.overlapped ? DSA_FRAME_OVERLAP : 0) * bytes_per_samp;
    while (run) {
        size_t nread, nwant, offset;
        size_t remaining_bytes = INT32_MAX;
        uint32_t nsamp;
        if (maxframe > 0 && frno >= (unsigned) maxframe) {
            goto end_of_stream;
        }
        if (is_wav) {
            remaining_bytes = (wavdat.expected_bytes > totalb) ? (wavdat.expected_bytes - totalb) : 0;
        }
        if (first_block) {
            nwant = DSA_FRAME_SAMPLES * bytes_per_samp;
            offset = 0;
        } else {
            nwant = frame_stride * bytes_per_samp;
            memmove(rawbytes, rawbytes + nwant, overlap_bytes);
            offset = overlap_bytes;
        }
        nwant = MIN(nwant, remaining_bytes);
        nread = fread(rawbytes + offset, 1, nwant, inpfile);
        totalb += nread;

        nsamp = (offset + nread) / bytes_per_samp;

        if (nread == 0) {
            DSA_INFO(("reached end of stream"));
            no_more_data = 1;
            goto end_of_stream;
        } else if (nread != nwant) {
            long int pos = ftell(inpfile);
            if (pos < 0) {
                DSA_ERROR(("stream error"));
                no_more_data = 1;
                goto end_of_stream;
            }
        }

        read_frame_as_s16(enc.num_channels, enc.bits_per_samp, rawbytes, nsamp * bytes_per_samp, samples);
        state = dsa_enc(&enc, samples, nsamp, bufs);

        first_block = 0;
        min_br = MIN(min_br, enc.frame_bitrate);
        max_br = MAX(max_br, enc.frame_bitrate);
        total_br += enc.frame_bitrate;
        total_q += enc.frame_quant;
        min_q = MIN(min_q, enc.frame_quant);
        max_q = MAX(max_q, enc.frame_quant);
        run = !(state & DSA_ENC_FINISHED);
        state &= DSA_ENC_NUM_BUFS;
        if (verbose && state) {
            int tbytes = 0, numbufs = state;
            while (numbufs-- > 0) {
                tbytes += bufs[numbufs].len;
            }
            printf("\rencoded frame %d to %d bytes", frno, tbytes);
            fflush(stdout);
        }
        for (i = 0; i < state; i++) {
            savebuffer(bufs[i].data, bufs[i].len);
            dsa_buf_free(&bufs[i]);
        }
        frno++;
        continue;
end_of_stream:
        if (write_eos || (!write_eos && no_more_data && bufsz > 0)) {
            dsa_enc_end_of_stream(&enc, bufs);
            savebuffer(bufs[0].data, bufs[0].len);
            dsa_buf_free(&bufs[0]);
        }
        break;
    }
    if (verbose) {
        printf("\nencoded %d frames\n", frno);
        printf("[min, avg, max] quant = [%d, %d %d]\n", min_q, total_q / frno, max_q);
        printf("[min, avg, max] bitrate = [%d, %d %d] kbps\n", min_br, total_br / frno, max_br);
        {
            unsigned t;
            t = enc.stats.msnum + enc.stats.lrnum;
            if (t > 0) {
                printf("MS%% LR%% = [%d%% %d%%]\n", (enc.stats.msnum * 100 + t / 2) / t, (enc.stats.lrnum * 100 + t / 2) / t);
            }
            t = enc.stats.tnum[0] + enc.stats.tnum[1];
            if (t > 0) {
                printf("T0%% T1%% = [%d%% %d%%]\n",
                        (enc.stats.tnum[0] * 100 + t / 2) / t, (enc.stats.tnum[1] * 100 + t / 2) / t);
            }
        }
        printf("file size = %u bytes\n", bufsz);
        print_psystats(&enc);
    }
    writefile(opts.out, enc_buf, bufsz);
    if (verbose) {
        printf("saved audio file\n");
    }
    dsa_enc_free(&enc);
    dsa_free(rawbytes);
    dsa_free(samples);

    free(enc_buf);
    if (opts.inp[0] != USE_STDIO_CHAR) {
        fclose(inpfile);
    }
    return no_more_data ? -2 : EXIT_SUCCESS;
}

#define DSA_PKT_ERR_EOF -1
#define DSA_PKT_ERR_OOB -2 /* out of bytes */
#define DSA_PKT_ERR_PSZ -3 /* bad packet size */
#define DSA_PKT_ERR_4CC -4 /* bad 4cc */

static int
read_packet(FILE *f, DSA_BUF *rb, int *packet_type)
{
    int n, size;
    uint8_t hdr[DSA_PACKET_HDR_SIZE];

    n = fread(hdr, 1, DSA_PACKET_HDR_SIZE, f);
    if (n == 0) {
        DSA_ERROR(("no data"));
        return DSA_PKT_ERR_EOF;
    }
    if (n < DSA_PACKET_HDR_SIZE) {
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
    dsa_mk_buf(rb, size);
    memcpy(rb->data, hdr, DSA_PACKET_HDR_SIZE);
    n = fread(rb->data + DSA_PACKET_HDR_SIZE, 1, size - DSA_PACKET_HDR_SIZE, f);
    if (n < size - DSA_PACKET_HDR_SIZE) {
        DSA_ERROR(("did not read enough data: %d", size - DSA_PACKET_HDR_SIZE));
        dsa_buf_free(rb);
        return DSA_PKT_ERR_OOB;
    }
    return 1;
}

static void
cvt2s16(DSA_SAMPLE *sam, int16_t *out, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        out[i] = CLAMP(sam[i], -32768, 32767);
    }
}

static void
cvt2u8(DSA_SAMPLE *sam, uint8_t *out, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        int s8 = (sam[i] + 32768) / 256;
        out[i] = CLAMP(s8, 0, 255);
    }
}

static int
decode(void)
{
    DSA_DECODER dec;
    FILE *inpfile, *outfile;
    DSA_BUF buffer;
    DSA_PCM pcm;
    int code;
    DSA_FNUM frameno = 0;
    DSA_FNUM dec_frameno = 0;
    void *out_buf;
    int channels = -1, samprate = -1, depth = -1;
    DSA_TAGS *tags = NULL;

    memset(&dec, 0, sizeof(dec));

    if (opts.inp[0] == USE_STDIO_CHAR) {
        inpfile = stdin;
    } else {
        inpfile = fopen(opts.inp, "rb");
        if (inpfile == NULL) {
            printf("error opening input file %s\n", opts.inp);
            return EXIT_FAILURE;
        }
    }

    if (opts.out[0] == USE_STDIO_CHAR) {
        outfile = stdout;
    } else {
        outfile = fopen(opts.out, "wb");
        if (outfile == NULL) {
            printf("error opening output file %s\n", opts.out);
            return EXIT_FAILURE;
        }
    }
    out_buf = dsa_alloc(DSA_FRAME_SAMPLES * sizeof(int16_t));
    dsa_dec_init(&dec);
    dsa_dec_set_outbuf(&dec, NULL); /* use internal buffer so we don't have to manage sample memory */
    if (verbose) {
        printf(DRV_HEADER);
        printf("\n");
    }
    while (1) {
        int packet_type;

        if (read_packet(inpfile, &buffer, &packet_type) < 0) {
            DSA_ERROR(("error reading packet"));
            break;
        }
        code = dsa_dec(&dec, &buffer, &pcm, &frameno);
        if (code == DSA_DEC_GOT_TAGS) {
            static int got_it_once = 0;
            /* TODO: check if parameters changed mid-track? */
            if (!got_it_once) {
                tags = dsa_dec_get_tags(&dec);
                got_it_once = 1;
                DSA_INFO(("got tags"));
            }
        } else if (code == DSA_DEC_GOT_ID) {
            DSA_INFO(("got ID"));
        } else if (code == DSA_DEC_NEED_NEXT) {
            DSA_INFO(("needs next packet"));
            dec_frameno++;
        } else {
            int dec_nchan;

            if (code != DSA_DEC_OK && code != DSA_DEC_EOS) {
                continue;
            }
            dec_nchan = dsa_channels_for_mode(pcm.meta.mode);
            if (channels != -1 && dec_nchan != channels) {
                DSA_ERROR(("num channels not constant in decoded audio?"));
            }
            if (samprate != -1 && pcm.meta.rate != samprate) {
                DSA_ERROR(("rate not constant in decoded audio?"));
            }
            if (depth != -1 && pcm.meta.depth != depth) {
                DSA_ERROR(("bit depth not constant in decoded audio?"));
            }
            channels = dec_nchan;
            samprate = pcm.meta.rate;
            depth = pcm.meta.depth;

            DSA_ASSERT(pcm.len <= DSA_FRAME_SAMPLES);
            if (depth) {
                cvt2s16(pcm.samples, out_buf, pcm.len);
                savebuffer(out_buf, pcm.len * sizeof(int16_t));
            } else {
                cvt2u8(pcm.samples, out_buf, pcm.len);
                savebuffer(out_buf, pcm.len * sizeof(uint8_t));
            }

            if (verbose) {
                printf("\rdecoded frame (ID %u, actual %u)", frameno, dec_frameno);
                fflush(stdout);
            }
            dec_frameno++;

            if (code == DSA_DEC_EOS) {
                DSA_INFO(("got end of stream"));
                break;
            }
        }
    }

    if (verbose) {
        printf("\n");
    }
    DSA_INFO(("freeing decoder"));
    dsa_dec_free(&dec);
    dsa_free(out_buf);
    if (tags) {
        free_tags(tags);
        dsa_free(tags);
    }
    wav_write(channels, (samprate == 3) ? 48000 : (11025 << samprate), depth, enc_buf, bufsz, outfile);
    free(enc_buf);
    if (opts.inp[0] != USE_STDIO_CHAR) {
        fclose(inpfile);
    }
    if (opts.out[0] != USE_STDIO_CHAR) {
        fclose(outfile);
    }
    return EXIT_SUCCESS;
}

static int
startup(int argc, char **argv)
{
    if (!init_params(argc, argv)) {
        return EXIT_SUCCESS;
    }

    if (!promptoverwrite(opts.out)) {
        return EXIT_FAILURE;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (encoding) {
        return encode();
    }

    return decode();
}

static int
split_paths(int argc, char **argv)
{
    dsa_set_log_level(DSA_LEVEL_WARNING);

    if (argc < 2) {
        goto badarg;
    }
    if (argv[1][0] == 'e') {
        encoding = 1;
        return startup(argc - 1, argv + 1);
    }
    if (argv[1][0] == 'd') {
        encoding = 0;
        return startup(argc - 1, argv + 1);
    }
badarg:
    usage_general();
    return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
    static char standard[2] = { USE_STDIO_CHAR, '\0' };
    int ret;

    progname = argv[0];
    /* default to stdin/out */
    opts.inp = standard;
    opts.out = standard;

    ret = split_paths(argc, argv);

    dsa_memory_report();
    return ret;
}
