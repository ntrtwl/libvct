#include "stream.h"

#include <nitro.h>

#include "queue.h"
#include "udp_buffer.h"
#include "vct.h"

static inline int default_jitter_buffer_size(void)
{
    return OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH * 2) + OS_MilliSecondsToTicks(15);
}

static inline void init_equation(Equation *eq)
{
    eq->prev = 0;
    eq->sum = 0;
}

void vct_init_stream(AudioStream *stream, u32 channel, int aid)
{
    MI_CpuClear8(&stream->info, sizeof(VCTAudioInfo));
    stream->info.bufferLatency = default_jitter_buffer_size();
    init_equation(&stream->latency);
    stream->channel = channel;
    stream->aid = aid;
    stream->jitter = 0;
    stream->diff = 0;
    stream->spike = 0;
    stream->lowThr = 0;
    stream->highThr = default_jitter_buffer_size();
    stream->thrCount = 0;
    stream->firstRemoteTime = 0;
    stream->firstLocalTime = 0;
    stream->resetSkew = 0;
    stream->skewCount = 0;
    stream->prevTimestamp = 0;
    stream->receiveSequence = 0;
    stream->playSequence = 0;
    stream->initSequence = 1;
    stream->prevBuffer = NULL;
    stream->recoverCount = 0;
    stream->comfortLevel = 0;
}

void vct_reset_stream(AudioStream *stream)
{
    if (stream->prevBuffer != NULL) {
        vct_free_packet_buffer(stream->prevBuffer);
        stream->prevBuffer = NULL;
    }

    stream->aid = -1;
}

int vct_save_previous_buffer(AudioStream *stream, UDPPacket *packet)
{
    if (stream->prevBuffer != NULL) {
        vct_free_packet_buffer(stream->prevBuffer);
    }

    stream->prevBuffer = packet;
    return vct_pop_audio_queue(stream->channel);
}

void vct_init_sequence(AudioStream *stream, u16 sequence)
{
    stream->maxSequence = sequence;
    stream->badSequence = 65537;
    stream->cycles = 0;
    stream->initSequence = 0;
}

BOOL vct_update_sequence(AudioStream *stream, UDPPacket *packet, u16 sequence)
{
    u16 udelta = sequence - stream->maxSequence;
    if (udelta < 3000) {
        if (sequence < stream->maxSequence) {
            stream->cycles += 65536;
        }

        stream->maxSequence = sequence;
    } else if (udelta <= 65436) {
        if (sequence == stream->badSequence) {
            vct_init_sequence(stream, sequence);
            stream->receiveSequence = (sequence - 1) & 0xFFFF;
        } else {
            stream->badSequence = (sequence + 1) & 0xFFFF;
            return FALSE;
        }
    } else if (sequence + stream->cycles > stream->receiveSequence + 65436) {
        packet->sequence = sequence + stream->cycles - 65536;
        return TRUE;
    }

    packet->sequence = sequence + stream->cycles;
    return TRUE;
}

BOOL vct_calc_jitter_and_skew(AudioStream *stream, UDPPacket *packet)
{
    OSTick remote;
    OSTick local;
    int RmS;
    int adjustment = 0;

    if (stream->initSequence != 0) {
        vct_init_sequence(stream, packet->sequence);
    } else {
        if (!vct_update_sequence(stream, packet, packet->sequence)) {
            stream->info.dropCount++;
            return FALSE;
        }

        stream->info.sequence = packet->sequence;

        if (stream->receiveSequence == packet->sequence) {
            stream->info.dropCount++;
            return FALSE;
        }

        if (stream->receiveSequence + 1 != packet->sequence && packet->marker == 0) {
            int d = MATH_IAbs(packet->sequence - stream->receiveSequence);
            if (d > 100) {
                stream->receiveSequence = packet->sequence;
                return FALSE;
            }

            if (stream->receiveSequence > packet->sequence) {
                stream->info.jamCount++;
            }
        }
    }

    stream->receiveSequence = packet->sequence;

    if (stream->firstRemoteTime == 0) {
        stream->firstRemoteTime = packet->timestamp;
        stream->firstLocalTime = packet->receive_time;
        stream->resetSkew = 0;
    }

    remote = (packet->timestamp - stream->firstRemoteTime) * 64;
    local = packet->receive_time - stream->firstLocalTime;
    RmS = remote - local;

    if (stream->diff == 0 || packet->marker != 0) {
        stream->diff = RmS;
    } else {
        int Dn = MATH_IAbs(RmS - stream->diff);
        stream->diff = RmS;

        if (Dn > (stream->jitter + (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH)) * 2 + OS_MilliSecondsToTicks(100)) {
            stream->spike = Dn / (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) + 2;
        }

        if (stream->spike <= 0) {
            stream->jitter += (Dn - stream->jitter) >> 4;
            stream->info.jitter = stream->jitter;
        }
    }

    packet->performance_time = remote + stream->firstLocalTime + stream->info.bufferLatency;

    OSTick tick = OS_GetTick();
    if (packet->performance_time + OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) < tick) {
        return FALSE;
    }
    if (packet->performance_time > tick + OS_MilliSecondsToTicks(1000)) {
        return FALSE;
    }

    if (stream->spike <= 0) {
        int stdev = stream->jitter * 3;
        int div = stdev / (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH);

        if (stdev > stream->highThr) {
            stream->highThr = (div + 1) * (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) + default_jitter_buffer_size();
            stream->lowThr = stream->highThr - (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) * 3 / 2;
            stream->info.bufferLatency = div * (u32)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) + default_jitter_buffer_size();
            stream->thrCount = 0;
        } else if (stdev < stream->lowThr) {
            if (++stream->thrCount > 70) {
                stream->highThr = (div + 1) * (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) + default_jitter_buffer_size();
                stream->lowThr = div * (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) - (int)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) / 2;
                if (stream->lowThr < 0) {
                    stream->lowThr = 0;
                }
                stream->info.bufferLatency = div * (u32)OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) + default_jitter_buffer_size();
                stream->thrCount = 0;
            }
        }

        if (stream->info.bufferLatency > OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) * 12) {
            stream->info.bufferLatency = OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) * 12;
        }

        if (stream->resetSkew < 16) {
            stream->resetSkew++;
            stream->delayEstimate = RmS;
            stream->activeDelay = RmS;
        } else {
            stream->delayEstimate = (RmS + (stream->delayEstimate * 31)) / 32;
        }

        stream->info.clockSkew = stream->activeDelay - stream->delayEstimate;
        if (stream->info.clockSkew > (int)OS_MilliSecondsToTicks(66)) {
            adjustment = 1;
        }
        if (stream->info.clockSkew < (int)-OS_MilliSecondsToTicks(66)) {
            adjustment = -1;
            stream->skewCount++;
        }

        if (adjustment != 0) {
            stream->resetSkew = 0;
            stream->firstRemoteTime = 0;
        }
    }

    stream->spike--;
    return TRUE;
}
