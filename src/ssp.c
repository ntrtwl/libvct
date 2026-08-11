#include "ssp.h"

#include <dwc.h>
#include <nitro.h>

#include "vct.h"
#include "vct_main.h"

typedef struct SessionInfo {
    VCTSession *free;
    VCTSession *use;
    MATHRandContext16 rand;
    VCTSession tmp;
    VCTSession trans;
} SessionInfo;

typedef enum {
    VCT_NOTIFY_FREE = 0,
    VCT_NOTIFY_BUSY,
} VCTNotify;

static u32 sNotifyAID;
static u32 sNumOfNotifyAID;
static OSTick sTransTalkTime;
static SessionInfo sSession;

static OSTick sTransceiverLimit = OS_SecondsToTicks(30);

extern VCTi_Globals vct;

static const char *vct_request_name[5] = {
    "Invite",
    "Bye",
    "Cancel",
    "Notify",
    "Contact"
};

static const char *vct_response_name[6] = {
    "OK",
    "BadRequest",
    "NotAcceptable",
    "BusyHere",
    "RequestTerminated",
    "Decline",
};

#define OFFSET(response) (response + VCT_REQUEST_END)

#define VCT_NO_CHANGE    -1
#define VCT_STATE_ERROR  -2
#define VCT_CONTACT_FAIL -3

static const s8 sSendState[6][11] = {
    [VCT_STATE_INIT] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_OUTGOING,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_OUTGOING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_TALKING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_CONNECTED] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_INCOMING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_TALKING,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_INIT,
    },
    [VCT_STATE_DISCONNECTING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    }
};

static const s8 sTransSendState[6][11] = {
    [VCT_STATE_INIT] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_OUTGOING,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_OUTGOING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CANCEL] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_TALKING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_CONNECTED] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    },
    [VCT_STATE_INCOMING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_CONNECTED,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_INIT,
    },
    [VCT_STATE_DISCONNECTING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_ERROR,
    }
};

static const s8 sRecvState[6][11] = {
    [VCT_STATE_INIT] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_INCOMING,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_OUTGOING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_TALKING,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_INIT,
    },
    [VCT_STATE_TALKING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CANCEL] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_CONNECTED] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_STATE_ERROR,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_INCOMING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_DISCONNECTING,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_DISCONNECTING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_ERROR,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
};

static const s8 sTransRecvState[6][11] = {
    [VCT_STATE_INIT] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_STATE_CONNECTED,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_OUTGOING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_CONTACT_FAIL,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_TALKING,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_STATE_INIT,
    },
    [VCT_STATE_TALKING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_CONTACT_FAIL,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_CONNECTED] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_INIT,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_CONTACT_FAIL,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_INCOMING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_CONTACT_FAIL,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    },
    [VCT_STATE_DISCONNECTING] = {
        [VCT_REQUEST_INVITE] = VCT_STATE_ERROR,
        [VCT_REQUEST_BYE] = VCT_STATE_ERROR,
        [VCT_REQUEST_CANCEL] = VCT_STATE_ERROR,
        [VCT_REQUEST_CONTACT] = VCT_CONTACT_FAIL,
        [VCT_REQUEST_NOTIFY] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_OK)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_BAD_REQUEST)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_NOT_ACCEPTABLE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_BUSY_HERE)] = VCT_NO_CHANGE,
        [OFFSET(VCT_RESPONSE_TERMINATED)] = VCT_STATE_INIT,
        [OFFSET(VCT_RESPONSE_DECLINE)] = VCT_NO_CHANGE,
    }
};

static const u8 sRequestEvent[] = {
    [VCT_REQUEST_INVITE] = VCT_EVENT_INCOMING,
    [VCT_REQUEST_BYE] = VCT_EVENT_RESPONDBYE,
    [VCT_REQUEST_CANCEL] = VCT_EVENT_CANCEL,
    [VCT_REQUEST_CONTACT] = VCT_EVENT_CONTACT,
    [VCT_REQUEST_NOTIFY] = VCT_EVENT_NONE,
};

