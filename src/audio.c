#include "audio.h"

#include <dwc.h>
#include <nitro.h>

#include "adpcm.h"
#include "decoder.h"
#include "fir.h"
#include "g711.h"
#include "mixer.h"
#include "queue.h"
#include "stream.h"
#include "udp_buffer.h"
#include "vad.h"
#include "vct.h"
#include "vct_main.h"

#define VCT_SEND_BUFFER_SIZE 1120

typedef struct VCTSendInfo {
    u8 buffer[VCT_SEND_BUFFER_SIZE * 2];
    u32 read;
    u32 write;
    u8 *ptr;
    u32 bitmap;
    u32 balance;
    int numClients;
} VCTSendInfo;

typedef struct VCTCodecInfo {
    u8 chunk_size;
    u8 header_size;
} VCTCodecInfo;

static u16 sAudioSequence;
static BOOL sChangeVAD;
static BOOL sMarker;
static AdpcmState sAdpcmState;
static u32 sAudioPacketSize;
static VCTVADStatus sVADStatus;
static u32 sBitmap;
static BOOL sEnableEchoCancel;
static VCTCodec sCodec;
static u32 sNumStream;
static u32 sAudioDataSize;
static VCTSession *sAudioSession[VCT_MAX_AUDIO_STREAM];
static AudioStream sStream[VCT_MAX_AUDIO_STREAM];
static s16 sMixBuffer[VCT_AUDIO_DATA_SIZE / sizeof(s16)];
static VCTSendInfo sSendInfo ATTRIBUTE_ALIGN(32);

extern VCTi_Globals vct;

static BOOL sEnableVAD = TRUE;

static const VCTCodecInfo sCodecInfo[5] = {
    [VCT_CODEC_8BIT_RAW] = { .chunk_size = 8, .header_size = 0 },
    [VCT_CODEC_G711_ULAW] = { .chunk_size = 8, .header_size = 0 },
    [VCT_CODEC_2BIT_ADPCM] = { .chunk_size = 2, .header_size = sizeof(AdpcmState) },
    [VCT_CODEC_3BIT_ADPCM] = { .chunk_size = 3, .header_size = sizeof(AdpcmState) },
    [VCT_CODEC_4BIT_ADPCM] = { .chunk_size = 4, .header_size = sizeof(AdpcmState) }
};

static const u8 sTransceiverBalance[VCT_MAX_TRANSCEIVER_CLIENT - 1][3] = {
    { 1, 0, 0 },
    { 1, 1, 0 },
    { 1, 1, 1 },
    { 1, 1, 2 },
    { 1, 2, 2 },
    { 2, 2, 2 },
    { 2, 2, 3 },
};

static inline void vct_reset_send_info(void)
{
    sSendInfo.read = 0;
    sSendInfo.write = 0;
    sSendInfo.bitmap = 0;
    sSendInfo.ptr = 0;
    sSendInfo.balance = 0;
    sSendInfo.numClients = 0;
}

BOOL vct_init_audio(void)
{
    sAudioSequence = OS_GetTickLo();
    sEnableVAD = TRUE;
    sNumStream = 0;
    sBitmap = 0;

    if (vct.SSPMode == VCT_MODE_PHONE) {
        sCodec = VCT_CODEC_8BIT_RAW;
    } else {
        sCodec = VCT_CODEC_4BIT_ADPCM;
    }

    sAudioDataSize = VCT_AUDIO_DATA_SIZE;
    sAudioPacketSize = (sCodecInfo[sCodec].chunk_size * VCT_AUDIO_FRAME_LENGTH) + sCodecInfo[sCodec].header_size + sizeof(AudioHeader);

    vct_reset_send_info();
    vct_init_vad();

    for (u32 i = 0; i < VCT_MAX_AUDIO_STREAM; i++) {
        sAudioSession[i] = NULL;
        vct_init_audio_queue(i);
        vct_init_stream(&sStream[i], i, -1);
    }

    vct_init_decoder();
    InitFIRFilter();

    return TRUE;
}

void vct_finish_audio(void)
{
    sNumStream = 0;
}

