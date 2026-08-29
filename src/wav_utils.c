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

#include "wav_utils.h"

#define MAGIC_RIFF          0x46464952
#define MAGIC_WAVE          0x45564157
#define MAGIC_FMT_          0x20746d66
#define MAGIC_DATA          0x61746164

#define READSKIP(bytes, f, err_label) if (fseek(f, bytes, SEEK_CUR) != 0) { goto err_label; }

#define READBYTE(f, err_label) do { if (!fread(&temp8,1,1,f)) { goto err_label; } } while(0)
#define READ8(val, f, err_label)  READBYTE(f, err_label); val = temp8

#define READ16(val, f, err_label) \
  READBYTE(f, err_label); temp16  = temp8; \
  READBYTE(f, err_label); temp16 |= ((uint16_t)temp8 << 8); val = temp16 \

#define READ32(val, f, err_label) \
  READBYTE(f, err_label); temp32  = temp8; \
  READBYTE(f, err_label); temp32 |= ((uint32_t)temp8 << 8); \
  READBYTE(f, err_label); temp32 |= ((uint32_t)temp8 << 16); \
  READBYTE(f, err_label); temp32 |= ((uint32_t)temp8 << 24); val = temp32 \


#define WRITE8(val, f) temp8 =  (uint8_t)  val; fwrite(&temp8,1,1,f)

#define WRITE16(val, f) temp16 = (uint16_t) val; \
  temp8 = (temp16 >> 0) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp16 >> 8) & 0xff; fwrite(&temp8,1,1,f)

#define WRITE32(val, f) temp32 = (uint32_t)   val; \
  temp8 = (temp32 >>  0) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp32 >>  8) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp32 >> 16) & 0xff; fwrite(&temp8,1,1,f); \
  temp8 = (temp32 >> 24) & 0xff; fwrite(&temp8,1,1,f)

extern int
wav_read(DSA_WAV_DATA *data, FILE *f)
{
    uint8_t temp8;
    uint16_t temp16;
    uint32_t temp32;

    uint32_t magic;
    uint16_t audio_fmt;
    uint16_t nchan;
    uint32_t sample_rate;
    uint16_t bits_per_samp;
    uint32_t fmt_sz;
    int got_data = 0;
    while (!got_data) {
        uint32_t skip;

        READ32(magic, f, readerr);

        switch (magic) {
            case MAGIC_FMT_:
                READ32(fmt_sz, f, readerr);
                READ16(audio_fmt, f, readerr);
                if (audio_fmt != 1) {
                    DSA_ERROR(("WAV audio format must be 1 (linear PCM). read value = %d", audio_fmt));
                    return 0;
                }
                READ16(nchan, f, readerr);
                if (nchan != 1 && nchan != 2) {
                    DSA_ERROR(("WAV must have one or two channels. read value = %d", nchan));
                    return 0;
                }
                READ32(sample_rate, f, readerr);
                if (sample_rate == 0 || (sample_rate != 48000 && (sample_rate % 11025) != 0)) {
                    DSA_ERROR(("WAV sampling frequency/rate is invalid or unsupported. read value = %d", sample_rate));
                    return 0;
                }
                READSKIP(4 + 2, f, readerr);
                READ16(bits_per_samp, f, readerr);
                if (bits_per_samp != 8 && bits_per_samp != 16) {
                    DSA_ERROR(("WAV must contain either 8-bit or 16-bit samples. read value = %d", bits_per_samp));
                    return 0;
                }
                if (fmt_sz == 18) {
                    uint16_t extra;

                    READ16(extra, f, readerr);
                    READSKIP(extra, f, readerr);
                }
            break;
            case MAGIC_RIFF:
                READSKIP(4, f, readerr);
                READ32(magic, f, readerr);
                if (magic != MAGIC_WAVE) {
                    DSA_ERROR(("WAV file format not WAVE. read value = 0x%x, expected = 0x%x", magic, MAGIC_WAVE));
                    return 0;
                }
                break;
            case MAGIC_DATA:
                got_data = 1;
                READ32(data->expected_bytes, f, readerr);
                break;
            default:
                READ32(skip, f, readerr);
                READSKIP(skip, f, readerr);
                break;
        }
    }

    data->channels = nchan;
    data->freq = sample_rate;
    data->bits = bits_per_samp;
    return 1;
readerr:
    DSA_ERROR(("error while reading WAV file header!"));
    return 0;
}

extern int
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
    WRITE32(UINT32_MAX, f); /* gets filled in at the end */
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