static const u8 sResponseEvent[] = {
    [VCT_RESPONSE_OK] = VCT_EVENT_CONNECTED,
    [VCT_RESPONSE_BAD_REQUEST] = VCT_EVENT_ABORT,
    [VCT_RESPONSE_NOT_ACCEPTABLE] = VCT_EVENT_REJECT,
    [VCT_RESPONSE_BUSY_HERE] = VCT_EVENT_BUSY,
    [VCT_RESPONSE_TERMINATED] = VCT_EVENT_DISCONNECTED,
    [VCT_RESPONSE_DECLINE] = VCT_EVENT_REJECT,
};

static VCTSession *vct_create_session_impl(u8 aid);
static int vct_build_ssp_header(VCTSession *session, SSPHeader *header, u8 code);
static int vct_check_send_state(VCTSession *session, SSPHeader *header);
static BOOL vct_check_multisession(VCTSession *session);
static int vct_request_impl(VCTSession *session, VCTRequestCode request);
static int vct_response_impl(VCTSession *session, VCTResponseCode response);
static void vct_init_session(VCTSession *session, u8 aid);
static BOOL vct_send_ssp(VCTSession *session, SSPHeader *header);
static int vct_ssp_auto_response(VCTRequestCode request);
static VCTSession *vct_find_session(u8 aid);
static int vct_send_notify(VCTNotify notify);
static BOOL vct_handle_ssp_client(SSPHeader *header, VCTResult *result);
static BOOL vct_handle_ssp_server(SSPHeader *header, VCTResult *result);
static BOOL vct_response_reject(SSPHeader *header, VCTResult *result);
static int vct_send_notify_impl(VCTSession *session, SSPHeader *header);
static BOOL vct_handle_notify(SSPHeader *header, VCTResult *result);

static inline u32 vct_set_remote_aid(VCTSession *session, u32 aidBitmap)
{
    session->aidBitmap = aidBitmap;
}

static inline BOOL vct_send_ssp_impl(u8 aid, SSPHeader *header)
{
    return DWC_SendReliable(aid, header, sizeof(SSPHeader));
}

VCTSession *VCT_CreateSession(u8 aid)
{
    if (vct.SSPMode != VCT_MODE_NULL && vct.SSPMode != VCT_MODE_TRANSCEIVER) {
        return vct_create_session_impl(aid);
    } else {
        return NULL;
    }
}

BOOL VCT_DeleteSession(VCTSession *session)
{
    VCTSession *prev = NULL;
    VCTSession *node = sSession.use;
    if (session == NULL) {
        return FALSE;
    }
    if (vct.SSPMode == VCT_MODE_TRANSCEIVER) {
        if (session == &sSession.trans) {
            sSession.trans.mode = VCT_MODE_NULL;
        }
        return TRUE;
    }
    while (node != NULL) {
        if (node == session) {
            session->mode = VCT_MODE_NULL;
            if (prev != NULL) {
                prev->next = session->next;
            } else {
                if (node->next != NULL) {
                    sSession.use = node->next;
                } else {
                    sSession.use = NULL;
                }
            }
            session->next = sSession.free;
            sSession.free = session;
            return TRUE;
        }
        prev = node;
        node = node->next;
    }
    return FALSE;
}

int vct_build_request_data(VCTSession *session, VCTRequestCode request, void *buffer, u32 size)
{
    SSPHeader *header = buffer;
    if (session == NULL) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (size < sizeof(SSPHeader)) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (vct.listenMode == LISTEN_MODE_SERVER) {
        return VCT_ERROR_BAD_REQUEST;
    }
    if (request < 0 || request >= VCT_REQUEST_END) {
        return VCT_ERROR_BAD_REQUEST;
    }
    header->method = VCT_METHOD_REQUEST;
    vct_build_ssp_header(session, header, request);
    if (vct_check_send_state(session, header) == 0) {
        return VCT_ERROR_BAD_REQUEST;
    }
    if (vct.SSPMode == VCT_MODE_PHONE && request == VCT_REQUEST_INVITE && !vct_check_multisession(session)) {
        return VCT_ERROR_BAD_REQUEST;
    }
    return VCT_ERROR_NONE;
}

