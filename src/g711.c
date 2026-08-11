/*
 * This source code is a product of Sun Microsystems, Inc. and is provided
 * for unrestricted use.  Users may copy or modify this source code without
 * charge.
 *
 * SUN SOURCE CODE IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING
 * THE WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun source code is provided with no support and without any obligation on
 * the part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY THIS SOFTWARE
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */
#include "g711.h"

#include <nitro.h>

#include "vct.h"

static int ulaw_segment[8] = {
    0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF
};

#define BIAS       0x84 /* Bias for linear code. */
#define CLIP       8159
#define SIGN_BIT   0x80 /* Sign bit for a u-law byte. */
#define QUANT_MASK 0xF /* Quantization field mask. */
#define SEG_SHIFT  4 /* Left shift for segment number. */
#define SEG_MASK   0x70 /* Segment field mask. */

static inline int search(int val, int *table, int size)
{
    for (int i = 0; i < size; i++) {
        if (val <= *table++) {
            return i;
        }
    }
    return size;
}

/*
 * linear2ulaw() - Convert a linear PCM value to u-law
 *
 * In order to simplify the encoding process, the original linear magnitude
 * is biased by adding 33 which shifts the encoding range from (0 - 8158) to
 * (33 - 8191). The result can be seen in the following encoding table:
 *
 *  Biased Linear Input Code	Compressed Code
 *  ------------------------	---------------
 *  00000001wxyza               000wxyz
 *  0000001wxyzab               001wxyz
 *  000001wxyzabc               010wxyz
 *  00001wxyzabcd               011wxyz
 *  0001wxyzabcde               100wxyz
 *  001wxyzabcdef               101wxyz
 *  01wxyzabcdefg               110wxyz
 *  1wxyzabcdefgh               111wxyz
 *
 * Each biased linear code has a leading 1 which identifies the segment
 * number. The value of the segment number is equal to 7 minus the number
 * of leading 0's. The quantization interval is directly available as the
 * four bits wxyz.  * The trailing bits (a - h) are ignored.
 *
 * Ordinarily the complement of the resulting code word is used for
 * transmission, and so the code word is complemented before it is returned.
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
static inline int linear2ulaw(int pcm_val)
{
    int mask;

    /* Get the sign and the magnitude of the value. */
    pcm_val >>= 2;
    if (pcm_val < 0) {
        pcm_val = -pcm_val;
        mask = 0x7F;
    } else {
        mask = 0xFF;
    }
    if (pcm_val > CLIP) {
        pcm_val = CLIP; /* clip the magnitude */
    }
    pcm_val += (BIAS >> 2);

    /* Convert the scaled magnitude to segment number. */
    int seg = search(pcm_val, ulaw_segment, 8);

    /*
     * Combine the sign, segment, quantization bits;
     * and complement the code word.
     */
    if (seg >= 8) { /* out of range, return maximum value. */
        return 0x7F ^ mask;
    } else {
        int uval = (seg << SEG_SHIFT) | ((pcm_val >> (seg + 1)) & QUANT_MASK);
        return uval ^ mask;
    }
}

void vct_encode_g711_ulaw(u8 *g711, s16 *pcm, u32 pcm_samples)
{
    for (int i = 0; i < pcm_samples; i++) {
        g711[i] = linear2ulaw(pcm[i]);
    }
}

/*
 * ulaw2linear() - Convert a u-law value to 16-bit linear PCM
 *
 * First, a biased linear code is derived from the code word. An unbiased
 * output can then be obtained by subtracting 33 from the biased code.
 *
 * Note that this function expects to be passed the complement of the
 * original code word. This is in keeping with ISDN conventions.
 */
static inline int ulaw2linear(int u_val)
{
    /* Complement to obtain normal u-law value. */
    u_val = ~u_val;

    /*
     * Extract and bias the quantization bits. Then
     * shift up by the segment number and subtract out the bias.
     */
    int t = ((u_val & QUANT_MASK) << 3) + BIAS;
    t <<= (u_val & SEG_MASK) >> SEG_SHIFT;

    return (u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS);
}

void vct_decode_g711_ulaw(u8 *g711, s16 *pcm, u32 data_length)
{
    for (int i = 0; i < data_length; i++) {
        pcm[i] = ulaw2linear(g711[i]);
    }
}

void vct_encode_8bit_raw(u8 *encoded, s16 *pcm, u32 pcm_samples)
{
    s8 *out = encoded;
    for (int i = 0; i < pcm_samples; i++) {
        out[i] = pcm[i] >> 8;
    }
}

void vct_decode_8bit_raw(u8 *encoded, s16 *pcm, u32 data_length)
{
    s8 *in = encoded;
    for (int i = 0; i < data_length; i++) {
        pcm[i] = in[i] << 8;
    }
}
