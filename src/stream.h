#ifndef VCT_STREAM_H
#define VCT_STREAM_H
#include <nitro.h>

#include "udp_buffer.h"
#include "vct.h"

typedef struct Equation {
    u32 prev;
    u32 sum;
} Equation;

typedef struct AudioStream {
    VCTAudioInfo info;
    Equation latency;
    u32 comfortLevel;
    u32 channel;
    int aid;
    UDPPacket *prevBuffer;
    int recoverCount;
    OSTick firstLocalTime;
    u32 firstRemoteTime;
    u32 prevTimestamp;
    u32 receiveSequence;
    u32 playSequence;
    int initSequence;
    u32 cycles;
    u32 badSequence;
    u16 maxSequence;
    u32 resetSkew;
    int skewCount;
    int delayEstimate;
    int activeDelay;
    int jitter;
    int diff;
    int spike;
    int lowThr;
    u32 highThr;
    u32 thrCount;
} AudioStream;

void vct_init_stream(AudioStream *stream, u32 channel, int aid);
void vct_reset_stream(AudioStream *stream);
int vct_save_previous_buffer(AudioStream *stream, UDPPacket *packet);
void vct_init_sequence(AudioStream *stream, u16 sequence);
BOOL vct_update_sequence(AudioStream *stream, UDPPacket *packet, u16 sequence);
BOOL vct_calc_jitter_and_skew(AudioStream *stream, UDPPacket *packet);

#endif // VCT_STREAM_H