int VCT_Request(VCTSession *session, VCTRequestCode request)
{
    if (vct.SSPMode == VCT_MODE_TRANSCEIVER) {
        return VCT_ERROR_BAD_REQUEST;
    }

    return vct_request_impl(session, request);
}

int vct_build_response_data(VCTSession *session, VCTResponseCode response, void *buffer, u32 size)
{
    SSPHeader *header = buffer;

    if (session == NULL) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (size < sizeof(SSPHeader)) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (response < 0 || response >= VCT_RESPONSE_END) {
        return VCT_ERROR_BAD_REQUEST;
    }

    header->method = VCT_METHOD_RESPONSE;
    vct_build_ssp_header(session, header, response);
    if (response == VCT_RESPONSE_OK) {
        if (vct.SSPMode == VCT_MODE_PHONE && session->state == VCT_STATE_INCOMING && !vct_check_multisession(session)) {
            return VCT_ERROR_BAD_REQUEST;
        }

        vct_set_remote_aid(session, session->aid != 0 ? 1 << session->aid : 1);
    }

    if (vct_check_send_state(session, header) == 0) {
        return VCT_ERROR_BAD_REQUEST;
    }

    return VCT_ERROR_NONE;
}

int VCT_Response(VCTSession *session, VCTResponseCode response)
{
    if (vct.SSPMode == VCT_MODE_TRANSCEIVER) {
        return VCT_ERROR_BAD_REQUEST;
    }
    return vct_response_impl(session, response);
}

int VCT_Contact(VCTSession **outSession)
{
    int ret;

    if (vct.SSPMode != VCT_MODE_TRANSCEIVER) {
        return VCT_ERROR_BAD_REQUEST;
    }
    if (sSession.trans.mode == VCT_MODE_NULL) {
        vct_init_session(&sSession.trans, vct.serverAID);
    } else {
        return VCT_ERROR_TRANSCEIVER_BUSY;
    }
    if (vct.listenMode == LISTEN_MODE_CLIENT) {
        SSPHeader header;
        ret = vct_build_request_data(&sSession.trans, VCT_REQUEST_CONTACT, &header, sizeof(SSPHeader));
        if (ret != VCT_ERROR_NONE) {
            return ret;
        }
        if (vct_send_ssp(&sSession.trans, &header)) {
            ret = VCT_ERROR_NONE;
        } else {
            ret = VCT_ERROR_SEND_FAIL;
        }
    } else {
        SSPHeader header;
        header.method = VCT_METHOD_REQUEST;
        header.code = VCT_REQUEST_CONTACT;
        if (vct_check_send_state(&sSession.trans, &header) == 0) {
            return VCT_ERROR_BAD_REQUEST;
        }

        ret = vct_ssp_auto_response(VCT_REQUEST_CONTACT);
    }
    if (outSession != NULL) {
        *outSession = &sSession.trans;
    }
    return ret;
}

int VCT_Release(VCTSession *session)
{
    SSPHeader header;

    if (session == NULL) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (vct.SSPMode != VCT_MODE_TRANSCEIVER) {
        return VCT_ERROR_BAD_REQUEST;
    }

    if (vct.listenMode == LISTEN_MODE_CLIENT) {
        int ret = vct_build_request_data(session, VCT_REQUEST_BYE, &header, sizeof(SSPHeader));
        if (ret != VCT_ERROR_NONE) {
            return ret;
        }
        if (vct_send_ssp(session, &header)) {
            return VCT_ERROR_NONE;
        } else {
            return VCT_ERROR_SEND_FAIL;
        }
    }

    header.method = VCT_METHOD_REQUEST;
    header.code = VCT_REQUEST_BYE;
    if (vct_check_send_state(&sSession.trans, &header) == 0) {
        return VCT_ERROR_BAD_REQUEST;
    }

    return vct_ssp_auto_response(1);
}

