#ifndef VCT_AUDIO_H
#define VCT_AUDIO_H

#include <nitro.h>

#include "vct.h"

#define VCT_AUDIO_DATA_SIZE 1088

#define VCT_HEADER_AUDIO     0x40
#define VCT_HEADER_AUDIO_END 0x41

typedef struct AudioHeader {
    u32 magic;
    u8 header;
    union {
        u8 codec;
        u8 marker;
    };
    u16 sequence;
    u32 timestamp;
} AudioHeader;

BOOL vct_init_audio(void);
void vct_finish_audio(void);
u8 *vct_prepare_send_buffer(void);
BOOL vct_handle_audio(u8 aid, u8 *buffer, u32 size, OSTick tick);
int vct_decode_audio_buffer(void);
BOOL vct_flush_send_buffer(void);

#endif // VCT_AUDIO_H
