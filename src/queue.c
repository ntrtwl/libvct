#include "queue.h"

#include <nitro.h>

#include "udp_buffer.h"
#include "vct.h"

static UDPPacket *sTail[VCT_MAX_AUDIO_STREAM];
static UDPPacket *sHead[VCT_MAX_AUDIO_STREAM];
static int sCount[VCT_MAX_AUDIO_STREAM];

void vct_init_audio_queue(u32 ch)
{
    OSIntrMode sIntr = OS_DisableInterrupts();
    sHead[ch] = sTail[ch] = NULL;
    sCount[ch] = 0;
    OS_RestoreInterrupts(sIntr);
}

void vct_flush_audio_queue(u32 ch)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    while (sHead[ch] != NULL) {
        UDPPacket *node = sHead[ch];
        sHead[ch] = sHead[ch]->next;
        if (sHead[ch] != NULL) {
            sHead[ch]->prev = NULL;
        }
        vct_free_packet_buffer(node);
    }

    sTail[ch] = NULL;
    sCount[ch] = 0;

    OS_RestoreInterrupts(sIntr);
}

void vct_flush_all_audio_queue(void)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    for (int ch = 0; ch < VCT_MAX_AUDIO_STREAM; ch++) {
        while (sHead[ch] != NULL) {
            UDPPacket *node = sHead[ch];
            sHead[ch] = sHead[ch]->next;
            if (sHead[ch] != NULL) {
                sHead[ch]->prev = NULL;
            }
            vct_free_packet_buffer(node);
        }

        sTail[ch] = NULL;
        sCount[ch] = 0;
    }

    OS_RestoreInterrupts(sIntr);
}

int vct_insert_audio_queue(UDPPacket *packet, u32 ch)
{
    if (packet == NULL) {
        return -1;
    }
    if (ch >= VCT_MAX_AUDIO_STREAM) {
        return -1;
    }

    OSIntrMode sIntr = OS_DisableInterrupts();

    if (sHead[ch] == NULL) {
        sHead[ch] = packet;
        packet->prev = NULL;
        sHead[ch]->next = NULL;
        sTail[ch] = sHead[ch];
    } else {
        UDPPacket *list = sTail[ch];

        while (list != NULL) {
            if (list->sequence < packet->sequence) {
                packet->prev = list;
                packet->next = list->next;
                list->next = packet;
                if (packet->next != NULL) {
                    packet->next->prev = packet;
                }
                if (list == sTail[ch]) {
                    sTail[ch] = packet;
                }
                goto inserted;
            }
            list = list->prev;
        }

        packet->prev = NULL;
        packet->next = sHead[ch];
        sHead[ch]->prev = packet;
        sHead[ch] = packet;
    }
inserted:
    sCount[ch]++;

    OS_RestoreInterrupts(sIntr);
    return sCount[ch];
}

UDPPacket *vct_top_audio_queue(u32 ch)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    if (sHead[ch] != NULL) {
        OS_RestoreInterrupts(sIntr);
        return sHead[ch];
    } else {
        OS_RestoreInterrupts(sIntr);
        return NULL;
    }
}

int vct_pop_audio_queue(u32 ch)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    if (sHead[ch] != NULL) {
        UDPPacket *node = sHead[ch]->next;
        sCount[ch]--;
        sHead[ch] = node;
        if (sHead[ch] != NULL) {
            node->prev = NULL;
        } else {
            sTail[ch] = NULL;
        }
    }

    OS_RestoreInterrupts(sIntr);
    return sCount[ch];
}

int vct_count_audio_queue(u32 ch)
{
    return sCount[ch];
}