int VCT_SetTransceiverServer(u8 aid)
{
    if (vct.SSPMode != VCT_MODE_TRANSCEIVER) {
        return VCT_ERROR_BAD_MODE;
    }
    if (vct.listenMode != LISTEN_MODE_CLIENT) {
        return VCT_ERROR_BAD_MODE;
    }
    if (vct.myAID == aid) {
        return VCT_ERROR_BAD_PARAM;
    }
    sSession.trans.aid = aid;
    vct.serverAID = aid;
    return VCT_ERROR_NONE;
}

int VCT_SetTransceiverClients(u8 aidList[], int num_of_aid)
{
    u32 i, j = 0;
    if (num_of_aid > VCT_MAX_TRANSCEIVER_CLIENT) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (vct.listenMode != LISTEN_MODE_SERVER) {
        return VCT_ERROR_BAD_MODE;
    }
    if (aidList == NULL || num_of_aid == 0) {
        sNumOfNotifyAID = 0;
        return VCT_ERROR_NONE;
    }

    sNotifyAID = 0;
    for (i = 0; i < num_of_aid; i++) {
        sNotifyAID |= aidList[i] != 0 ? 1 << aidList[i] : 1;

        if (vct.myAID != aidList[i]) {
            j++;
        }
    }
    if (j > VCT_MAX_TRANSCEIVER_CLIENT - 1) {
        return VCT_ERROR_BAD_PARAM;
    }

    sNotifyAID |= vct.myAID != 0 ? 1 << vct.myAID : 1;
    return VCT_ERROR_NONE;
}

void VCT_SetTransceiverTimeout(u32 sec)
{
    sTransceiverLimit = OS_SecondsToTicks(sec);
}

int VCT_AddConferenceClient(u8 aid)
{
    if (vct.SSPMode != VCT_MODE_CONFERENCE) {
        return VCT_ERROR_BAD_MODE;
    }
    if (sNumOfNotifyAID == VCT_MAX_CONFERENCE_CLIENT - 1) {
        return VCT_ERROR_EXHAUST_CLIENTS;
    }
    if (aid == vct.myAID) {
        return VCT_ERROR_BAD_PARAM;
    }
    if (vct_find_session(aid) != 0) {
        return VCT_ERROR_NONE;
    }
    VCTSession *session = VCT_CreateSession(aid);
    if (session == NULL) {
        return VCT_ERROR_BAD_PARAM;
    }
    session->aid = aid;
    session->talking = aid;
    session->state = VCT_STATE_TALKING;
    session->aidBitmap = 1 << aid;
    vct.callback(aid, VCT_EVENT_CONNECTED, session, vct.userData);
    sNumOfNotifyAID++;
    return VCT_ERROR_NONE;
}

int VCT_RemoveConferenceClient(u8 aid)
{
    if (vct.SSPMode != VCT_MODE_CONFERENCE) {
        return VCT_ERROR_BAD_MODE;
    }
    VCTSession *session = vct_find_session(aid);
    if (session == NULL) {
        return VCT_ERROR_BAD_PARAM;
    }
    vct.callback(aid, VCT_EVENT_DISCONNECTED, session, vct.userData);
    sNumOfNotifyAID--;
    return VCT_ERROR_NONE;
}

BOOL vct_init_ssp(VCTConfig *config)
{
    if (config->session == NULL || config->numSession == 0 || config->numSession > 8) {
        return FALSE;
    }

    sSession.free = config->session;
    MI_CpuClear8(sSession.free, config->numSession * sizeof(VCTSession));
    MI_CpuClear8(&sSession.trans, sizeof(VCTSession));
    MI_CpuClear8(&sSession.tmp, sizeof(VCTSession));
    for (int i = 0; i < config->numSession - 1; i++) {
        sSession.free[i].next = &sSession.free[i + 1];
    }

    sSession.free[config->numSession - 1].next = NULL;
    sSession.use = 0;
    sNotifyAID = 0;
    sNumOfNotifyAID = 0;
    vct.autoResponse = 0;
    MATH_InitRand16(&sSession.rand, (u32)OS_GetTick); // bug
    return TRUE;
}

