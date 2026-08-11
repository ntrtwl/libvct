#ifndef VCT_SSP_H
#define VCT_SSP_H
#include <nitro.h>

#include "vct.h"
#include "vct_main.h"

typedef struct SSPHeader {
    u32 magic;
    u8 method;
    u8 version;
    u8 code;
    u8 reserved;
    u8 from;
    u8 to;
    u8 info;
    u8 talking;
    u32 aidBitmap;
} SSPHeader;

#define VCT_SSP_HEADER_VERSION 0x10

enum VCTMethod {
    VCT_METHOD_REQUEST = 0xFF,
    VCT_METHOD_RESPONSE = 0,
};

int vct_build_request_data(VCTSession *session, VCTRequestCode request, void *buffer, u32 size);
int vct_build_response_data(VCTSession *session, VCTResponseCode response, void *buffer, u32 size);
BOOL vct_init_ssp(VCTConfig *config);
void vct_finish_ssp(void);
void vct_check_timeout(void);
BOOL vct_handle_ssp(u8 aid, u8 *buffer, int size, VCTResult *result);

#endif // VCT_SSP_H