BOOL VCT_StartStreaming(VCTSession *session)
{
    u32 ch;

    if (session == NULL) {
        return FALSE;
    }
    if (sNumStream == VCT_MAX_AUDIO_STREAM) {
        return FALSE;
    }
    if (session->state != VCT_STATE_TALKING && session->state != VCT_STATE_CONNECTED) {
        return FALSE;
    }

    for (ch = 0; ch < VCT_MAX_AUDIO_STREAM; ch++) {
        if (sAudioSession[ch] == session) {
            return TRUE;
        }
    }

    for (ch = 0; ch < VCT_MAX_AUDIO_STREAM; ch++) {
        if (sAudioSession[ch] == NULL) {
            sAudioSession[ch] = session;
            break;
        }
    }

    sNumStream++;
    sBitmap |= session->aidBitmap;
    vct_init_stream(&sStream[ch], ch, session->talking);
    vct_init_audio_queue(ch);

    if (session->state != VCT_STATE_TALKING) {
        return TRUE;
    }

    if (sNumStream == 1) {
        VCT_ResetVAD();
        sAudioSequence = OS_GetTickLo();
        sAdpcmState.prevSample = 0;
        sAdpcmState.prevIndex = 0;
        sMarker = TRUE;
        vct_reset_send_info();
    }

    return TRUE;
}

void VCT_StopStreaming(VCTSession *session)
{
    for (u32 ch = 0; ch < VCT_MAX_AUDIO_STREAM; ch++) {
        if (sAudioSession[ch] == session) {
            sAudioSession[ch] = NULL;
            sNumStream--;
            vct_reset_stream(&sStream[ch]);
            vct_flush_audio_queue(ch);
            vct_flush_decoder(ch);
            sBitmap &= ~session->aidBitmap;
            break;
        }
    }

    if (sNumStream != 0) {
        return;
    }

    sMarker = TRUE;
    VCT_ResetVAD();
    sBitmap = 0;
}

BOOL VCT_SendAudio(void *audio_data, u32 length)
{
    AudioHeader *header;
    u32 ch;
    BOOL flag = FALSE;

    if (length != sAudioDataSize) {
        return FALSE;
    }
    if (sNumStream == 0) {
        return FALSE;
    }

    for (ch = 0; ch < VCT_MAX_AUDIO_STREAM; ch++) {
        if (sAudioSession[ch] != NULL && sAudioSession[ch]->state == VCT_STATE_TALKING) {
            flag = TRUE;
            break;
        }
    }

    if (!flag) {
        return FALSE;
    }

    u8 *ptr = &sSendInfo.buffer[(sSendInfo.write & 1) ? 0 : VCT_SEND_BUFFER_SIZE];

    header = (AudioHeader *)ptr;
    header->magic = VCT_MAGIC_PACKET_HEADER;

    if (sCodec >= VCT_CODEC_2BIT_ADPCM) {
        MI_CpuCopyFast(audio_data, ptr + sizeof(AudioHeader) + sizeof(AdpcmState), length);
    } else {
        MI_CpuCopyFast(audio_data, ptr + sizeof(AudioHeader), length);
    }

    header->header = VCT_HEADER_AUDIO;
    header->sequence = sAudioSequence++;
    header->timestamp = OS_GetTick() / 64;
    sSendInfo.write++;
    return TRUE;
}

static inline u32 calc_equation(Equation *eq, u32 sample)
{
    u32 D = sample - eq->prev;
    eq->prev = sample;
    u32 ret = eq->prev + ((D - eq->prev) >> 4);
    eq->sum = ret;
    return ret;
}

static inline void vct_recover_drop(void *previous, void *missing, u32 length)
{
    MI_CpuCopyFast(previous, missing, length);
}