void vct_finish_ssp(void)
{
    sSession.use = NULL;
    sSession.free = NULL;
}

void vct_check_timeout(void)
{
    OSTick tick = OS_GetTick();

    if (vct.SSPMode != VCT_MODE_TRANSCEIVER) {
        return;
    }
    if (vct.listenMode == LISTEN_MODE_CLIENT) {
        return;
    }
    if (sTransTalkTime == 0) {
        return;
    }
    if (tick - sTransTalkTime <= sTransceiverLimit) {
        return;
    }

    sTransTalkTime = 0;
    sSession.trans.mode = VCT_MODE_NULL;
    sSession.trans.state = VCT_STATE_INIT;
    vct_send_notify(VCT_NOTIFY_FREE);
    vct.callback(sSession.trans.aid, VCT_EVENT_DISCONNECTED, &sSession.trans, vct.userData);
}

BOOL vct_handle_ssp(u8 aid, u8 *buffer, int size, VCTResult *result)
{
    SSPHeader *header = (SSPHeader *)buffer;
    if (size != sizeof(SSPHeader)) {
        return FALSE;
    }
    if (header->from != aid || header->to != vct.myAID) {
        return FALSE;
    }

    if (header->version != VCT_SSP_HEADER_VERSION) {
        return vct_response_reject(header, result);
    }

    if (vct.listenMode == LISTEN_MODE_CLIENT) {
        return vct_handle_ssp_client(header, result);
    } else {
        return vct_handle_ssp_server(header, result);
    }
}

static VCTSession *vct_create_session_impl(u8 aid)
{
    if (aid >= DWC_MAX_CONNECTIONS) {
        return NULL;
    }
    if (aid == vct.myAID) {
        return NULL;
    }

    VCTSession *result = sSession.free;
    if (result == NULL) {
        return NULL;
    }

    sSession.free = result->next;
    vct_init_session(result, aid);
    result->next = sSession.use;
    sSession.use = result;
    return result;
}

static int vct_request_impl(VCTSession *session, VCTRequestCode request)
{
    SSPHeader header;

    int ret = vct_build_request_data(session, request, &header, sizeof(SSPHeader));
    if (ret != VCT_ERROR_NONE) {
        return ret;
    }

    if (!vct_send_ssp(session, &header)) {
        return VCT_ERROR_SEND_FAIL;
    }

    return VCT_ERROR_NONE;
}

static int vct_response_impl(VCTSession *session, VCTResponseCode response)
{
    SSPHeader header;

    int ret = vct_build_response_data(session, response, &header, sizeof(SSPHeader));
    if (ret != 0) {
        return ret;
    }

    if (!vct_send_ssp(session, &header)) {
        return VCT_ERROR_SEND_FAIL;
    }

    return VCT_ERROR_NONE;
}

static int vct_ssp_auto_response(VCTRequestCode request)
{
    VCTEvent event;

    if (request == VCT_REQUEST_CONTACT) {
        sSession.trans.state = VCT_STATE_CONNECTED;
        event = VCT_EVENT_CONNECTED;
        sSession.trans.aid = vct.myAID;
        sSession.trans.talking = vct.myAID;
        int ret = vct_send_notify(VCT_NOTIFY_BUSY);
        if (ret != VCT_ERROR_NONE) {
            return ret;
        }
        sTransTalkTime = OS_GetTick();
    } else if (request == VCT_REQUEST_BYE) {
        int ret = vct_send_notify(VCT_NOTIFY_FREE);
        if (ret != VCT_ERROR_NONE) {
            return ret;
        }
        sSession.trans.mode = VCT_MODE_NULL;
        sSession.trans.state = VCT_STATE_INIT;
        sTransTalkTime = 0;
        event = VCT_EVENT_DISCONNECTED;
    } else {
        return VCT_ERROR_BAD_REQUEST;
    }
    vct.callback(vct.myAID, event, &sSession.trans, vct.userData);
    return VCT_ERROR_NONE;
}

