#ifndef VCT_VAD_H
#define VCT_VAD_H
#include <nitro.h>

typedef enum {
    VCT_VAD_NONE = 0,
    VCT_VAD_DETECT,
    VCT_VAD_ACTIVE,
    VCT_VAD_LOST,
} VCTVADStatus;

VCTVADStatus VCTi_GetVADStatus(u32 scale, u32 E1, u8 *outScale);
void vct_init_vad(void);

#endif // VCT_VAD_H
