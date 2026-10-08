#include <stdio.h>
#include <string.h>

#include "bridge.h"
#include "service_client.h"

#define MDO_BRIDGE_PEERS 4u
#define MDO_BRIDGE_HTTP_REQUESTS 8u
#define MDO_BRIDGE_REQUESTS (MDO_BRIDGE_HTTP_REQUESTS + MDO_BRIDGE_PEERS)
#define MDO_BRIDGE_LIVE_LIMIT (2u * 1024u * 1024u)
#define MDO_BRIDGE_LIVE_COMMANDS 8u
#define MDO_BRIDGE_UPLOAD (8u * 1024u * 1024u)
#define MDO_BRIDGE_UPLOAD_TOTAL (16u * 1024u * 1024u)
#define MDO_BRIDGE_QUEUE (512u * 1024u)
#define MDO_BRIDGE_PACKETS 128u
#define MDO_BRIDGE_CHUNK 65536u
#define MDO_BRIDGE_WINDOW 65536u
#define MDO_BRIDGE_WINDOW_MAX 262144u
#define MDO_BRIDGE_BINARY_HEAD 29u
#define MDO_BRIDGE_UPLOAD_US 120000000u
#define MDO_BRIDGE_QUEUE_US 15000000u

typedef struct MdoBridgePeer {
    char Id[33];
    uint8 Bytes[16];
    uint64 Generation;
    xcancel* Cancel;
    bool Active, ReadOnly;
} MdoBridgePeer;
typedef struct MdoBridgeCommand {
    struct MdoBridgeCommand* Next;
    size_t Size;
    char Bytes[];
} MdoBridgeCommand;
typedef struct MdoBridgeRequest {
    struct MdoRemoteBridge* Bridge;
    char Id[33], Method[7], Path[4097], FieldStorage[4128];
    XS_FetchHeader Fields[16];
    MdoRemoteHttpRequest Request;
    uint8* Upload;
    uint8 Digest[32];
    xsha256 Hash;
    xcancel* Cancel;
    xthread* Thread;
    MdoBridgeCommand *Commands, *LastCommand;
    MdoRemoteReceipt* Receipt;
    uint64 Serial, PeerGeneration, Until, Received, Sent, Acknowledged, LiveSequence;
    size_t Peer, Slot, Packets;
    size_t Window;
    uint16 Status;
    size_t CommandCount;
    bool Completed, Live;
} MdoBridgeRequest;
typedef struct MdoBridgePacket {
    struct MdoBridgePacket* Next;
    size_t Peer, Slot, Size;
    uint64 PeerGeneration, RequestSerial;
    uint8 Bytes[];
} MdoBridgePacket;
struct MdoRemoteBridge {
    xmutex* Lock;
    xcond* Changed;
    XS_ServerInfo* Server;
    MdoRemoteNet* Net;
    MdoRemoteReceiptStore Receipts;
    MdoBridgePeer Peers[MDO_BRIDGE_PEERS];
    MdoBridgeRequest Requests[MDO_BRIDGE_REQUESTS];
    MdoBridgePacket *First, *Last;
    size_t Bytes, Packets, UploadBytes;
    uint64 Serial;
    bool Stopping;
    MdoRemoteBridgeOffer Offer;
    void* OfferData;
};

static int32 MdoBridgeHttpWorker(void* Data);
static int32 MdoBridgeLiveWorker(void* Data);
static void MdoBridgeWrite64(uint8* Bytes, uint64 Number)
{
    for (size_t i = 0u; i < 8u; i++) Bytes[7u-i] = (uint8)(Number >> (i*8u));
}
static uint64 MdoBridgeRead64(const uint8* Bytes)
{
    uint64 number = 0u;
    for (size_t i = 0u; i < 8u; i++) number = (number << 8u) | Bytes[i];
    return number;
}
static bool MdoBridgeHex(cstr Text, size_t Bytes, uint8* Output)
{
    if (!Text || strlen(Text) != Bytes*2u) return false;
    for (size_t i = 0u; i < Bytes*2u; i++) {
        char c = Text[i]; unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c-'0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c-'a')+10u;
        else return false;
        if (!(i & 1u)) Output[i/2u] = (uint8)(digit << 4u);
        else Output[i/2u] |= (uint8)digit;
    }
    return true;
}
static cstr MdoBridgeText(const xvalue* Value, size_t Limit)
{
    xstrview view;
    return xrtValueGetString(Value,&view) && view.Size <= Limit && !memchr(view.Data,0,view.Size) ? view.Data : NULL;
}
static size_t MdoBridgePeerFind(MdoRemoteBridge* Bridge, cstr Id)
{
    for (size_t i = 0u; i < MDO_BRIDGE_PEERS; i++)
        if (Bridge->Peers[i].Active && !strcmp(Bridge->Peers[i].Id,Id)) return i;
    return SIZE_MAX;
}
static bool MdoBridgeRequestLive(const MdoBridgeRequest* Request)
{
    const MdoRemoteBridge* bridge = Request->Bridge;
    const MdoBridgePeer* peer = &bridge->Peers[Request->Peer];
    return !bridge->Stopping && peer->Active && peer->Generation == Request->PeerGeneration &&
        !xrtCancelRequested(Request->Cancel);
}
static MdoBridgeRequest* MdoBridgeRequestFind(MdoRemoteBridge* Bridge, size_t Peer, cstr Id)
{
    for (size_t i = 0u; i < MDO_BRIDGE_REQUESTS; i++) {
        MdoBridgeRequest* request = &Bridge->Requests[i];
        if (request->Id[0] && request->Peer == Peer && request->PeerGeneration == Bridge->Peers[Peer].Generation &&
            !strcmp(request->Id,Id)) return request;
    }
    return NULL;
}
/* Call with the bridge lock. Owner-thread control messages never wait. HTTP
 * workers wait using the condition before admission to this bounded queue. */