static BOOL VCTi_ReceiveAudioData(void *audio_data, u32 length, u32 channel, u32 *outAID)
{
    int diff;
    UDPPacket *node;
    AudioStream *stream = &sStream[channel];

    BOOL calc_skew = TRUE;
    diff = vct_count_audio_queue(channel);
    while (diff != 0) {
        OSTick tick = OS_GetTick();
        node = vct_top_audio_queue(channel);
        if (node != NULL) {
            if (node->performance_time > tick + OS_MicroSecondsToTicks(100)) {
                if (node->marker == 0 && stream->prevBuffer != NULL) {
                    stream->info.recoverCount++;
                    vct_recover_drop(stream->prevBuffer->samples, audio_data, length);
                    vct_free_packet_buffer(stream->prevBuffer);
                    stream->prevBuffer = NULL;
                    if (outAID != NULL) {
                        *outAID |= 1 << node->remote;
                    }
                    return TRUE;
                }
                return FALSE;
            }

            if (tick > node->performance_time + OS_MilliSecondsToTicks(VCT_AUDIO_FRAME_LENGTH) + OS_MicroSecondsToTicks(100)) {
                stream->playSequence = node->sequence;
                diff = vct_save_previous_buffer(stream, node);
                vct_top_audio_queue(channel);
                calc_skew = FALSE;
                continue;
            }

            if (stream->playSequence + 1 != node->sequence && stream->playSequence != 0
                && stream->playSequence <= node->sequence && node->marker == 0) {
                stream->info.dropCount++;
            }

            stream->playSequence = node->sequence;

            if (calc_skew && stream->skewCount != 0) {
                diff = vct_save_previous_buffer(stream, node);
                vct_top_audio_queue(channel);
                stream->skewCount--;
                continue;
            }

            stream->info.internalLatency = calc_equation(&stream->latency, tick - node->receive_time);
            MI_CpuCopyFast(node->samples, audio_data, length);
            vct_save_previous_buffer(stream, node);

            if (outAID != NULL) {
                *outAID |= 1 << node->remote;
            }

            return TRUE;
        }

        stream->info.bufferCount = diff;
        return FALSE;
    }

    if (stream->prevBuffer != NULL) {
        vct_free_packet_buffer(stream->prevBuffer);
        stream->prevBuffer = NULL;
    }

    return FALSE;
}

static inline BOOL vct_receive_mixed_audio(void *audio_data, u32 length, u32 *outAID)
{
    u32 i;
    BOOL flag = FALSE;

    if (length == sAudioDataSize && sNumStream != 0) {
        for (i = 0; i < VCT_MAX_AUDIO_STREAM; i++) {
            if (VCTi_ReceiveAudioData(audio_data, length, i, outAID) == TRUE) {
                flag = TRUE;
                break;
            }
        }

        if (flag) {
            i++;
            if (i == VCT_MAX_AUDIO_STREAM) {
                flag = TRUE;
            } else {
                for (u32 j = i; j < VCT_MAX_AUDIO_STREAM; j++) {
                    if (VCTi_ReceiveAudioData(sMixBuffer, length, j, outAID) == TRUE) {
                        vct_mix_audio(audio_data, sMixBuffer, audio_data, length);
                    }
                }
            }
        } else {
            MI_CpuClearFast(audio_data, length);
        }
    } else {
        MI_CpuClearFast(audio_data, length);
        return FALSE;
    }

    return flag;
}

BOOL VCT_ReceiveAudio(void *audio_data, u32 length, u32 *outAID)
{
    if (outAID != NULL) {
        *outAID = 0;
    }

    BOOL ret = vct_receive_mixed_audio(audio_data, length, outAID);

    if (sEnableEchoCancel) {
        vct_set_speaker_samples(audio_data, length);
    }

    return ret;
}

BOOL VCT_SetCodec(VCTCodec codec)
{
    if (codec >= VCT_CODEC_END) {
        return FALSE;
    }
    if (vct.SSPMode != VCT_MODE_PHONE && (codec == VCT_CODEC_8BIT_RAW || codec == VCT_CODEC_G711_ULAW)) {
        return FALSE;
    }

    sAudioPacketSize = (sCodecInfo[codec].chunk_size * VCT_AUDIO_FRAME_LENGTH) + sCodecInfo[codec].header_size + sizeof(AudioHeader);
    sCodec = codec;
    sAdpcmState.prevSample = 0;
    sAdpcmState.prevIndex = 0;
    return TRUE;
}

void VCT_EnableVAD(BOOL flag)
{
    sEnableVAD = flag;
    VCT_ResetVAD();

    if (!flag) {
        sChangeVAD = TRUE;
    }
}

