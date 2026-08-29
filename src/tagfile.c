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

#include "tagfile.h"

#include <stdlib.h>
#include <errno.h>

#define MAX_LINE_LENGTH 65535

static int
all_ascii(char *s, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        if ((unsigned char) s[i] > 0x7f) {
            return 0;
        }
    }
    return 1;
}

static int
set_str(char **dst, uint16_t *sz, char *s, unsigned n)
{
    char *p;

    if (n > UINT16_MAX || !all_ascii(s, n)) {
        printf("string too long or contains non ASCII character(s)\n");
        return 0;
    }
    p = dsa_alloc(n + 1);
    if (!p) {
        return 0;
    }
    memcpy(p, s, n);
    p[n] = '\0';

    if (*dst) {
        dsa_free(*dst);
    }
    *dst = p;
    *sz = (uint16_t) n;
    return 1;
}

static int
set_num(void *dst, int is_u8, char *s, unsigned n)
{
    char buf[32];
    int v;
    char *tail;

    if (!n || n >= sizeof(buf) || !all_ascii(s, n)) {
        return 0;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';

    errno = 0;
    v = (int) strtol(buf, &tail, 10);
    if (errno == ERANGE) {
        printf("integer out of integer range\n");
        return 0;
    }
    if (errno != 0) {
        printf("bad string: %s\n", strerror(errno));
        return 0;
    }
    if (*tail != '\0') {
        printf("integer contained non-numeric characters\n");
        return 0;
    }

    if (is_u8) {
        if (v > UINT8_MAX) {
            printf("integer out of expected unsigned 8-bit range\n");
            return 0;
        }
        *(uint8_t*) dst = (uint8_t) v;
    } else {
        if (v > UINT16_MAX) {
            printf("integer out of expected unsigned 16-bit range\n");
            return 0;
        }
        *(uint16_t*) dst = (uint16_t) v;
    }
    return 1;
}

static int
parse_line(DSA_TAGS *tags, char *line, unsigned lno)
{
    char *p, *sep, *key, *val, *end;
    unsigned i, n, valstrlen;
    struct {
        char *key;
#define VTYPE_STR 0
#define VTYPE_U16 1
#define VTYPE_U8  2
        int vtype;
        union {
            struct {
                char **sp;
                uint16_t *sz;
            } str;
            uint16_t *u16;
            uint8_t *u8;
        } out;
    } table[8], *e;

#define PUT_STR(i, name, field, tags)              \
        do { table[i].key = (name);                \
             table[i].vtype = VTYPE_STR;           \
             table[i].out.str.sp = &(tags)->field; \
             table[i].out.str.sz = &(tags)->field##_sz; } while (0)

#define PUT_U16(i, name, field, tags)    \
        do { table[i].key = (name);      \
             table[i].vtype = VTYPE_U16; \
             table[i].out.u16 = &(tags)->field; } while (0)

#define PUT_U8(i, name, field, tags)    \
        do { table[i].key = (name);     \
             table[i].vtype = VTYPE_U8; \
             table[i].out.u8 = &(tags)->field; } while (0)

    PUT_STR(0, "track", track, tags);
    PUT_STR(1, "artist", artist, tags);
    PUT_STR(2, "album", album, tags);
    PUT_STR(3, "comment", comment, tags);
    PUT_U16(4, "year", year, tags);
    PUT_U8(5, "track_num", track_num, tags);
    PUT_U8(6, "total_tracks", total_tracks, tags);
    PUT_U8(7, "genre", genre, tags);

#undef PUT_STR
#undef PUT_U16
#undef PUT_U8

/* non-control-character whitespace */
#define IS_SPACE_NOT_CONTROL(v) (((v) == ' ') || ((v) == '\t'))
#define IS_LINEEND(v) (((v) == '\n') || ((v) == '\r'))

    /* heavily commented because of how difficult it can be to easily understand string parsing code */
    p = line;
    /* skip leading whitespace */
    while (IS_SPACE_NOT_CONTROL(*p)) {
        p++;
    }
    /* make sure there is still a string (and not a new line or comment) */
    if (!*p || *p == '#' || IS_LINEEND(*p)) {
        return 1; /* not an error, just skip blank lines */
    }
    /* ensure there is an equal sign to signify the beginning of the 'value' */
    sep = strchr(p, '=');
    if (!sep) {
        DSA_ERROR(("equal sign missing in tag file on line %u", lno));
        return 0;
    }
    key = p;
    /* trim space after key and before equal sign */
    while (sep > key && IS_SPACE_NOT_CONTROL(sep[-1])) {
        sep--;
    }
    val = sep + 1;
    /* trim space after equal sign and before value */
    while (IS_SPACE_NOT_CONTROL(*val)) {
        val++;
    }
    end = val;
    /* find of end value string, stop at either a line ending or a comment (denoted by #) */
    while (*end) {
        if ((*end == '#' || IS_LINEEND(*end))) {
            break;
        }
        end++;
    }
    /* trim space after value (including some control chars because this should be the end of the line) */
    while (end > val && (IS_SPACE_NOT_CONTROL(end[-1]) || IS_LINEEND(end[-1]))) {
        end--;
    }
    n = (unsigned) (sep - key);
    if (n == 0) {
        DSA_ERROR(("strange parse error on line %u [%s:%s]", lno, key, val));
        return 0;
    }
    e = NULL;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strlen(table[i].key) == n && memcmp(table[i].key, key, n) == 0) {
            e = &table[i];
            break;
        }
    }
    if (!e) {
        /* unknown entry */
        DSA_WARNING(("unknown tag [%s] in tag file on line %u", key, lno));
        return 1;
    }
    valstrlen = (unsigned) (end - val);
    switch (e->vtype) {
        case VTYPE_STR:
            return set_str(e->out.str.sp, e->out.str.sz, val, valstrlen);
        case VTYPE_U16:
            return set_num(e->out.u16, 0, val, valstrlen);
        case VTYPE_U8:
            return set_num(e->out.u8, 1, val, valstrlen);
        default:
            return 0;
    }
    return 1;
#undef IS_SPACE_NOT_CONTROL
#undef IS_RETURN
}

extern int
tagfile_read(DSA_TAGS *tags, FILE *tagf)
{
    char *line;
    int ret = 1;
    unsigned lno = 0;

    if (!tagf || !tags) {
        return 0;
    }
    line = calloc(MAX_LINE_LENGTH + 128, sizeof(char));

    while (fgets(line, MAX_LINE_LENGTH + 128, tagf)) {
        if (!parse_line(tags, line, lno)) {
            ret = 0;
            break;
        }
        lno++;
    }
    free(line);
    return ret;
}