static bool MdoBridgeQueue(MdoRemoteBridge* Bridge, size_t Peer, MdoBridgeRequest* Request,
    bool Binary, xbytesview Payload)
{
    MdoBridgePeer* peer = &Bridge->Peers[Peer];
    size_t size = MDO_REMOTE_RELAY_HEADER+Payload.Size;
    if (Bridge->Stopping || !peer->Active || Payload.Size > MDO_REMOTE_RELAY_PAYLOAD ||
        size > MDO_BRIDGE_QUEUE-Bridge->Bytes || Bridge->Packets >= MDO_BRIDGE_PACKETS ||
        (Request && !MdoBridgeRequestLive(Request))) return false;
    MdoBridgePacket* packet = xrtMalloc(sizeof(*packet)+size);
    if (!packet) return false;
    packet->Next = NULL; packet->Peer = Peer; packet->PeerGeneration = peer->Generation;
    packet->Slot = Request ? Request->Slot : SIZE_MAX;
    packet->RequestSerial = Request ? Request->Serial : 0u; packet->Size = size;
    memcpy(packet->Bytes,"MDR1",4u); memcpy(packet->Bytes+4u,peer->Bytes,16u); packet->Bytes[20] = Binary ? 2u : 1u;
    if (Payload.Size) memcpy(packet->Bytes+MDO_REMOTE_RELAY_HEADER,Payload.Data,Payload.Size);
    if (Bridge->Last) Bridge->Last->Next = packet; else Bridge->First = packet;
    Bridge->Last = packet; Bridge->Bytes += size; ++Bridge->Packets;
    if (Request) ++Request->Packets;
    return true;
}
static bool MdoBridgeValue(MdoRemoteBridge* Bridge, size_t Peer, MdoBridgeRequest* Request, xvalue* Value)
{
    size_t size = 0u; char* json = Value ? xrtJsonStringify(Value,false,&size) : NULL;
    bool ok = json && MdoBridgeQueue(Bridge,Peer,Request,false,(xbytesview){(const uint8*)json,size});
    if (json) xrtSecureZero(json,size);
    xrtFree(json); xrtValueRelease(Value); return ok;
}
static bool MdoBridgeError(MdoRemoteBridge* Bridge, size_t Peer, cstr Id, cstr Code, cstr Outcome)
{
    xvalue* value = xrtValueObject();
    bool ok = value && MdoAccountSetString(value,"type","error") &&
        MdoAccountSetString(value,"id",Id ? Id : "") && MdoAccountSetString(value,"code",Code) &&
        MdoAccountSetString(value,"outcome",Outcome);
    if (!ok) { xrtValueRelease(value); return false; }
    return MdoBridgeValue(Bridge,Peer,NULL,value);
}
static bool MdoBridgeReceiptReply(MdoRemoteBridge* Bridge, size_t Peer, cstr Id,
    MdoRemoteReceiptAdmission Admission, const MdoRemoteReceipt* Receipt)
{
    xvalue* value = xrtValueObject();
    bool ok = value && MdoAccountSetString(value,"type","receipt") && MdoAccountSetString(value,"id",Id) &&
        MdoAccountSetString(value,"result",MdoRemoteReceiptAdmissionName(Admission)) &&
        MdoAccountSetString(value,"state",Receipt ? MdoRemoteReceiptPhaseName(Receipt->Phase) : "unknown") &&
        MdoAccountSetUInt(value,"status",Receipt ? Receipt->Status : 0u);
    if (!ok) { xrtValueRelease(value); return false; }
    return MdoBridgeValue(Bridge,Peer,NULL,value);
}
static bool MdoBridgeProgress(MdoBridgeRequest* Request, cstr Type, uint64 Offset)
{
    xvalue* value = xrtValueObject();
    bool ok = value && MdoAccountSetString(value,"type",Type) && MdoAccountSetString(value,"id",Request->Id) &&
        MdoAccountSetUInt(value,"offset",Offset);
    if (!ok) { xrtValueRelease(value); return false; }
    return MdoBridgeValue(Request->Bridge,Request->Peer,Request,value);
}
static bool MdoBridgeHashPart(xsha256* Hash, const void* Bytes, size_t Size)
{
    uint8 length[8]; MdoBridgeWrite64(length,(uint64)Size);
    return xrtSha256Update(Hash,length,sizeof(length)) && xrtSha256Update(Hash,Bytes,Size);
}
static bool MdoBridgeFingerprint(const MdoRemoteHttpRequest* Request, const uint8 Digest[32], uint8 Output[32])
{
    xsha256 hash; uint8 number[8]; xrtSha256Init(&hash);
    bool ok = MdoBridgeHashPart(&hash,Request->Method,strlen(Request->Method)) &&
        MdoBridgeHashPart(&hash,Request->Target,strlen(Request->Target));
    MdoBridgeWrite64(number,(uint64)Request->HeaderCount);
    ok = ok && xrtSha256Update(&hash,number,sizeof(number));
    for (size_t i = 0u; ok && i < Request->HeaderCount; i++)
        ok = MdoBridgeHashPart(&hash,Request->Headers[i].Name,strlen(Request->Headers[i].Name)) &&
            MdoBridgeHashPart(&hash,Request->Headers[i].Value,strlen(Request->Headers[i].Value));
    MdoBridgeWrite64(number,(uint64)Request->Body.Size);
    ok = ok && xrtSha256Update(&hash,number,sizeof(number)) && xrtSha256Update(&hash,Digest,32u) && xrtSha256Final(&hash,Output);
    xrtSecureZero(&hash,sizeof(hash)); return ok;
}
static void MdoBridgeAbandon(MdoBridgeRequest* Request)
{
    xrtCancelRequest(Request->Cancel);
    if (!Request->Thread && !Request->Completed) {
        if (Request->Receipt) (void)MdoRemoteReceiptFinish(Request->Receipt,false,0u);
        Request->Receipt = NULL; Request->Completed = true;
    }
}
static bool MdoBridgeStart(MdoBridgeRequest* Request)
{
    uint8 digest[32];
    bool valid = xrtSha256Final(&Request->Hash,digest) && !memcmp(digest,Request->Digest,32u);
    xrtSecureZero(digest,sizeof(digest));
    if (!valid) {
        bool reply = MdoBridgeError(Request->Bridge,Request->Peer,Request->Id,"body_digest_mismatch","not_started");
        MdoBridgeAbandon(Request); return reply;
    }
    Request->Thread = xrtThreadCreate(MdoBridgeHttpWorker,Request,0u);
    if (!Request->Thread) {
        bool reply = MdoBridgeError(Request->Bridge,Request->Peer,Request->Id,"remote_resources_unavailable","not_started");
        MdoBridgeAbandon(Request); return reply;
    }
    return true;
}
static bool MdoBridgeAdmit(MdoRemoteBridge* Bridge, size_t Peer, const xvalue* Value)
{
    cstr id = MdoAccountText(Value,"id",32u), runtime = MdoAccountText(Value,"runtime_id",32u);
    cstr client = MdoAccountText(Value,"client_id",32u), method = MdoAccountText(Value,"method",6u);
    cstr path = MdoAccountText(Value,"path",4096u), digest_text = MdoAccountText(Value,"sha256",64u);
    uint64 sequence = 0u, bytes = 0u, window = MDO_BRIDGE_WINDOW; uint8 digest[32], fingerprint[32];
    const xvalue* window_value = xrtValueObjectGet(Value,XRT_STR_LITERAL("window_bytes"));
    const xvalue* headers = xrtValueObjectGet(Value,XRT_STR_LITERAL("headers"));
    XS_FetchHeader fields[16]; MdoRemoteHttpRequest req = {0};
    bool valid = xrtValueCount(Value) == (window_value ? 11u : 10u) &&
        (!window_value || (MdoAccountGetUInt(window_value,&window) && window >= MDO_BRIDGE_WINDOW &&
          window <= MDO_BRIDGE_WINDOW_MAX && window % 16384u == 0u)) &&
        MdoRemoteIdValid(id) && MdoRemoteIdValid(client) && MdoRemoteIdValid(runtime) &&
        MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("sequence")),&sequence) &&
        MdoAccountGetUInt(xrtValueObjectGet(Value,XRT_STR_LITERAL("bytes")),&bytes) && bytes <= MDO_BRIDGE_UPLOAD &&
        MdoBridgeHex(digest_text,32u,digest) && xrtValueType(headers) == XVALUE_ARRAY && xrtValueCount(headers) <= 16u;
    if (!valid) return MdoBridgeError(Bridge,Peer,MdoRemoteIdValid(id) ? id : "","invalid_request","not_started");
    req.Method = method; req.Target = path; req.ReadOnly = Bridge->Peers[Peer].ReadOnly;
    req.Headers = fields; req.HeaderCount = xrtValueCount(headers);
    /* Validation sees the declared body size, never dereferences this dummy. */
    req.Body = (xbytesview){bytes ? digest : NULL,(size_t)bytes};
    for (size_t i = 0u; valid && i < req.HeaderCount; i++) {
        const xvalue* pair = xrtValueArrayGet(headers,i);
        valid = xrtValueType(pair) == XVALUE_ARRAY && xrtValueCount(pair) == 2u;
        if (valid) {
            fields[i].Name = MdoBridgeText(xrtValueArrayGet(pair,0u),64u);
            fields[i].Value = MdoBridgeText(xrtValueArrayGet(pair,1u),3072u);
            valid = fields[i].Name && fields[i].Value;
        }
    }
    valid = valid && MdoRemoteHttpRequestValid(&req) && MdoBridgeFingerprint(&req,digest,fingerprint);
    if (!valid) return MdoBridgeError(Bridge,Peer,id,req.ReadOnly ? "read_only" : "invalid_request","not_started");
    if (strcmp(runtime,Bridge->Receipts.Runtime)) return MdoBridgeError(Bridge,Peer,id,"runtime_changed","unknown");
    bool write = strcmp(method,"GET") && strcmp(method,"HEAD");
    if ((!write && sequence) || (write && (!sequence || sequence > MDO_REMOTE_SEQUENCE_MAX)))
        return MdoBridgeError(Bridge,Peer,id,"invalid_sequence","not_started");
    MdoRemoteReceipt* receipt = NULL;
    if (write) {
        MdoRemoteReceiptAdmission admission = MdoRemoteReceiptQuery(&Bridge->Receipts,runtime,client,sequence,id,&receipt);
        if (admission == MDO_REMOTE_RECEIPT_DUPLICATE) {
            admission = MdoRemoteReceiptClaim(&Bridge->Receipts,runtime,client,sequence,id,fingerprint,req.ReadOnly,&receipt);
            return MdoBridgeReceiptReply(Bridge,Peer,id,admission,receipt);
        }
        if (admission != MDO_REMOTE_RECEIPT_UNKNOWN) return MdoBridgeReceiptReply(Bridge,Peer,id,admission,NULL);
    }
    size_t count = 0u; MdoBridgeRequest* request = NULL;
    for (size_t i = 0u; i < MDO_BRIDGE_HTTP_REQUESTS; i++) {
        if (!Bridge->Requests[i].Id[0] && !request) request = &Bridge->Requests[i];
        if (Bridge->Requests[i].Id[0] && Bridge->Requests[i].Peer == Peer &&
            Bridge->Requests[i].PeerGeneration == Bridge->Peers[Peer].Generation) ++count;
    }
    if (MdoBridgeRequestFind(Bridge,Peer,id)) return MdoBridgeError(Bridge,Peer,id,"request_conflict","unknown");
    if (!request || count >= 4u || bytes > MDO_BRIDGE_UPLOAD_TOTAL-Bridge->UploadBytes)
        return MdoBridgeError(Bridge,Peer,id,"remote_capacity","not_started");
    if (write) {
        MdoRemoteReceiptAdmission admission = MdoRemoteReceiptClaim(&Bridge->Receipts,runtime,client,sequence,id,fingerprint,req.ReadOnly,&receipt);
        if (admission != MDO_REMOTE_RECEIPT_ADMITTED) return MdoBridgeReceiptReply(Bridge,Peer,id,admission,receipt);
    }
    memset(request,0,sizeof(*request)); request->Bridge = Bridge;
    request->Peer = Peer; request->PeerGeneration = Bridge->Peers[Peer].Generation;
    request->Window = (size_t)window;
    request->Slot = (size_t)(request-Bridge->Requests); request->Serial = ++Bridge->Serial;
    request->Receipt = receipt; strcpy(request->Id,id); strcpy(request->Method,method); strcpy(request->Path,path);
    memcpy(request->Digest,digest,32u); xrtSha256Init(&request->Hash);
    request->Cancel = xrtCancelChild(Bridge->Peers[Peer].Cancel); request->Until = xrtDeadlineAfter(MDO_BRIDGE_UPLOAD_US);
    request->Request = req; request->Request.Method = request->Method; request->Request.Target = request->Path;
    request->Request.Headers = request->Fields;
    size_t at = 0u;
    for (size_t i = 0u; i < req.HeaderCount; i++) {
        size_t length = strlen(fields[i].Name)+1u; request->Fields[i].Name = request->FieldStorage+at;
        memcpy(request->FieldStorage+at,fields[i].Name,length); at += length;
        length = strlen(fields[i].Value)+1u; request->Fields[i].Value = request->FieldStorage+at;
        memcpy(request->FieldStorage+at,fields[i].Value,length); at += length;
    }
    request->Upload = bytes ? xrtMalloc((size_t)bytes) : NULL;
    request->Request.Body.Data = request->Upload;
    Bridge->UploadBytes += (size_t)bytes;
    if (!request->Cancel || (bytes && !request->Upload)) {
        bool reply = MdoBridgeError(Bridge,Peer,id,"remote_resources_unavailable","not_started");
        MdoBridgeAbandon(request); return reply;
    }
    if (!bytes) return MdoBridgeStart(request);
    return MdoBridgeProgress(request,"request_ready",0u);
}

