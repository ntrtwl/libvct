#include "decoder.h"

#include <nitro.h>

#include "udp_buffer.h"
#include "vct.h"

static int sCount[VCT_MAX_AUDIO_STREAM];
static UDPPacket *sHead;
static UDPPacket *sTail;

void vct_init_decoder(void)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    sTail = NULL;
    sHead = NULL;

    for (int i = 0; i < VCT_MAX_AUDIO_STREAM; i++) {
        sCount[i] = 0;
    }

    OS_RestoreInterrupts(sIntr);
}

void vct_flush_decoder(u32 ch)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    UDPPacket *p = vct_get_packet_from_decoder(ch);
    while (p != 0) {
        vct_remove_packet_from_decoder(p);
        vct_free_packet_buffer(p);
        p = vct_get_packet_from_decoder(ch);
    }

    sCount[ch] = 0;
    OS_RestoreInterrupts(sIntr);
}

void vct_flush_all_decoder(void)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    UDPPacket *node = sHead;
    while (node != NULL) {
        UDPPacket *p = node;
        node = node->next;
        vct_free_packet_buffer(p);
    }

    sHead = NULL;
    sTail = NULL;

    for (int i = 0; i < VCT_MAX_AUDIO_STREAM; i++) {
        sCount[i] = 0;
    }

    OS_RestoreInterrupts(sIntr);
}

int vct_insert_decoder(UDPPacket *packet)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    if (sHead == NULL) {
        sHead = packet;
        packet->prev = NULL;
        sHead->next = NULL;
        sTail = sHead;
    } else {
        packet->prev = sTail;
        packet->next = NULL;
        sTail->next = packet;
        sTail = packet;
    }

    sCount[packet->channel]++;
    OS_RestoreInterrupts(sIntr);
    return sCount[packet->channel];
}

UDPPacket *vct_top_decoder(void)
{
    return sHead;
}

int vct_count_decoder_queue(u32 ch)
{
    return sCount[ch];
}

UDPPacket *vct_get_packet_from_decoder(u32 ch)
{
    UDPPacket *node = sHead;
    OSIntrMode sIntr = OS_DisableInterrupts();

    while (node != NULL) {
        if (node->channel == ch) {
            OS_RestoreInterrupts(sIntr);
            return node;
        } else {
            node = node->next;
        }
    }

    OS_RestoreInterrupts(sIntr);
    return NULL;
}

int vct_remove_packet_from_decoder(UDPPacket *packet)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    if (packet->prev != NULL) {
        packet->prev->next = packet->next;
    } else {
        sHead = packet->next;
        if (sHead != NULL) {
            sHead->prev = NULL;
        }
    }

    if (packet->next != NULL) {
        packet->next->prev = packet->prev;
    } else {
        sTail = packet->prev;
        if (sTail != NULL) {
            sTail->next = NULL;
        }
    }

    sCount[packet->channel]--;
    OS_RestoreInterrupts(sIntr);
    return sCount[packet->channel];
}