void VCT_EnableEchoCancel(BOOL flag)
{
    sEnableEchoCancel = flag;
}

u8 *vct_prepare_send_buffer(void)
{
    u8 *ptr;
    u8 *audio_data;
    AudioHeader *header;
    u32 micScale;
    u32 micGain;
    VCTCodec codec = sCodec;

    u32 diff = sSendInfo.write - sSendInfo.read;
    if (diff > 1) {
        sSendInfo.read = sSendInfo.write - 1;
        diff = 1;
    }

    if (diff == 0) {
        return NULL;
    }

    ptr = &sSendInfo.buffer[(sSendInfo.read & 1) ? 0 : VCT_SEND_BUFFER_SIZE];
    header = (AudioHeader *)ptr;

    audio_data = ptr + sizeof(AudioHeader);
    if (codec >= VCT_CODEC_2BIT_ADPCM) {
        audio_data += sizeof(AdpcmState);
    }

    micScale = HPFFilter(audio_data, sAudioDataSize);
    micGain = SQRT(micScale);

    if (sEnableEchoCancel) {
        vct_process_fir(audio_data, sAudioDataSize, micGain);
    }

    header->codec = codec;

    if (sEnableVAD) {
        sVADStatus = VCTi_GetVADStatus(micScale, micGain, 0);
        switch (sVADStatus) {
        case VCT_VAD_LOST:
            header->header = VCT_HEADER_AUDIO_END;
            break;
        case VCT_VAD_NONE:
            sSendInfo.read++;
            return NULL;
        case VCT_VAD_DETECT:
            header->marker |= 0x80;
            break;
        }
    }

    if (!sEnableVAD && sChangeVAD) {
        header->marker |= 0x80;
        sChangeVAD = FALSE;
    }

    if (sMarker) {
        header->marker |= 0x80;
        sMarker = FALSE;
    }

    if (codec >= VCT_CODEC_2BIT_ADPCM) {
        if (header->marker & 0x80) {
            sAdpcmState.prevSample = 0;
            sAdpcmState.prevIndex = 0;
        }
        vct_encode_adpcm(audio_data - sizeof(AdpcmState), audio_data, sAudioDataSize / sizeof(s16), &sAdpcmState, codec);
    } else if (codec == VCT_CODEC_G711_ULAW) {
        vct_encode_g711_ulaw(audio_data, audio_data, sAudioDataSize / sizeof(s16));
    } else if (codec == VCT_CODEC_8BIT_RAW) {
        vct_encode_8bit_raw(audio_data, audio_data, sAudioDataSize / sizeof(s16));
    }

    sSendInfo.bitmap = sBitmap;
    sSendInfo.bitmap &= ~(1 << vct.myAID);
    sSendInfo.numClients = MATH_CountPopulation(sSendInfo.bitmap) - 1;

    if (sSendInfo.numClients > VCT_MAX_TRANSCEIVER_CLIENT - 1) {
        return NULL;
    }

    sSendInfo.balance = 0;
    return ptr;
}

