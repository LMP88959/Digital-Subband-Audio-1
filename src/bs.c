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

/* B. Bitstream */

extern void
dsa_bs_init(DSA_BS *s, uint8_t *buffer)
{
    s->start = buffer;
    s->pos = 0;
}

extern void
dsa_bs_align(DSA_BS *s)
{
    if (dsa_bs_aligned(s)) {
        return; /* already aligned */
    }
    s->pos = ((s->pos + (unsigned) 7) & (~(unsigned) 7)); /* byte align */
}

extern void
dsa_bs_concat(DSA_BS *s, uint8_t *data, int len)
{
    if (!dsa_bs_aligned(s)) {
        DSA_ERROR(("concat to unaligned bs"));
    }
    if (len == 0) {
        return;
    }
    memcpy(s->start + dsa_bs_ptr(s), data, len);
    s->pos += len * 8;
}

/* static versions to possibly make the compiler more likely to inline */
static void
local_put_bit(DSA_BS *s, int v)
{
    if (v) {
        s->start[dsa_bs_ptr(s)] |= 1 << (7 - (s->pos & 7));
    }
    s->pos++;
}

static void
local_put_one(DSA_BS *s)
{
    s->start[dsa_bs_ptr(s)] |= 1 << (7 - (s->pos & 7));
    s->pos++;
}

static unsigned
local_get_bit(DSA_BS *s)
{
    unsigned out;

    out = s->start[dsa_bs_ptr(s)] >> (7 - (s->pos & 7));
    s->pos++;

    return out & 1;
}

static void
local_put_bits(DSA_BS *s, unsigned n, unsigned v)
{
    unsigned rem, bit;
    uint8_t data;

    while (n > 0) {
        rem = 8 - (s->pos & 7);
        rem = MIN(n, rem);
        bit = (8 - (s->pos & 7)) - rem;
        data = (v >> (n - rem)) & ((1 << rem) - 1);
        s->start[dsa_bs_ptr(s)] |= data << bit;
        n -= rem;
        s->pos += rem;
    }
}

extern void
dsa_bs_put_bit(DSA_BS *s, int v)
{
    local_put_bit(s, v);
}

extern unsigned
dsa_bs_get_bit(DSA_BS *s)
{
    return local_get_bit(s);
}

extern void
dsa_bs_put_bits(DSA_BS *s, unsigned n, unsigned v)
{
    local_put_bits(s, n, v);
}

extern unsigned
dsa_bs_get_bits(DSA_BS *s, unsigned n)
{
    unsigned rem, bit, out = 0;

    while (n > 0) {
        rem = 8 - (s->pos & 7);
        rem = MIN(n, rem);
        bit = (8 - (s->pos & 7)) - rem;
        out <<= rem;
        out |= (s->start[dsa_bs_ptr(s)] & (((1 << rem) - 1) << bit)) >> bit;
        n -= rem;
        s->pos += rem;
    }
    return out;
}

static unsigned
local_update_rice_k(unsigned avg)
{
    unsigned k = 0;
    avg >>= 2;
    while (avg >>= 1) {
        k++;
    }
    return k;
}

static unsigned
local_update_rice_state(unsigned ravg, unsigned v)
{
    return ravg - (ravg >> 2) + v;
}

extern void
dsa_bs_put_rice(DSA_BS *bs, unsigned v, unsigned *rk, unsigned *avg)
{
    unsigned k = *rk;
    unsigned q = v >> k;

    bs->pos += q; /* equivalent to putting 'q' zeroes, assuming buffer was clear */
    local_put_one(bs);
    local_put_bits(bs, k, v);

    *avg = local_update_rice_state(*avg, v);
    *rk = local_update_rice_k(*avg);
}

extern unsigned
dsa_bs_get_rice(DSA_BS *bs, unsigned *rk, unsigned *avg)
{
    unsigned k = *rk;
    unsigned v, q = 0;

    while (!local_get_bit(bs)) {
        q++;
    }
    v = (q << k) | dsa_bs_get_bits(bs, k);

    *avg = local_update_rice_state(*avg, v);
    *rk = local_update_rice_k(*avg);
    return v;
}

/* escaped adaptive Rice coding */
#define RICE_ESCAPE_Q (DSA_MAX_FRAME_SUBBANDS * 2 / 3)

extern void
dsa_bs_put_erice(DSA_BS *bs, unsigned v, unsigned *rk, unsigned *avg)
{
    unsigned k = *rk;
    unsigned q = v >> k;

    if (q < RICE_ESCAPE_Q) {
        bs->pos += q; /* equivalent to putting 'q' zeroes, assuming buffer was clear */
        local_put_one(bs);
    } else {
        unsigned x, l, n;

        x = q - (RICE_ESCAPE_Q - 1);
        l = 0;
        n = x;
        bs->pos += RICE_ESCAPE_Q; /* put escape prefix */
        local_put_one(bs);
        while (n >>= 1) {
            l++;
        }
        bs->pos += l; /* n leading zeroes */
        local_put_one(bs);
        local_put_bits(bs, l, x);
    }
    local_put_bits(bs, k, v);

    *avg = local_update_rice_state(*avg, v);
    *rk = local_update_rice_k(*avg);
}

extern unsigned
dsa_bs_get_erice(DSA_BS *bs, unsigned *rk, unsigned *avg)
{
    unsigned k = *rk;
    unsigned v, q = 0;

    while (!local_get_bit(bs)) {
        q++;
    }
    if (q == RICE_ESCAPE_Q) {
        q = 0;
        while (!local_get_bit(bs)) {
            q++;
        }
        q = RICE_ESCAPE_Q - 1 + (((unsigned) 1 << q) | dsa_bs_get_bits(bs, q));
    }
    v = (q << k) | dsa_bs_get_bits(bs, k);

    *avg = local_update_rice_state(*avg, v);
    *rk = local_update_rice_k(*avg);
    return v;
}