static bool MdoBridgeLiveToken(cstr Token)
{
    uint8 ignored[16]; char id[33];
    if (!Token || strlen(Token) < 34u || strlen(Token) > 53u || Token[32] != '-') return false;
    memcpy(id,Token,32u); id[32] = 0;
    if (!MdoBridgeHex(id,16u,ignored) || (Token[33] == '0' && Token[34])) return false;
    uint64 number = 0u;
    for (size_t i = 33u; Token[i]; i++) {
        unsigned digit = (unsigned)(Token[i]-'0');
        if (digit > 9u || number > (UINT64_MAX-digit)/10u) return false;
        number = number*10u+digit;
    }
    return true;
}
static bool MdoBridgeLiveOpen(MdoRemoteBridge* Bridge, size_t Peer, const xvalue* Value)
{
    cstr id = MdoAccountText(Value,"id",32u), token = MdoAccountText(Value,"token",53u);
    cstr runtime = MdoAccountText(Value,"runtime_id",32u);
    if (xrtValueCount(Value) != 4u || !MdoRemoteIdValid(id) || !MdoBridgeLiveToken(token) || !MdoRemoteIdValid(runtime))
        return MdoBridgeError(Bridge,Peer,"","invalid_live","not_started");
    if (strcmp(runtime,Bridge->Receipts.Runtime)) return MdoBridgeError(Bridge,Peer,id,"runtime_changed","unknown");
    MdoBridgeRequest* request = NULL;
    for (size_t i = MDO_BRIDGE_HTTP_REQUESTS; i < MDO_BRIDGE_REQUESTS; i++) {
        if (!Bridge->Requests[i].Id[0] && !request) request = &Bridge->Requests[i];
        if (Bridge->Requests[i].Id[0] && Bridge->Requests[i].Peer == Peer &&
            Bridge->Requests[i].PeerGeneration == Bridge->Peers[Peer].Generation)
            return MdoBridgeError(Bridge,Peer,id,"live_capacity","not_started");
    }
    if (!request || MdoBridgeRequestFind(Bridge,Peer,id)) return MdoBridgeError(Bridge,Peer,id,"live_capacity","not_started");
    memset(request,0,sizeof(*request)); request->Bridge = Bridge; request->Live = true;
    request->Peer = Peer; request->PeerGeneration = Bridge->Peers[Peer].Generation;
    request->Slot = (size_t)(request-Bridge->Requests); request->Serial = ++Bridge->Serial;
    strcpy(request->Id,id); strcpy(request->FieldStorage,token);
    request->Cancel = xrtCancelChild(Bridge->Peers[Peer].Cancel);
    if (request->Cancel) request->Thread = xrtThreadCreate(MdoBridgeLiveWorker,request,0u);
    if (request->Thread) return true;
    MdoBridgeAbandon(request); return MdoBridgeError(Bridge,Peer,id,"live_unavailable","not_started");
}
static bool MdoBridgeLiveSend(MdoRemoteBridge* Bridge, size_t Peer, const xvalue* Value)
{
    cstr id = MdoAccountText(Value,"id",32u), data = MdoAccountText(Value,"data",4096u);
    MdoBridgeRequest* request = MdoRemoteIdValid(id) ? MdoBridgeRequestFind(Bridge,Peer,id) : NULL;
    if (xrtValueCount(Value) != 3u || !data || !data[0] || !request || !request->Live ||
        !MdoBridgeRequestLive(request) || request->Completed || request->CommandCount >= MDO_BRIDGE_LIVE_COMMANDS)
        return MdoBridgeError(Bridge,Peer,MdoRemoteIdValid(id) ? id : "","live_command_unavailable","not_started");
    size_t size = strlen(data); MdoBridgeCommand* command = xrtMalloc(sizeof(*command)+size);
    if (!command) return false;
    command->Next = NULL; command->Size = size; memcpy(command->Bytes,data,size);
    if (request->LastCommand) request->LastCommand->Next = command; else request->Commands = command;
    request->LastCommand = command; ++request->CommandCount;
    xrtCondBroadcast(Bridge->Changed); return true;
}

