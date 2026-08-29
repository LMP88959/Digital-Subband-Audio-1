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

#define _DSA1_IMPL_
#define _DSA1_MEMORY_STATS_
#include "dsa1dec.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define VERSION_BUILD 0

#define DRV_HEADER "Envel Graphics DSA v1.%d decoder by EMMIR 2025-2026. "\
                   "build %d\n", \
                    DSA_VERSION_MINOR, VERSION_BUILD

static char *progname = NULL;
static int dooverwrite = 1;
static int verbose = 0;

#define USE_STDIO_CHAR '-'

struct PARAM {
   char *prefix;
   int value;
   int min, max;
   int (*convert)(int);
   char *desc;
   char *extra;
};

static struct PARAM dec_params[] = {
    /* decoder has no params currently */
    { NULL, 0, 0, 0, NULL, "", "" }
};

static struct {
    char *inp; /* input file path */
    char *out; /* output file path */
} opts;

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
usage(int extra)
{
    char *p = progname;

    printf(DRV_HEADER);
    printf("usage: %s d [options]\n", p);
    printf("sample usage: %s -v -inp=out.dsa -out=recon.wav\n", p);
    print_params(dec_params, extra);
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
        dsa1_set_log_level(lvl);
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

    params = dec_params;

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

static void
free_tags(DSA_TAGS *tags)
{
    dsa1_free(tags->track);
    dsa1_free(tags->artist);
    dsa1_free(tags->album);
    dsa1_free(tags->comment);
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

#define MAGIC_RIFF          0x46464952
#define MAGIC_WAVE          0x45564157
#define MAGIC_FMT_          0x20746d66
#define MAGIC_DATA          0x61746164

#define WRITE8(val, f) temp8 =  (uint8_t)  val; fwrite(&temp8,1,1,f)

#define WRITE16(val, f) temp16 = (uint16_t) val;      \
  temp8 = (temp16 >> 0) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp16 >> 8) & 0xff; fwrite(&temp8,1,1,f)

#define WRITE32(val, f) temp32 = (uint32_t)   val;     \
  temp8 = (temp32 >>  0) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp32 >>  8) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp32 >> 16) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp32 >> 24) & 0xff; fwrite(&temp8,1,1,f)

static int
wav_write(int chan, int freq, int is16bit, void *buf, uint32_t len, FILE *f)
{
    uint8_t temp8;
    uint16_t temp16;
    uint32_t temp32;
    unsigned i;
    unsigned bps = (is16bit ? 2 : 1); /* bytes per sample */
    long int rifflen;
    int is_le = 0;
    union {
        uint16_t word;
        uint8_t bytes[2];
    } endtest;
    endtest.word = 0x0001;
    is_le = (endtest.bytes[0] == 1);
    DSA_INFO(("writing wav: %d bit, %d channels @ %d Hz", bps * 8, chan, freq));

    fwrite("RIFF", 1, 4, f);
    WRITE32(((uint32_t) ~0), f); /* gets filled in at the end */
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    WRITE32(16, f);
    WRITE16(1, f);
    WRITE16(chan, f);
    WRITE32(freq, f);
    WRITE32(freq * chan * bps, f);
    WRITE16(chan * bps, f);
    WRITE16(8 * bps, f);

    fwrite("data", 1, 4, f);
    WRITE32(len, f); /* len in bytes */
    if (bps == 2) {
        int16_t *b16 = buf;
        unsigned nsamp = (len / 2);
        if (!is_le) {
            for (i = 0; i < nsamp; i++) {
                b16[i] = (((b16[i] >> 8) & 0xff) | ((b16[i] & 0xff) << 8));
            }
        }
    }
    fwrite(buf, len, sizeof(uint8_t), f);

    rifflen = ftell(f) - 8;
    fseek(f, 4, SEEK_SET);
    WRITE32(rifflen, f);
    return 1;
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
    out_buf = (void *) dsa1_alloc(DSA_FRAME_SAMPLES * sizeof(int16_t));
    dsa1_dec_init(&dec);
    dsa1_dec_set_outbuf(&dec, NULL); /* use internal buffer so we don't have to manage sample memory */
    if (verbose) {
        printf(DRV_HEADER);
        printf("\n");
    }
    while (1) {
        int packet_type;
        uint8_t hdr[DSA_PACKET_HDR_SIZE];
        size_t n;

        /* ideally, seek to a packet by searching for 'DSA10' in the byte stream */

        /* attempt to read header from input stream */
        n = fread(hdr, 1, DSA_PACKET_HDR_SIZE, inpfile);
        /* process potential packet header */
        if (dsa1_mk_packet_buf(hdr, n, &buffer, &packet_type) < 0) {
            DSA_ERROR(("error reading packet header"));
            break;
        }
        /* good packet header, so we read the number of bytes the packet contains */
        n = fread(buffer.data + DSA_PACKET_HDR_SIZE, 1, buffer.len - DSA_PACKET_HDR_SIZE, inpfile);
        if (n < buffer.len - DSA_PACKET_HDR_SIZE) {
            dsa1_buf_free(&buffer);
            DSA_ERROR(("error reading packet payload"));
            break;
        }
        code = dsa1_dec(&dec, &buffer, &pcm, &frameno);
        if (code == DSA_DEC_GOT_TAGS) {
            static int got_it_once = 0;
            /* TODO: check if parameters changed mid-track? */
            if (!got_it_once) {
                tags = dsa1_dec_get_tags(&dec);
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
            dec_nchan = dsa1_channels_for_mode(pcm.meta.mode);
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
    dsa1_dec_free(&dec);
    dsa1_free(out_buf);
    if (tags) {
        free_tags(tags);
        dsa1_free(tags);
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
    return decode();
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

    dsa1_set_log_level(DSA_LEVEL_WARNING);
    ret = startup(argc, argv);

    dsa1_memory_report();
    return ret;
}
