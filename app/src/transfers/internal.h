#ifndef MDO_TRANSFERS_INTERNAL_H
#define MDO_TRANSFERS_INTERNAL_H
#include "../../include/mdo/device_transfer.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/projects.h"
#include "../../include/mdo/project_binding.h"
#include "../../include/mdo/session_backup.h"
#include "../../include/mdo/power.h"
#include "../../include/mdo/remote.h"
#include "../account/internal.h"
#include "../remote/service_client.h"
#include "../remote/lan.h"
#include "../api/internal.h"

#define MDO_DT_BASE "data/device-transfers"
#define MDO_DT_CHUNK (1024u*1024u)
#define MDO_DT_MANIFEST (64u*1024u*1024u)
#define MDO_DT_FILES 100000u
#define MDO_DT_TASKS 16u
typedef struct MdoDtRpc {
    MdoRemoteNet Net;
    MdoRemoteSocket* Socket;
    MdoRemoteSocket* Direct;
    MdoRemoteNet DirectNet;
    xvalue* Offer;
    MdoAccountLease Lease;
    xcancel* Cancel;
    char Device[33], Runtime[33], Client[33], Request[33], Token[64];
    char ErrorCode[65], ErrorMessage[513];
    uint64 Sequence, Sent, Received;
    const uint8* Upload;
    size_t UploadBytes, ResponseBytes;
    char* Response;
    uint16 Status;
    bool Ready, Hello, Done, Failed;
    bool DirectReady;
} MdoDtRpc;

typedef struct MdoDtManager {
    xmutex* Lock;
    xthread* Thread;
    xthread* Receiver;
    xcancel* Stop;
    xcancel* WorkCancel;
    XS_ServerInfo* Server;
    char Active[33];
    char ActiveIncoming[33];
    uint64 Background, BackgroundUntil;
    bool Working;
    xvalue* IncomingManifest;
    char IncomingManifestId[33];
} MdoDtManager;
extern MdoDtManager g_MdoDt;
bool MdoDtId(cstr Text);
bool MdoDtSessionId(cstr Text);
bool MdoDtRandom(char Id[33]);
bool MdoDtPath(cstr Path);
bool MdoDtSave(cstr Path, const xvalue* Value);
xvalue* MdoDtRead(cstr Path, size_t Limit);
char* MdoDtBytes(cstr Path, size_t Limit, size_t* Bytes);
bool MdoDtHash(xfile File, char Hash[65], uint64* Bytes, xcancel* Cancel);
bool MdoDtHex(const void* Data, size_t Bytes, char Hash[65]);
bool MdoDtWrite(cstr Path, const void* Data, size_t Bytes);
void MdoDtTaskPath(char Path[256], bool Incoming, cstr Id, cstr Leaf);
xvalue* MdoDtTaskRead(bool Incoming, cstr Id);
bool MdoDtTaskSave(bool Incoming, const xvalue* Task);
bool MdoDtTaskState(xvalue* Task, cstr State, cstr Error);
bool MdoDtMember(uint64 Member);
bool MdoDtReceiveAllowed(void);
bool MdoDtCapture(xvalue* Task, xcancel* Cancel, xwork_error* Error);
bool MdoDtManifestValid(const xvalue* Manifest);
bool MdoDtCommit(xvalue* Task, xcancel* Cancel, xwork_error* Error);
bool MdoDtIncomingRoute(MdoApiContext* Context);
bool MdoApiDeviceTransfersRoute(MdoApiContext* Context);
bool MdoDtRpcConnect(MdoDtRpc* Rpc, XS_ServerInfo* Server, cstr Device, uint64 Member, xcancel* Cancel);
void MdoDtRpcClose(MdoDtRpc* Rpc);
xvalue* MdoDtRpcCall(MdoDtRpc* Rpc, cstr Method, cstr Path, const void* Data, size_t Bytes, bool Json);
bool MdoDtSend(xvalue* Task, xcancel* Cancel);
void MdoDtKeepAwake(void);
bool MdoDtDiskSpace(cstr Path,uint64 Required);
void MdoDtTargetSession(cstr Transfer,cstr Source,char Target[33]);
bool MdoDtUnsigned(xstrview Text,uint64 Max,uint64* Value);
xvalue* MdoDtQueue(bool Incoming);
bool MdoDtQueueChange(bool Incoming,cstr Id,bool Add);
bool MdoDtKeys(const xvalue* Value,const char* const* Keys,size_t Count);
bool MdoDtPrunePayload(bool Incoming,xvalue* Task);
bool MdoDtReceiverId(char Id[33]);
#endif