static bool MdoBridgeUpload(MdoRemoteBridge* Bridge, size_t Peer, xbytesview Bytes)
{
    const uint8* data = Bytes.Data; char id[33]; static const char hex[] = "0123456789abcdef";
    if (Bytes.Size <= MDO_BRIDGE_BINARY_HEAD || Bytes.Size > MDO_BRIDGE_BINARY_HEAD+MDO_BRIDGE_CHUNK ||
        memcmp(data,"MDP1",4u) || data[4] != 1u) return MdoBridgeError(Bridge,Peer,"","invalid_chunk","not_started");
    for (size_t i = 0u; i < 16u; i++) { id[i*2u] = hex[data[5u+i] >> 4u]; id[i*2u+1u] = hex[data[5u+i] & 15u]; }
    id[32] = 0;
    MdoBridgeRequest* request = MdoBridgeRequestFind(Bridge,Peer,id);
    if (!request || request->Completed || request->Thread) return MdoBridgeError(Bridge,Peer,id,"upload_unavailable","unknown");
    size_t size = Bytes.Size-MDO_BRIDGE_BINARY_HEAD; uint64 offset = MdoBridgeRead64(data+21u);
    if (!MdoBridgeRequestLive(request) || offset != request->Received ||
        size > request->Request.Body.Size-request->Received || xrtDeadlineExpired(request->Until)) {
        bool reply = MdoBridgeError(Bridge,Peer,id,"invalid_chunk","not_started");
        MdoBridgeAbandon(request); return reply;
    }
    memcpy(request->Upload+request->Received,data+MDO_BRIDGE_BINARY_HEAD,size);
    if (!xrtSha256Update(&request->Hash,data+MDO_BRIDGE_BINARY_HEAD,size)) { MdoBridgeAbandon(request); return false; }
    request->Received += size;
    if (!MdoBridgeProgress(request,"upload_ack",request->Received)) { MdoBridgeAbandon(request); return false; }
    return request->Received == request->Request.Body.Size ? MdoBridgeStart(request) : true;
}
bool MdoRemoteBridgeInput(MdoRemoteBridge* Bridge, cstr Peer, bool Binary, xbytesview Message)
{
    if (!Bridge || !MdoRemoteIdValid(Peer) || !Message.Data) return false;
    xrtMutexLock(Bridge->Lock); size_t peer = MdoBridgePeerFind(Bridge,Peer);
    if (peer == SIZE_MAX || Bridge->Stopping) { xrtMutexUnlock(Bridge->Lock); return true; }
    bool ok;
    if (Binary) ok = MdoBridgeUpload(Bridge,peer,Message);
    else {
        xjsonreadconfig limits; xrtJsonReadConfigInit(&limits);
        limits.MaxInputBytes = 16384u; limits.MaxStringBytes = 4096u; limits.MaxValues = 128u; limits.MaxDepth = 4u;
        xvalue* value = xrtJsonRead(xrtStrViewN((cstr)Message.Data,Message.Size),&limits);
        cstr type = MdoAccountText(value,"type",24u), id = MdoAccountText(value,"id",32u);
        if (type && !strcmp(type,"request")) ok = MdoBridgeAdmit(Bridge,peer,value);
        else if (type && !strcmp(type,"live_open")) ok = MdoBridgeLiveOpen(Bridge,peer,value);
        else if (type && !strcmp(type,"live_send")) ok = MdoBridgeLiveSend(Bridge,peer,value);
        else if (type && !strcmp(type,"receipt")) {
            uint64 sequence = 0u; MdoRemoteReceipt* record = NULL;
            MdoRemoteReceiptAdmission admission = MdoRemoteReceiptQuery(&Bridge->Receipts,
                MdoAccountText(value,"runtime_id",32u),MdoAccountText(value,"client_id",32u),
                MdoAccountGetUInt(xrtValueObjectGet(value,XRT_STR_LITERAL("sequence")),&sequence) ? sequence : 0u,id,&record);
            ok = MdoBridgeReceiptReply(Bridge,peer,MdoRemoteIdValid(id) ? id : "",admission,record);
        } else if (type && MdoRemoteIdValid(id) && (!strcmp(type,"cancel") || !strcmp(type,"live_close") ||
            !strcmp(type,"download_ack") || !strcmp(type,"live_ack"))) {
            MdoBridgeRequest* request = MdoBridgeRequestFind(Bridge,peer,id); uint64 offset = 0u;
            if (!strcmp(type,"cancel") || !strcmp(type,"live_close")) { if (request) MdoBridgeAbandon(request); ok = true; }
            else if (!request || request->Completed) ok = true; /* Late ack after terminal response. */
            else if (xrtValueCount(value) == 3u && MdoAccountGetUInt(xrtValueObjectGet(value,XRT_STR_LITERAL("offset")),&offset) &&
                offset >= request->Acknowledged && offset <= request->Sent) {
                request->Acknowledged = offset; xrtCondBroadcast(Bridge->Changed); ok = true;
            } else {
                MdoBridgeAbandon(request); ok = MdoBridgeError(Bridge,peer,id,"invalid_ack","unknown");
            }
        } else ok = MdoBridgeError(Bridge,peer,MdoRemoteIdValid(id) ? id : "","invalid_message","not_started");
        /* Incoming headers can include the target nonce. Values are private,
         * and the source WS message is cleared by its owning transport. */
        xrtValueRelease(value);
    }
    xrtMutexUnlock(Bridge->Lock); return ok;
}

