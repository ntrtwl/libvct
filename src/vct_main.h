#ifndef VCT_MAIN_H
#define VCT_MAIN_H
#include <nitro.h>

#include "vct.h"

typedef enum {
    LISTEN_MODE_CLIENT = 0,
    LISTEN_MODE_SERVER,
} VCTListenMode;

typedef struct VCTi_Globals {
    u8 myAID;
    u8 serverAID;
    VCTEventCallback callback;
    void *userData;
    VCTListenMode listenMode;
    VCTMode SSPMode;
    int autoResponse;
} VCTi_Globals;

typedef struct VCTResult {
    VCTEvent event;
    VCTSession *session;
} VCTResult;

int VCTi_HandleData(u8 aid, u8 *buffer, int size, VCTResult *result);

#endif
