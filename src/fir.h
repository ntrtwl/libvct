#ifndef VCT_FIR_H
#define VCT_FIR_H

#include <nitro.h>

#include "vct.h"

static inline u32 DIV64(u64 numer, u32 denom)
{
    CP_SetDiv64_32(numer, denom);
    return CP_GetDivResult32();
}

static inline u32 SQRT(u32 n)
{
    CP_SetSqrt32(n);
    return CP_GetSqrtResult32();
}

u32 HPFFilter(void *audio_data, u32 length);
void InitFIRFilter(void);
void vct_set_speaker_samples(void *spkBuffer, u32 length);
void DoFIRFilter(void *micSample, void *spkSample, u32 length, u32 micGain);
void vct_process_fir(void *micSample, u32 length, u32 gain);

#endif // VCT_FIR_H