static bool MdoBridgeWorkerQueue(MdoBridgeRequest* Request, bool Binary, xbytesview Bytes, size_t BodyBytes)
{
    MdoRemoteBridge* bridge = Request->Bridge; xdeadline until = xrtDeadlineAfter(MDO_BRIDGE_QUEUE_US);
    xrtMutexLock(bridge->Lock);
    while (MdoBridgeRequestLive(Request) && !xrtDeadlineExpired(until) &&
        (bridge->Bytes+Bytes.Size+MDO_REMOTE_RELAY_HEADER > MDO_BRIDGE_QUEUE || bridge->Packets >= MDO_BRIDGE_PACKETS ||
        (BodyBytes && Request->Sent+BodyBytes-Request->Acknowledged >
          (Request->Live ? MDO_BRIDGE_WINDOW : Request->Window))))
        xrtCondWaitFor(bridge->Changed,bridge->Lock,100000u);
    bool ok = !xrtDeadlineExpired(until) && MdoBridgeQueue(bridge,Request->Peer,Request,Binary,Bytes);
    if (ok) Request->Sent += BodyBytes;
    xrtMutexUnlock(bridge->Lock); return ok;
}
static bool MdoBridgeWorkerValue(MdoBridgeRequest* Request, xvalue* Value)
{
    size_t size = 0u; char* json = Value ? xrtJsonStringify(Value,false,&size) : NULL;
    bool ok = json && MdoBridgeWorkerQueue(Request,false,(xbytesview){(const uint8*)json,size},0u);
    if (json) xrtSecureZero(json,size);
    xrtFree(json); xrtValueRelease(Value); return ok;
}
static bool MdoBridgeResponseHead(const xhttp1head* Head, void* Data)
{
    MdoBridgeRequest* request = Data; xvalue *value = xrtValueObject(), *headers = xrtValueArray();
    bool ok = value && headers && MdoAccountSetString(value,"type","response") &&
        MdoAccountSetString(value,"id",request->Id) && MdoAccountSetUInt(value,"status",Head->Status);
    for (size_t i = 0u; ok && i < Head->FieldCount; i++) {
        const xhttpfield* field = &Head->Fields[i];
        if (!MdoRemoteHttpResponseField(field->Name)) continue;
        xvalue* pair = xrtValueArray();
        ok = pair && xrtValueArrayAppendNew(pair,xrtValueString(field->Name)) &&
            xrtValueArrayAppendNew(pair,xrtValueString(field->Value));
        if (ok) ok = xrtValueArrayAppendNew(headers,pair); else xrtValueRelease(pair);
    }
    if (ok) ok = xrtValueObjectSetNew(value,XRT_STR_LITERAL("headers"),headers);
    else xrtValueRelease(headers);
    if (!ok) { xrtValueRelease(value); return false; }
    xrtMutexLock(request->Bridge->Lock); request->Status = Head->Status; xrtMutexUnlock(request->Bridge->Lock);
    return MdoBridgeWorkerValue(request,value);
}
static bool MdoBridgeBody(MdoBridgeRequest* request, xbytesview Bytes, uint8 Kind)
{
    uint8 frame[MDO_BRIDGE_BINARY_HEAD+16384u];
    if (Bytes.Size > 16384u) return false;
    memcpy(frame,"MDP1",4u); frame[4] = Kind;
    if (!MdoBridgeHex(request->Id,16u,frame+5u)) return false;
    /* Sent is written only by this worker, read under the bridge lock by acks.
     * Snapshot it under that lock too, without holding it across queue waits. */
    xrtMutexLock(request->Bridge->Lock); uint64 offset = request->Sent; xrtMutexUnlock(request->Bridge->Lock);
    MdoBridgeWrite64(frame+21u,offset); memcpy(frame+MDO_BRIDGE_BINARY_HEAD,Bytes.Data,Bytes.Size);
    bool ok = MdoBridgeWorkerQueue(request,true,(xbytesview){frame,MDO_BRIDGE_BINARY_HEAD+Bytes.Size},Bytes.Size);
    xrtSecureZero(frame,sizeof(frame)); return ok;
}
static bool MdoBridgeResponseBody(xbytesview Bytes, void* Data)
{ return MdoBridgeBody(Data,Bytes,2u); }
static int32 MdoBridgeHttpWorker(void* Data)
{
    MdoBridgeRequest* request = Data; MdoRemoteBridge* bridge = request->Bridge;
    xrtMutexLock(bridge->Lock);
    bool started = MdoBridgeRequestLive(request);
    if (started && request->Receipt) started = MdoRemoteReceiptRun(request->Receipt);
    xrtMutexUnlock(bridge->Lock);
    MdoRemoteHttpSink sink = {MdoBridgeResponseHead,MdoBridgeResponseBody,request};
    bool complete = started && MdoRemoteLoopbackCall(bridge->Server,&request->Request,request->Cancel,&sink);
    xrtMutexLock(bridge->Lock);
    if (request->Receipt) {
        (void)MdoRemoteReceiptFinish(request->Receipt,complete,started ? request->Status : 0u);
        request->Receipt = NULL;
    }
    xrtMutexUnlock(bridge->Lock);
    xvalue* value = xrtValueObject();
    bool ok = value && MdoAccountSetString(value,"type",complete ? "end" : "error") &&
        MdoAccountSetString(value,"id",request->Id);
    if (complete) ok = ok && MdoAccountSetUInt(value,"bytes",request->Sent);
    else ok = ok && MdoAccountSetString(value,"code","remote_request_failed") &&
        MdoAccountSetString(value,"outcome",started && strcmp(request->Method,"GET") &&
            strcmp(request->Method,"HEAD") ? "uncertain" : "not_started");
    if (ok) (void)MdoBridgeWorkerValue(request,value); else xrtValueRelease(value);
    xrtMutexLock(bridge->Lock); request->Completed = true;
    xrtCondBroadcast(bridge->Changed); xrtMutexUnlock(bridge->Lock);
    return 0;
}

