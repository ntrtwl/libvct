#include "fir.h"

#include <limits.h>
#include <nitro.h>

#include "audio.h"
#include "vct.h"

static int sCounterIn = 0;
static int sCounterOut = 0;
static int sTap[6];
static int sH[6];
static s16 sSpkBuffer[VCT_AUDIO_DATA_SIZE / sizeof(s16)];
static s16 sDelayLine[2048];

#define MAX_DELAY_LINE (sizeof(sDelayLine) / sizeof(s16))

#define Q15_SHIFT 15

#define Q15_CONST(x) (((x) > 0)           \
        ? ((x) * (1 << Q15_SHIFT) + 0.5f) \
        : ((x) * (1 << Q15_SHIFT) - 0.5f))

s16 sImpulseResponse[8] = {
    Q15_CONST(-0.17203), Q15_CONST(0.50186), Q15_CONST(-0.43957), Q15_CONST(0.35013), Q15_CONST(-0.37372), Q15_CONST(0.02722),
    Q15_CONST(0.06982), Q15_CONST(-0.08145)
};

// these correspond to a cutoff of 239.45Hz
#define GAIN 0.9138
#define POLE 0.8276

u32 HPFFilter(void *audio_data, u32 length)
{
    u32 i;
    u64 sum;
    int in;
    s16 *sample = audio_data;

    static int prevOut = 0;
    static int prevIn = 0;

    length /= sizeof(s16);
    in = sample[0] * FX32_CONST(GAIN);
    prevOut = (in + prevIn + prevOut * FX32_CONST(POLE)) >> FX32_SHIFT;
    sum = prevOut * prevOut;
    prevIn = -in;

    for (i = 1; i < length; i++) {
        in = sample[i] * FX32_CONST(GAIN);
        sample[i - 1] = prevOut;
        prevOut = (in + prevIn + prevOut * FX32_CONST(POLE)) >> FX32_SHIFT;
        sum += prevOut * prevOut;
        prevIn = -in;
    }
    sample[length - 1] = prevOut;

    return DIV64(sum, length);
}

void InitFIRFilter(void)
{
    s16 *imp = sImpulseResponse;
    int delay = 1635;

    sCounterIn = 0;
    sCounterOut = MAX_DELAY_LINE - delay;

    for (int i = 0; i < (int)MAX_DELAY_LINE; i++) {
        sDelayLine[i] = 0;
    }
    for (int i = 0; i < 6; i++) {
        sTap[i] = 0;
        sH[i] = imp[i];
    }
}

void vct_set_speaker_samples(void *spkBuffer, u32 length)
{
    MI_CpuCopyFast(spkBuffer, sSpkBuffer, length);
}

static inline s16 process_sample_unroll(s16 spkSample)
{
    int acc = 0;
    sDelayLine[sCounterIn] = spkSample;
    sTap[0] = sDelayLine[sCounterOut];

    acc = sTap[5] * sH[5];
    acc += sTap[4] * sH[4];
    sTap[5] = sTap[4];
    acc += sTap[3] * sH[3];
    sTap[4] = sTap[3];
    acc += sTap[2] * sH[2];
    sTap[3] = sTap[2];
    acc += sTap[1] * sH[1];
    sTap[2] = sTap[1];
    acc += sTap[0] * sH[0];
    sTap[1] = sTap[0];

    if (acc > SHRT_MAX << Q15_SHIFT) {
        acc = SHRT_MAX << Q15_SHIFT;
    } else if (acc < SHRT_MIN << Q15_SHIFT) {
        acc = SHRT_MIN << Q15_SHIFT;
    }

    sCounterIn = (sCounterIn + 1) % MAX_DELAY_LINE;
    sCounterOut = (sCounterOut + 1) % MAX_DELAY_LINE;

    return acc >> Q15_SHIFT;
}

void DoFIRFilter(void *micSample, void *spkSample, u32 length, u32 micGain)
{
    int i;
    u32 spkGain;
    u64 sum = 0;
    s16 *mic = micSample;
    s16 *spk = spkSample;

    static s16 shift = 0;

    length /= sizeof(s16);
    for (i = 0; i < length; i++) {
        spk[i] = process_sample_unroll(spk[i]);
        sum += spk[i] * spk[i];
        spk[i] = mic[i];
    }

    spkGain = DIV64(sum, length);
    spkGain = SQRT(spkGain);

    if (spkGain * 3 < micGain * 2) {
        shift = 0;
        return;
    }
    if (micGain < spkGain) {
        shift = 4;
    }

    if (shift < 4) {
        shift++;
    }

    for (i = 0; i < length; i++) {
        mic[i] >>= shift;
    }
}

void vct_process_fir(void *micSample, u32 length, u32 gain)
{
    DoFIRFilter(micSample, sSpkBuffer, length, gain);
}