static void vct_add_notify_aid_list(VCTSession *session, SSPHeader *header)
{
    if (header->info != VCT_NOTIFY_BUSY) {
        return;
    }
    header->talking = session->aid;
    header->aidBitmap = sNotifyAID;
}

static int vct_send_notify_impl(VCTSession *session, SSPHeader *header)
{
    int count = 0;
    vct_add_notify_aid_list(session, header);
    for (u8 aid = 0; aid < DWC_MAX_CONNECTIONS; aid++) {
        if ((sNotifyAID & (aid != 0 ? 1 << aid : 1)) && (aid != vct.myAID)) {
            header->to = aid;
            if (vct_send_ssp_impl(aid, header)) {
                count++;
            }
        }
    }

    if (header->info == VCT_NOTIFY_BUSY) {
        vct_set_remote_aid(session, sNotifyAID);
    } else if (header->info == VCT_NOTIFY_FREE) {
        vct_set_remote_aid(session, 0);
    }

    if (count == 0) {
        return VCT_ERROR_SEND_FAIL;
    }

    return VCT_ERROR_NONE;
}

static int vct_send_notify(VCTNotify notify)
{
    SSPHeader header;
    header.method = VCT_METHOD_REQUEST;
    vct_build_ssp_header(&sSession.trans, &header, VCT_REQUEST_NOTIFY);
    header.info = notify;
    return vct_send_notify_impl(&sSession.trans, &header);
}

static void vct_init_session(VCTSession *session, u8 aid)
{
    memset(session, 0, sizeof(VCTSession));
    session->mode = vct.SSPMode;
    session->state = VCT_STATE_INIT;
    session->aid = aid;
}

static BOOL vct_send_ssp(VCTSession *session, SSPHeader *header)
{
    if (header->method == VCT_METHOD_REQUEST && header->code == VCT_REQUEST_NOTIFY) {
        vct_send_notify_impl(session, header);
    } else if (!vct_send_ssp_impl(session->aid, header)) {
        return FALSE;
    }

    return TRUE;
}

static int vct_dispatch_session(SSPHeader *header, VCTSession *session)
{
    session->talking = header->talking;

    if (header->method == VCT_METHOD_REQUEST) {
        if (header->code == VCT_REQUEST_INVITE) {
            vct_set_remote_aid(session, session->aid != 0 ? 1 << session->aid : 1);
        }

        return sRequestEvent[header->code];
    } else if (header->method == VCT_METHOD_RESPONSE) {
        if (header->code == VCT_RESPONSE_OK) {
            if (session->state == VCT_STATE_OUTGOING) {
                vct_set_remote_aid(session, session->aid != 0 ? 1 << session->aid : 1);
                return VCT_EVENT_CONNECTED;
            } else if (session->state == VCT_STATE_DISCONNECTING) {
                return VCT_EVENT_DISCONNECTED;
            } else {
                return VCT_EVENT_ABORT;
            }
        }

        return sResponseEvent[header->code];
    } else {
        return VCT_EVENT_ABORT;
    }
}

static int vct_build_ssp_header(VCTSession *session, SSPHeader *header, u8 code)
{
    header->magic = VCT_MAGIC_PACKET_HEADER;
    header->version = VCT_SSP_HEADER_VERSION;
    header->code = code;
    header->from = vct.myAID;
    header->to = session->aid;
    header->info = VCT_NOTIFY_FREE;
    header->talking = vct.myAID;
    header->aidBitmap = 0;
    return 0;
}

static inline int vct_code_convert(SSPHeader *header)
{
    int code = header->code;
    if (header->method == VCT_METHOD_RESPONSE) {
        code += VCT_REQUEST_END;
    }
    if (code < 0 || VCT_REQUEST_END + VCT_RESPONSE_END <= code) {
        return -1;
    }
    return code;
}

