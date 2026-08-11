#include "mixer.h"

#include <limits.h>
#include <nitro.h>

void vct_mix_audio(s16 *left, s16 *right, s16 *out, u32 length)
{
    for (int i = 0; i < length / sizeof(s16); i += 4) {
        s32 s = left[i] + right[i];
        if (s > SHRT_MAX) {
            s = SHRT_MAX;
        } else if (s < SHRT_MIN) {
            s = SHRT_MIN;
        }
        out[i] = s;

        s = left[i + 1] + right[i + 1];
        if (s > SHRT_MAX) {
            s = SHRT_MAX;
        } else if (s < SHRT_MIN) {
            s = SHRT_MIN;
        }
        out[i + 1] = s;

        s = left[i + 2] + right[i + 2];
        if (s > SHRT_MAX) {
            s = SHRT_MAX;
        } else if (s < SHRT_MIN) {
            s = SHRT_MIN;
        }
        out[i + 2] = s;

        s = left[i + 3] + right[i + 3];
        if (s > SHRT_MAX) {
            s = SHRT_MAX;
        } else if (s < SHRT_MIN) {
            s = SHRT_MIN;
        }
        out[i + 3] = s;
    }
}