static bool MdoBridgeLiveMessage(bool Binary, xbytesview Message, void* Data)
{
    MdoBridgeRequest* request = Data; uint8 digest[32]; xsha256 hash; char hex[65];
    static const char digits[] = "0123456789abcdef";
    if (Binary || Message.Size > MDO_BRIDGE_LIVE_LIMIT || request->LiveSequence == MDO_REMOTE_SEQUENCE_MAX) return false;
    xrtMutexLock(request->Bridge->Lock); uint64 offset = request->Sent; xrtMutexUnlock(request->Bridge->Lock);
    if (offset > MDO_REMOTE_SEQUENCE_MAX-Message.Size) return false;
    xrtSha256Init(&hash);
    bool ok = xrtSha256Update(&hash,Message.Data,Message.Size) && xrtSha256Final(&hash,digest);
    xrtSecureZero(&hash,sizeof(hash));
    if (!ok) return false;
    for (size_t i = 0u; i < 32u; i++) { hex[i*2u] = digits[digest[i] >> 4u]; hex[i*2u+1u] = digits[digest[i] & 15u]; }
    hex[64] = 0; xrtSecureZero(digest,sizeof(digest)); ++request->LiveSequence;
    xvalue* value = xrtValueObject();
    ok = value && MdoAccountSetString(value,"type","live_event") && MdoAccountSetString(value,"id",request->Id) &&
        MdoAccountSetUInt(value,"sequence",request->LiveSequence) && MdoAccountSetUInt(value,"offset",offset) &&
        MdoAccountSetUInt(value,"bytes",Message.Size) && MdoAccountSetString(value,"sha256",hex);
    if (ok) ok = MdoBridgeWorkerValue(request,value); else xrtValueRelease(value);
    for (size_t at = 0u; ok && at < Message.Size;) {
        size_t size = Message.Size-at; if (size > 16384u) size = 16384u;
        ok = MdoBridgeBody(request,(xbytesview){Message.Data+at,size},3u); at += size;
    }
    if (!ok) return false;
    value = xrtValueObject();
    ok = value && MdoAccountSetString(value,"type","live_event_end") && MdoAccountSetString(value,"id",request->Id) &&
        MdoAccountSetUInt(value,"sequence",request->LiveSequence) && MdoAccountSetUInt(value,"offset",offset+Message.Size);
    if (!ok) { xrtValueRelease(value); return false; }
    return MdoBridgeWorkerValue(request,value);
}
static bool MdoBridgeServerCurrent(MdoRemoteBridge* Bridge)
{
    XS_ServerInfo* current = xsServerFind(Bridge->Server->Name);
    bool ok = current == Bridge->Server; xsServerRelease(current); return ok;
}
static void MdoBridgeCommandRelease(MdoBridgeCommand* Command)
{
    if (!Command) return;
    xrtSecureZero(Command->Bytes,Command->Size); xrtFree(Command);
}
static int32 MdoBridgeLiveWorker(void* Data)
{
    MdoBridgeRequest* request = Data; MdoRemoteBridge* bridge = request->Bridge;
    char origin[80], protocols[96]; uint16 status = 0u;
    snprintf(origin,sizeof(origin),"http://127.0.0.1:%u",(unsigned)bridge->Server->PortBound);
    snprintf(protocols,sizeof(protocols),"mdo.live.v1, mdo.token.%s",request->FieldStorage);
    MdoRemoteSocketConfig config = {"127.0.0.1",bridge->Server->PortBound,false,
        "/api/v1/live",origin,protocols,"mdo.live.v1",MDO_BRIDGE_LIVE_LIMIT};
    MdoRemoteSocket* socket = MdoBridgeServerCurrent(bridge) ?
        MdoRemoteSocketOpen(bridge->Net,&config,request->Cancel,&status) : NULL;
    xrtSecureZero(protocols,sizeof(protocols)); xrtSecureZero(request->FieldStorage,sizeof(request->FieldStorage));
    if (socket && !MdoBridgeServerCurrent(bridge)) { MdoRemoteSocketDestroy(socket); socket = NULL; }
    xvalue* value = xrtValueObject();
    bool ok = socket && value && MdoAccountSetString(value,"type","live_opened") && MdoAccountSetString(value,"id",request->Id);
    if (ok) ok = MdoBridgeWorkerValue(request,value); else xrtValueRelease(value);
    while (ok && !xrtCancelRequested(request->Cancel) && MdoBridgeServerCurrent(bridge)) {
        xrtMutexLock(bridge->Lock); MdoBridgeCommand* command = request->Commands;
        if (command) {
            request->Commands = command->Next; if (!request->Commands) request->LastCommand = NULL;
            --request->CommandCount;
        }
        xrtMutexUnlock(bridge->Lock);
        if (command) ok = MdoRemoteSocketSend(socket,false,(xbytesview){(const uint8*)command->Bytes,command->Size});
        MdoBridgeCommandRelease(command);
        ok = ok && MdoRemoteSocketPoll(socket,MdoBridgeLiveMessage,request);
        xrtMutexLock(bridge->Lock);
        if (ok && !request->Commands && MdoBridgeRequestLive(request)) xrtCondWaitFor(bridge->Changed,bridge->Lock,25000u);
        xrtMutexUnlock(bridge->Lock);
    }
    value = xrtValueObject(); uint16 code = MdoRemoteSocketCloseCode(socket);
    ok = value && MdoAccountSetString(value,"type","live_closed") && MdoAccountSetString(value,"id",request->Id) &&
        MdoAccountSetUInt(value,"code",code ? code : 1006u) && MdoAccountSetUInt(value,"status",status);
    if (ok) (void)MdoBridgeWorkerValue(request,value); else xrtValueRelease(value);
    MdoRemoteSocketDestroy(socket);
    xrtMutexLock(bridge->Lock); request->Completed = true; xrtCondBroadcast(bridge->Changed); xrtMutexUnlock(bridge->Lock);
    return 0;
}