static int vct_check_recv_state(VCTSession *session, SSPHeader *header)
{
    int nextState;
    int code = vct_code_convert(header);
    if (code < 0) {
        return -1;
    }

    if (vct.SSPMode == VCT_MODE_PHONE) {
        nextState = sRecvState[session->state][code];
    } else {
        nextState = sTransRecvState[session->state][code];
    }

    switch (nextState) {
    case VCT_NO_CHANGE:
        return nextState;
    case VCT_STATE_ERROR:
        vct_response_impl(session, VCT_RESPONSE_BAD_REQUEST);
        session->state = VCT_STATE_INIT;
        session->mode = VCT_MODE_NULL;
        return nextState;
    case VCT_CONTACT_FAIL:
        vct_response_impl(session, VCT_RESPONSE_BUSY_HERE);
        session->state = VCT_STATE_INIT;
        session->mode = VCT_MODE_NULL;
        return nextState;
    default:
        return nextState;
    }
}

static int vct_check_multisession(VCTSession *session)
{
    VCTSession *node = sSession.use;
    while (node != NULL) {
        if (node->mode != VCT_MODE_NULL && node->state == VCT_STATE_TALKING && session != node) {
            return FALSE;
        }

        node = node->next;
    }

    return TRUE;
}

static int vct_check_send_state(VCTSession *session, SSPHeader *header)
{
    int nextState;
    int code = vct_code_convert(header);
    if (code < 0) {
        return -1;
    }

    if (vct.SSPMode == VCT_MODE_PHONE) {
        nextState = sSendState[session->state][code];
    } else {
        nextState = sTransSendState[session->state][code];
    }

    if (nextState == VCT_NO_CHANGE) {
        return 1;
    } else if (nextState == VCT_STATE_ERROR) {
        return 0;
    } else {
        session->state = nextState;
        return 1;
    }
}

