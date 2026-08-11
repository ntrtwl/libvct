#include "udp_buffer.h"

#include <nitro.h>

#include "vct.h"

UDPPacket *sBuffer;
UDPPacket *sFreeBuffer;

BOOL vct_init_packet_buffer(void *buffer, u32 size)
{
    u32 count = size / sizeof(UDPPacket);

    sBuffer = buffer;

    if (count < VCT_MIN_AUDIO_BUFFER_COUNT || VCT_MAX_AUDIO_BUFFER_COUNT < count) {
        return FALSE;
    }

    if (buffer == NULL) {
        return FALSE;
    }

    if ((u32)buffer & 31) {
        sBuffer = NULL;
        return FALSE;
    }

    MI_CpuClearFast(buffer, size);

    for (int i = 0; i < count - 1; i++) {
        sBuffer[i].free = &sBuffer[i + 1];
    }

    sBuffer[count - 1].free = NULL;
    sFreeBuffer = sBuffer;
    return TRUE;
}

void vct_cleanup_packet_buffer(void)
{
    sFreeBuffer = NULL;
    sBuffer = NULL;
}

UDPPacket *vct_alloc_packet_buffer(void)
{
    UDPPacket *p = NULL;
    OSIntrMode sIntr = OS_DisableInterrupts();

    if (sFreeBuffer != NULL) {
        p = sFreeBuffer;
        sFreeBuffer = sFreeBuffer->free;
    }

    OS_RestoreInterrupts(sIntr);
    return p;
}

void vct_free_packet_buffer(UDPPacket *buf)
{
    OSIntrMode sIntr = OS_DisableInterrupts();

    buf->free = sFreeBuffer;
    sFreeBuffer = buf;
    buf->prev = NULL;
    buf->next = NULL;

    OS_RestoreInterrupts(sIntr);
}