void MdoRemoteBridgeDirect(MdoRemoteBridge* Bridge, MdoRemoteBridgeOffer Offer, void* Data)
{ if (Bridge) { Bridge->Offer=Offer; Bridge->OfferData=Data; } }
cstr MdoRemoteBridgeRuntime(MdoRemoteBridge* Bridge) { return Bridge ? Bridge->Receipts.Runtime : ""; }

MdoRemoteBridge* MdoRemoteBridgeCreate(XS_ServerInfo* Server, MdoRemoteNet* Net)
{
    if (!Server || !Server->Engine || !Net || Net->Engine != Server->Engine) return NULL;
    MdoRemoteBridge* bridge = xrtMalloc(sizeof(*bridge));
    if (!bridge) return NULL;
    memset(bridge,0,sizeof(*bridge)); bridge->Net = Net;
    bridge->Lock = xrtMutexCreate(); bridge->Changed = xrtCondCreate(); bridge->Server = xsServerRetain(Server);
    if (!bridge->Lock || !bridge->Changed || !bridge->Server || !MdoRemoteReceiptsInit(&bridge->Receipts)) {
        MdoRemoteBridgeDestroy(bridge); return NULL;
    }
    return bridge;
}
bool MdoRemoteBridgePeerOpen(MdoRemoteBridge* Bridge, cstr Peer, bool ReadOnly, xcancel* ConnectionCancel)
{
    if (!Bridge || !MdoRemoteIdValid(Peer) || !ConnectionCancel) return false;
    xrtMutexLock(Bridge->Lock); size_t slot = SIZE_MAX;
    if (!Bridge->Stopping && MdoBridgePeerFind(Bridge,Peer) == SIZE_MAX)
        for (size_t i = 0u; i < MDO_BRIDGE_PEERS; i++) if (!Bridge->Peers[i].Active) { slot = i; break; }
    bool ok = slot != SIZE_MAX;
    if (ok) {
        MdoBridgePeer* peer = &Bridge->Peers[slot];
        xrtCancelDestroy(peer->Cancel); peer->Cancel = xrtCancelChild(ConnectionCancel);
        ++peer->Generation; strcpy(peer->Id,Peer); peer->ReadOnly = ReadOnly;
        peer->Active = peer->Cancel && MdoBridgeHex(Peer,16u,peer->Bytes);
        xvalue* hello = xrtValueObject();
        ok = peer->Active && hello && MdoAccountSetString(hello,"type","hello") &&
            MdoAccountSetUInt(hello,"version",1u) && MdoAccountSetString(hello,"runtime_id",Bridge->Receipts.Runtime) &&
            MdoAccountSetString(hello,"mode",ReadOnly ? "view" : "control") &&
            MdoAccountSetUInt(hello,"upload_limit",MDO_BRIDGE_UPLOAD) &&
            MdoAccountSetUInt(hello,"chunk_limit",MDO_BRIDGE_CHUNK) &&
            MdoAccountSetUInt(hello,"window_bytes",MDO_BRIDGE_WINDOW) && MdoAccountSetBool(hello,"live",true) &&
            MdoAccountSetUInt(hello,"window_max",MDO_BRIDGE_WINDOW_MAX) &&
            MdoAccountSetUInt(hello,"live_limit",MDO_BRIDGE_LIVE_LIMIT);
        if (ok && Bridge->Offer) {
            xvalue* offer = Bridge->Offer(Peer,ReadOnly,Bridge->OfferData);
            if (offer && !xrtValueObjectSetNew(hello,XRT_STR_LITERAL("direct"),offer)) ok = false;
        }
        if (ok) ok = MdoBridgeValue(Bridge,slot,NULL,hello); else xrtValueRelease(hello);
        if (!ok) { peer->Active = false; xrtCancelRequest(peer->Cancel); }
    }
    xrtMutexUnlock(Bridge->Lock); return ok;
}
static void MdoBridgePeerStop(MdoRemoteBridge* Bridge, size_t Peer)
{
    Bridge->Peers[Peer].Active = false; xrtCancelRequest(Bridge->Peers[Peer].Cancel);
    for (size_t i = 0u; i < MDO_BRIDGE_REQUESTS; i++) {
        MdoBridgeRequest* request = &Bridge->Requests[i];
        if (request->Id[0] && request->Peer == Peer && request->PeerGeneration == Bridge->Peers[Peer].Generation)
            MdoBridgeAbandon(request);
    }
    xrtCondBroadcast(Bridge->Changed);
}
void MdoRemoteBridgePeerClose(MdoRemoteBridge* Bridge, cstr Peer)
{
    if (!Bridge || !MdoRemoteIdValid(Peer)) return;
    xrtMutexLock(Bridge->Lock); size_t peer = MdoBridgePeerFind(Bridge,Peer);
    if (peer != SIZE_MAX) MdoBridgePeerStop(Bridge,peer);
    xrtMutexUnlock(Bridge->Lock);
}
void MdoRemoteBridgeDisconnect(MdoRemoteBridge* Bridge)
{
    if (!Bridge) return;
    xrtMutexLock(Bridge->Lock);
    for (size_t i = 0u; i < MDO_BRIDGE_PEERS; i++) MdoBridgePeerStop(Bridge,i);
    /* No peer can consume these frames. Clear them now, preserving request
     * counters so cancelled workers can be reclaimed during offline ticks. */
    MdoBridgePacket* packet = Bridge->First;
    while (packet) {
        MdoBridgePacket* next = packet->Next;
        if (packet->Slot < MDO_BRIDGE_REQUESTS && Bridge->Requests[packet->Slot].Serial == packet->RequestSerial)
            --Bridge->Requests[packet->Slot].Packets;
        xrtSecureZero(packet->Bytes,packet->Size); xrtFree(packet); packet = next;
    }
    Bridge->First = Bridge->Last = NULL; Bridge->Bytes = Bridge->Packets = 0u;
    xrtCondBroadcast(Bridge->Changed);
    xrtMutexUnlock(Bridge->Lock);
}
static void MdoBridgeRequestRelease(MdoBridgeRequest* Request)
{
    if (Request->Thread) { xrtThreadWait(Request->Thread); xrtThreadDestroy(Request->Thread); }
    xrtCancelDestroy(Request->Cancel);
    MdoBridgeCommand* command = Request->Commands;
    while (command) { MdoBridgeCommand* next = command->Next; MdoBridgeCommandRelease(command); command = next; }
    if (Request->Upload) xrtSecureZero(Request->Upload,Request->Request.Body.Size);
    xrtFree(Request->Upload);
    Request->Bridge->UploadBytes -= Request->Request.Body.Size;
    xrtSecureZero(Request,sizeof(*Request));
}
bool MdoRemoteBridgePump(MdoRemoteBridge* Bridge, MdoRemoteBridgeEmit Emit, void* Data)
{
    if (!Bridge) return false;
    for (size_t i = 0u; i < 16u; i++) {
        xrtMutexLock(Bridge->Lock); MdoBridgePacket* packet = Bridge->First;
        if (!packet) { xrtMutexUnlock(Bridge->Lock); break; }
        Bridge->First = packet->Next; if (!Bridge->First) Bridge->Last = NULL;
        Bridge->Bytes -= packet->Size; --Bridge->Packets;
        bool live = Bridge->Peers[packet->Peer].Active &&
            Bridge->Peers[packet->Peer].Generation == packet->PeerGeneration;
        xrtMutexUnlock(Bridge->Lock);
        bool sent = !live || (Emit && Emit((xbytesview){packet->Bytes,packet->Size},Data));
        xrtMutexLock(Bridge->Lock);
        if (packet->Slot < MDO_BRIDGE_REQUESTS && Bridge->Requests[packet->Slot].Serial == packet->RequestSerial)
            --Bridge->Requests[packet->Slot].Packets;
        xrtCondBroadcast(Bridge->Changed); xrtMutexUnlock(Bridge->Lock);
        xrtSecureZero(packet->Bytes,packet->Size); xrtFree(packet);
        if (!sent) return false;
    }
    xrtMutexLock(Bridge->Lock);
    for (size_t i = 0u; i < MDO_BRIDGE_REQUESTS; i++) {
        MdoBridgeRequest* request = &Bridge->Requests[i];
        if (!request->Id[0]) continue;
        if (!request->Thread && !request->Completed && xrtDeadlineExpired(request->Until)) {
            (void)MdoBridgeError(Bridge,request->Peer,request->Id,"upload_timeout","not_started");
            MdoBridgeAbandon(request);
        }
        if (request->Completed && !request->Packets && (!request->Thread || xrtThreadState(request->Thread) == XTHREAD_FINISHED))
            MdoBridgeRequestRelease(request);
    }
    xrtMutexUnlock(Bridge->Lock); return true;
}
void MdoRemoteBridgeDestroy(MdoRemoteBridge* Bridge)
{
    if (!Bridge) return;
    if (Bridge->Lock) {
        xrtMutexLock(Bridge->Lock); Bridge->Stopping = true;
        for (size_t i = 0u; i < MDO_BRIDGE_PEERS; i++) {
            Bridge->Peers[i].Active = false; xrtCancelRequest(Bridge->Peers[i].Cancel);
        }
        for (size_t i = 0u; i < MDO_BRIDGE_REQUESTS; i++) xrtCancelRequest(Bridge->Requests[i].Cancel);
        if (Bridge->Changed) xrtCondBroadcast(Bridge->Changed);
        xrtMutexUnlock(Bridge->Lock);
    }
    for (size_t i = 0u; i < MDO_BRIDGE_REQUESTS; i++) if (Bridge->Requests[i].Id[0]) MdoBridgeRequestRelease(&Bridge->Requests[i]);
    MdoBridgePacket* packet = Bridge->First;
    while (packet) {
        MdoBridgePacket* next = packet->Next;
        xrtSecureZero(packet->Bytes,packet->Size); xrtFree(packet); packet = next;
    }
    for (size_t i = 0u; i < MDO_BRIDGE_PEERS; i++) xrtCancelDestroy(Bridge->Peers[i].Cancel);
    xsServerRelease(Bridge->Server); xrtCondDestroy(Bridge->Changed); xrtMutexDestroy(Bridge->Lock);
    xrtSecureZero(Bridge,sizeof(*Bridge)); xrtFree(Bridge);
}