static VCTSession *vct_find_session(u8 aid)
{
    VCTSession *node = sSession.use;
    if (vct.SSPMode == VCT_MODE_TRANSCEIVER) {
        if (sSession.trans.mode != 0 && sSession.trans.aid == aid) {
            return &sSession.trans;
        }
        return NULL;
    }
    while (node != NULL) {
        if (node->mode != VCT_MODE_NULL && node->aid == aid) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

static BOOL vct_handle_ssp_client(SSPHeader *header, VCTResult *result)
{
    if (header->method == VCT_METHOD_REQUEST) {
        if (header->code == VCT_REQUEST_NOTIFY) {
            return vct_handle_notify(header, result);
        }
        if (header->code == VCT_REQUEST_CONTACT) {
            return FALSE;
        }
    }
    VCTSession *session = vct_find_session(header->from);
    if (session != NULL) {
        int nextState = vct_check_recv_state(session, header);
        switch (nextState) {
        case VCT_NO_CHANGE:
            return FALSE;
        case VCT_STATE_ERROR:
            result->event = VCT_EVENT_ABORT;
            session->mode = VCT_MODE_NULL;
            result->session = session;
            return TRUE;
        default:
            result->event = vct_dispatch_session(header, session);
            result->session = session;
            session->state = nextState;
            if (result->event != VCT_EVENT_NONE) {
                return TRUE;
            } else {
                return FALSE;
            }
        }
    } else {
        session = vct_create_session_impl(header->from);
        if (session == NULL) {
            vct_init_session(&sSession.tmp, header->from);
            vct_response_impl(&sSession.tmp, VCT_RESPONSE_BUSY_HERE);
            return FALSE;
        }

        int nextState = vct_check_recv_state(session, header);
        if (nextState == VCT_NO_CHANGE || nextState == VCT_STATE_ERROR) {
            VCT_DeleteSession(session);
            return FALSE;
        }

        result->event = vct_dispatch_session(header, session);
        result->session = session;
        session->state = nextState;
        if (result->event != VCT_EVENT_NONE) {
            return TRUE;
        } else {
            return FALSE;
        }
    }
}

static BOOL vct_handle_ssp_server(SSPHeader *header, VCTResult *result)
{
    if (sSession.trans.mode == VCT_MODE_TRANSCEIVER) {
        if (sSession.trans.aid == header->from) {
            int nextState = vct_check_recv_state(&sSession.trans, header);
            switch (nextState) {
            case VCT_STATE_ERROR:
                result->event = VCT_EVENT_ABORT;
                result->session = NULL;
                return TRUE;
            case VCT_CONTACT_FAIL:
            case VCT_NO_CHANGE:
                return FALSE;
            default:
                result->event = vct_dispatch_session(header, &sSession.trans);
                result->session = &sSession.trans;
                result->session->state = nextState;
            }
        } else {
            vct_send_notify(VCT_NOTIFY_BUSY);
            return FALSE;
        }
    } else {
        if (!(sNotifyAID & (header->from != 0 ? 1 << header->from : 1))) {
            return FALSE;
        }

        sSession.trans.aid = header->from;
        sSession.trans.mode = VCT_MODE_TRANSCEIVER;
        sSession.trans.state = VCT_STATE_INIT;

        int nextState = vct_check_recv_state(&sSession.trans, header);
        switch (nextState) {
        case VCT_CONTACT_FAIL:
        case VCT_STATE_ERROR:
        case VCT_NO_CHANGE:
            sSession.trans.mode = VCT_MODE_NULL;
            return FALSE;
        default:
            result->event = vct_dispatch_session(header, &sSession.trans);
            result->session = &sSession.trans;
            result->session->state = nextState;
            break;
        }
    }

    if (result->event == VCT_EVENT_CONTACT) {
        vct_send_notify(VCT_NOTIFY_BUSY);
        sTransTalkTime = OS_GetTick();
    } else {
        vct_send_notify(VCT_NOTIFY_FREE);
        sTransTalkTime = 0;
    }

    return TRUE;
}

static BOOL vct_response_reject(SSPHeader *header, VCTResult *result)
{
    if (header->method == VCT_METHOD_REQUEST && header->code == VCT_REQUEST_NOTIFY) {
        return FALSE;
    }

    VCTSession *session = vct_find_session(header->from);
    if (session != NULL) {
        result->event = VCT_EVENT_REJECT;
        result->session = session;
        session->mode = VCT_MODE_NULL;
        vct_response_impl(session, VCT_RESPONSE_NOT_ACCEPTABLE);
        return TRUE;
    } else {
        vct_init_session(&sSession.tmp, header->from);
        vct_response_impl(&sSession.tmp, VCT_RESPONSE_NOT_ACCEPTABLE);
        return FALSE;
    }
}

static BOOL vct_handle_notify(SSPHeader *header, VCTResult *result)
{
    if (vct.SSPMode != VCT_MODE_TRANSCEIVER) {
        return FALSE;
    }
    if (vct.serverAID != header->from) {
        return FALSE;
    }

    switch (header->info) {
    case VCT_NOTIFY_FREE:
        if (sSession.trans.mode == VCT_MODE_NULL) {
            return FALSE;
        }
        if (sSession.trans.state == VCT_STATE_DISCONNECTING || sSession.trans.state == VCT_STATE_TALKING) {
            result->event = VCT_EVENT_DISCONNECTED;
        } else {
            result->event = VCT_EVENT_NOTIFY_FREE;
        }
        sSession.trans.mode = VCT_MODE_NULL;
        sSession.trans.state = VCT_STATE_INIT;
        result->session = &sSession.trans;
        break;
    case VCT_NOTIFY_BUSY:
        sSession.trans.mode = VCT_MODE_TRANSCEIVER;
        if (header->talking == vct.myAID) {
            sSession.trans.state = VCT_STATE_TALKING;
            result->event = VCT_EVENT_CONNECTED;
            vct_set_remote_aid(&sSession.trans, header->aidBitmap);
        } else {
            sSession.trans.state = VCT_STATE_CONNECTED;
            result->event = VCT_EVENT_NOTIFY_BUSY;
            vct_set_remote_aid(&sSession.trans, header->talking != 0 ? 1 << header->talking : 1);
        }

        sSession.trans.talking = header->talking;
        result->session = &sSession.trans;
        break;
    default:
        return FALSE;
    }
    
    return TRUE;
}
