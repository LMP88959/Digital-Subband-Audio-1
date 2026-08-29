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

#ifndef _WAV_UTILS_H_
#define _WAV_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dsa.h"

#include <stdio.h>

typedef struct {
    uint32_t channels;
    uint32_t freq;
    uint32_t bits;
    uint32_t expected_bytes;
} DSA_WAV_DATA;

extern int wav_read(DSA_WAV_DATA *wav_data, FILE *infile);
extern int wav_write(int chan, int freq, int is16bit, void *buf, uint32_t len, FILE *f);

#ifdef __cplusplus
}
#endif

#endif