BOOL vct_handle_audio(u8 aid, u8 *buffer, u32 size, OSTick tick)
{
    UDPPacket *p;
    AudioStream *stream;
    u8 *tmp_buffer;
    VCTCodec codec;

    AudioHeader *header = (AudioHeader *)buffer;

    stream = NULL;
    if (sNumStream == 0) {
        return FALSE;
    }

    for (u32 ch = 0; ch < VCT_MAX_AUDIO_STREAM; ch++) {
        if (aid == sStream[ch].aid) {
            stream = &sStream[ch];
            break;
        }
    }

    if (stream == NULL) {
        return FALSE;
    }

    if (header->header == VCT_HEADER_AUDIO_END) {
        stream->comfortLevel = header->sequence;
        stream->firstRemoteTime = 0;
        stream->resetSkew = 1;
        return FALSE;
    } else if (header->header != VCT_HEADER_AUDIO) {
        return FALSE;
    }

    codec = header->codec & 0x7F;
    stream->info.codec = codec;
    if (codec >= VCT_CODEC_END) {
        return FALSE;
    }

    u32 packet_size = (sCodecInfo[codec].chunk_size * VCT_AUDIO_FRAME_LENGTH) + sCodecInfo[codec].header_size + sizeof(AudioHeader);
    if (size != packet_size) {
        return FALSE;
    }

    u32 aidBit;
    if (aid != 0) {
        aidBit = 1 << aid;
    } else {
        aidBit = 1;
    }

    if (!(sBitmap & aidBit)) {
        return FALSE;
    }

    p = vct_alloc_packet_buffer();
    if (p == NULL) {
        vct_flush_audio_queue(stream->channel);
        vct_flush_decoder(stream->channel);
        p = vct_alloc_packet_buffer();
        if (p == NULL) {
            vct_flush_all_audio_queue();
            vct_flush_all_decoder();
            p = vct_alloc_packet_buffer();
        }
        if (p == NULL) {
            return FALSE;
        }
    }

    buffer += sizeof(AudioHeader);
    tmp_buffer = &p->buffer[VCT_AUDIO_DATA_SIZE] - (size - sizeof(AudioHeader));
    MI_CpuCopy8(buffer, tmp_buffer, size - sizeof(AudioHeader));

    p->channel = stream->channel;
    p->codec = codec;
    p->samples = tmp_buffer;
    p->length = size - sizeof(AudioHeader);
    p->receive_time = tick;
    p->remote = aid;
    p->marker = header->marker & 0x80;
    p->timestamp = header->timestamp;
    p->sequence = header->sequence;

    if (!vct_calc_jitter_and_skew(stream, p)) {
        vct_free_packet_buffer(p);
        return FALSE;
    }

    vct_insert_decoder(p);
    return TRUE;
}

int vct_decode_audio_buffer(void)
{
    UDPPacket *p;
    int i;
    int n;
    u32 ch;

    p = vct_top_decoder();
    if (p == NULL) {
        return 0;
    }

    OSIntrMode sIntr = OS_DisableInterrupts();
    ch = p->channel;
    n = vct_count_decoder_queue(ch);
    for (i = 0; i < n; i++) {
        p = vct_get_packet_from_decoder(ch);
        if (p->codec >= VCT_CODEC_2BIT_ADPCM) {
            vct_decode_adpcm(p->samples, p->buffer, p->length, p->codec);
        } else if (p->codec == VCT_CODEC_G711_ULAW) {
            vct_decode_g711_ulaw(p->samples, p->buffer, p->length);
        } else {
            vct_decode_8bit_raw(p->samples, p->buffer, p->length);
        }

        p->samples = p->buffer;
        vct_remove_packet_from_decoder(p);
        if (vct_insert_audio_queue(p, p->channel) < 0) {
            vct_free_packet_buffer(p);
        }
    }

    OS_RestoreInterrupts(sIntr);
    return n;
}

static inline void vct_send_audio_buffer(void)
{
    int loop = 1;
    if (vct.SSPMode == VCT_MODE_TRANSCEIVER) {
        loop = sTransceiverBalance[sSendInfo.numClients][sSendInfo.balance++];
    }

    for (int i = 0; i < loop; i++) {
        u32 aid = MATH_CountLeadingZeros(sSendInfo.bitmap);
        if (aid == 32) {
            break;
        }
        sSendInfo.bitmap &= ~(0x80000000 >> aid);
        aid = 31 - aid;

        DWC_SendUnreliable(aid, sSendInfo.ptr, sAudioPacketSize);
        if (sSendInfo.bitmap == 0) {
            sSendInfo.read++;
            sSendInfo.ptr = NULL;
            sSendInfo.bitmap = 0;
            break;
        }
    }
}

BOOL vct_flush_send_buffer(void)
{
    if (sNumStream == 0) {
        return TRUE;
    }

    if (sSendInfo.ptr == NULL) {
        sSendInfo.ptr = vct_prepare_send_buffer();
        if (sSendInfo.ptr != NULL && sEnableEchoCancel) {
            return FALSE;
        } else {
            return TRUE;
        }
    }

    vct_send_audio_buffer();
    return TRUE;
}
