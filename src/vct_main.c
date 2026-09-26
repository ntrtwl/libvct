#include "vct_main.h"

#include <dwc.h>
#include <nitro.h>

#include "audio.h"
#include "ssp.h"
#include "stream.h"
#include "udp_buffer.h"
#include "vct.h"

#include <nitro/version_begin.h>
static char id_string[] = SDK_MIDDLEWARE_STRING("Abiosso", "libVCT 1.3.1");
#include <nitro/version_end.h>

enum HandleDataResult {
    HANDLE_DATA_RESULT_NO_CALLBACK = 0,
    HANDLE_DATA_RESULT_CALLBACK,
    HANDLE_DATA_RESULT_NOT_VCT_DATA,
};

#ifndef SDK_FINALROM
static VCTReportLevel sReportLevel = VCT_REPORTLEVEL_ALL;
#endif

static int initialized = FALSE;
static u32 sRecvPerFrame = 0;

VCTi_Globals vct;

BOOL VCT_Init(VCTConfig *config)
{
    SDK_USING_MIDDLEWARE(id_string);

    if (config == NULL) {
        return FALSE;
    }
    if (initialized) {
        return TRUE;
    }
    if (config->mode != VCT_MODE_PHONE && config->mode != VCT_MODE_TRANSCEIVER && config->mode != VCT_MODE_CONFERENCE) {
        return FALSE;
    }
    if (config->callback == NULL) {
        return FALSE;
    }
    if (config->audioBuffer == NULL || config->audioBufferSize == 0) {
        return FALSE;
    }
    if ((int)config->audioBuffer % 32 != 0) {
        return FALSE;
    }

    MI_CpuClear8(&vct, sizeof(VCTi_Globals));
    vct.callback = config->callback;
    vct.userData = config->userData;
    vct.SSPMode = config->mode;
    vct.listenMode = LISTEN_MODE_CLIENT;

    if (config->aid >= DWC_MAX_CONNECTIONS) {
        return FALSE;
    }

    vct.myAID = config->aid;

    if (!vct_init_packet_buffer(config->audioBuffer, config->audioBufferSize)) {
        vct_cleanup_packet_buffer();
        return FALSE;
    }

    if (!vct_init_ssp(config)) {
        vct_cleanup_packet_buffer();
        return FALSE;
    }

    if (!vct_init_audio()) {
        vct_finish_ssp();
        vct_cleanup_packet_buffer();
        return FALSE;
    }

    initialized = TRUE;
    return TRUE;
}

void VCT_Cleanup(void)
{
    vct_finish_audio();
    vct_finish_ssp();
    vct_cleanup_packet_buffer();
    vct.SSPMode = VCT_MODE_NULL;
    initialized = FALSE;
}

void VCT_SetTransceiverMode(BOOL server_or_client)
{
    VCTListenMode mode;

    if (server_or_client != 0) {
        mode = LISTEN_MODE_SERVER;
    } else {
        mode = LISTEN_MODE_CLIENT;
    }

    vct.listenMode = mode;
}

void VCT_Main(void)
{
    static u32 count = 0;

    if (!initialized) {
        return;
    }

    count++;

    if (!(count % 16)) {
        vct_check_timeout();
    }

    if (!vct_flush_send_buffer()) {
        return;
    }

    vct_decode_audio_buffer();
}

BOOL VCT_HandleData(u8 aid, u8 *buffer, int size)
{
    VCTResult result;
    int ret = VCTi_HandleData(aid, buffer, size, &result);
    switch (ret) {
    case HANDLE_DATA_RESULT_CALLBACK:
        vct.callback(aid, result.event, result.session, vct.userData);
        return TRUE;
    case HANDLE_DATA_RESULT_NOT_VCT_DATA:
        return FALSE;
    case HANDLE_DATA_RESULT_NO_CALLBACK:
    default:
        return TRUE;
    }
}

int VCTi_HandleData(u8 aid, u8 *buffer, int size, VCTResult *result)
{
    if (buffer == NULL) {
        return HANDLE_DATA_RESULT_NO_CALLBACK;
    }
    if (((AudioHeader *)buffer)->magic != VCT_MAGIC_PACKET_HEADER) {
        return HANDLE_DATA_RESULT_NOT_VCT_DATA;
    }
    if (vct.SSPMode == VCT_MODE_NULL || !initialized) {
        return HANDLE_DATA_RESULT_NO_CALLBACK;
    }

    result->event = 0;
    result->session = NULL;
    OSTick tick = OS_GetTick();

    if ((buffer[4] & 0xF0) == VCT_HEADER_AUDIO) {
        sRecvPerFrame++;
        vct_handle_audio(aid, buffer, size, tick);
        return HANDLE_DATA_RESULT_NO_CALLBACK;
    } else if (((SSPHeader *)buffer)->method == VCT_METHOD_RESPONSE || ((SSPHeader *)buffer)->method == VCT_METHOD_REQUEST) {
        if (vct_handle_ssp(aid, buffer, size, result)) {
            return HANDLE_DATA_RESULT_CALLBACK;
        } else {
            return HANDLE_DATA_RESULT_NO_CALLBACK;
        }
    } else {
        return HANDLE_DATA_RESULT_NO_CALLBACK;
    }
}

#ifndef SDK_FINALROM
void VCT_SetReportLevel(VCTReportLevel level)
{
    sReportLevel = level
}
#endif
