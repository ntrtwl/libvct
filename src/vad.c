#include "vad.h"

#include <nitro.h>

#include "fir.h"
#include "vct.h"

#define DEFAULT_RELEASE_COUNT 15

static VCTVADInfo sVADInfo = {
    .activity = FALSE,
    .scale = 0,
    .releaseCount = DEFAULT_RELEASE_COUNT,
    .releaseTime = DEFAULT_RELEASE_COUNT * VCT_AUDIO_FRAME_LENGTH
};

static u32 sNoiseThreshold;
static int sLostCount;
static u32 sIndex;

static u32 E2scales[4];

void VCT_SetVADReleaseTime(int count)
{
    sVADInfo.releaseCount = count;
    sVADInfo.releaseTime = sVADInfo.releaseCount * VCT_AUDIO_FRAME_LENGTH;
}

static inline void reset_E2(void)
{
    for (int i = 0; i < 4; i++) {
        E2scales[i] = 0x01000000;
    }
    
    sIndex = 0;
}

void VCT_ResetVAD(void)
{
    reset_E2();
    sNoiseThreshold = 0;
    sVADInfo.activity = FALSE;
    sVADInfo.scale = 0;
    sLostCount = 0;
}

void VCT_GetVADInfo(VCTVADInfo *outInfo)
{
    if (outInfo != NULL) {
        MI_CpuCopy8(&sVADInfo, outInfo, sizeof(VCTVADInfo));
    }
}

static inline u32 calc_avg_E2()
{
    int i;
    u32 E2 = 0;

    for (i = 0; i < 4; i++) {
        E2 += E2scales[i];
    }

    return SQRT(E2 / 4);
}

static inline u32 calc_threshold()
{
    u32 i;
    u32 E2 = 0;

    for (i = 0; i < 4; i++) {
        E2 += E2scales[i];
    }

    E2 = SQRT(E2 / 4);
    u32 result = E2 * 3 / 2;
    return result;
}

static inline void store_E2(u32 scale)
{
    E2scales[sIndex] = scale;
    sIndex = (sIndex + 1) % 4;
}

VCTVADStatus VCTi_GetVADStatus(u32 scale, u32 E1, u8 *outScale)
{
    int result;

    sVADInfo.scale = E1 >> 8;
    if (outScale != NULL) {
        *outScale = E1 >> 8;
    }

    if (!sVADInfo.activity) {
        u32 E2 = calc_avg_E2();
        if (E1 != 0 && E1 >= E2 * 2) {
            sNoiseThreshold = calc_threshold();
            sIndex = 0;
            result = VCT_VAD_DETECT;
            sVADInfo.activity = TRUE;
        } else {
            result = VCT_VAD_NONE;
        }
        store_E2(scale);
    } else {
        store_E2(scale);

        u32 E2 = calc_avg_E2();
        if (E2 <= sNoiseThreshold) {
            if (++sLostCount > sVADInfo.releaseCount) {
                sVADInfo.activity = FALSE;
                sLostCount = 0;
                sIndex = 0;
                return VCT_VAD_LOST;
            }
        } else {
            sLostCount = 0;
        }
        result = VCT_VAD_ACTIVE;
    }

    return result;
}

void vct_init_vad(void)
{
    sVADInfo.releaseCount = DEFAULT_RELEASE_COUNT;
    sVADInfo.releaseTime = sVADInfo.releaseCount * VCT_AUDIO_FRAME_LENGTH;
    VCT_ResetVAD();
}
