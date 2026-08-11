#ifndef VCT_UDP_BUFFER_H
#define VCT_UDP_BUFFER_H
#include <nitro.h>

#include "audio.h"

typedef struct UDPPacket UDPPacket;
typedef struct UDPPacket {
    UDPPacket *free;
    UDPPacket *prev;
    UDPPacket *next;
    int codec;
    u8 remote;
    u8 channel;
    u32 length;
    u32 sequence;
    u8 buffer[VCT_AUDIO_DATA_SIZE];
    int marker;
    u32 timestamp;
    OSTick receive_time;
    OSTick performance_time;
    u8 *samples;
} UDPPacket;

BOOL vct_init_packet_buffer(void *buffer, u32 size);
void vct_cleanup_packet_buffer(void);
UDPPacket *vct_alloc_packet_buffer(void);
void vct_free_packet_buffer(UDPPacket *buf);

#endif // VCT_UDP_BUFFER_H
