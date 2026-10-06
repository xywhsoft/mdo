#include <stdio.h>
#include <string.h>
#include "net.h"

#define MDO_REMOTE_HEAD 16384u
#define MDO_REMOTE_MESSAGE_MAX (2u * 1024u * 1024u)
#define MDO_REMOTE_SEND_US 2000000u
#define MDO_REMOTE_OPEN_US 15000000u
#define MDO_REMOTE_PING_US 20000000u
#define MDO_REMOTE_IDLE_US 60000000u

struct MdoRemoteSocket {
    xnetstream* Tcp;
    xtlsstream* Tls;
    xcancel* Cancel;
    char* Input;
    char* Message;
    size_t Limit, Capacity, InputSize, MessageSize;
    xwsmessagestate State;
    uint64 LastRead, LastPing;
    uint16 CloseCode;
    bool Ready, Failed, CloseSent, PeerClosed;
};

void MdoRemoteNetUnit(MdoRemoteNet* Net)
{
    if (!Net) return;
    if (Net->Resolver) (void)xrtNetResolverDestroy(Net->Resolver);
    xrtTlsVerifierRelease(Net->Verifier); xrtTlsContextRelease(Net->Tls);
    memset(Net,0,sizeof(*Net));
}
bool MdoRemoteNetInit(MdoRemoteNet* Net, xnetengine* Engine, const xx509store* Trust)
{
    xtlscontextconfig tls; xtlsverifierconfig verify;
    xnetresolverconfig resolve; xx509store* system = NULL;
    if (!Net || !Engine) return false;
    memset(Net,0,sizeof(*Net)); Net->Engine = Engine;
    xrtNetResolverConfigInit(&resolve); resolve.Workers = 1u;
    resolve.RequestLimit = 32u; resolve.QueryLimit = 16u; resolve.CacheEntries = 16u;
    Net->Resolver = xrtNetResolverCreate(&resolve);
    xrtTlsContextConfigInit(&tls); Net->Tls = xrtTlsContextCreate(&tls);
    if (!Trust) { system = xrtX509StoreSystem(); Trust = system; }
    xrtTlsVerifierConfigInit(&verify); verify.Store = Trust;
    if (Trust) Net->Verifier = xrtTlsVerifierCreate(&verify);
    xrtX509StoreFree(system);
    if (Net->Resolver && Net->Tls && Net->Verifier) return true;
    MdoRemoteNetUnit(Net); return false;
}
static bool MdoRemoteFuture(xfuture* Future, xdeadline Deadline, xcancel* Cancel)
{
    bool ok = Future && xrtFutureWaitUntilCancel(Future,Deadline,Cancel) == XWAIT_OK &&
        xrtFutureState(Future) == XFUTURE_RESOLVED;
    if (!ok && Future) (void)xrtFutureCancel(Future);
    return ok;
}
static bool MdoRemoteSocketAlive(MdoRemoteSocket* Socket)
{
    return Socket && !Socket->Failed && !xrtCancelRequested(Socket->Cancel) &&
        (Socket->Tls ? xrtTlsStreamState(Socket->Tls) == XTLS_STREAM_OPEN :
        Socket->Tcp && xrtNetStreamState(Socket->Tcp) == XNET_STREAM_OPEN);
}
static size_t MdoRemoteAvailable(MdoRemoteSocket* Socket)
{
    return Socket->Tls ? xrtTlsStreamAvailable(Socket->Tls) : xrtNetStreamAvailable(Socket->Tcp);
}
static bool MdoRemoteRawSend(MdoRemoteSocket* Socket, const void* Data, size_t Size, xdeadline Until)
{
    size_t offset = 0u;
    while (offset < Size) {
        size_t chunk = Size - offset; bool ok;
        if (!MdoRemoteSocketAlive(Socket) || xrtDeadlineExpired(Until)) return false;
        if (chunk > 16384u) chunk = 16384u;
        if (Socket->Tls) {
            xfuture* sent = xrtTlsStreamSendAsync(Socket->Tls,(const char*)Data+offset,chunk);
            ok = MdoRemoteFuture(sent,Until,Socket->Cancel); xrtFutureDestroy(sent);
            if (!ok) return false;
            xfuture* drain = xrtTlsStreamWaitAsync(Socket->Tls,XTLS_STREAM_WAIT_DRAIN);
            ok = MdoRemoteFuture(drain,Until,Socket->Cancel); xrtFutureDestroy(drain);
            if (!ok) return false;
        } else {
            size_t limit = xrtNetStreamWriteLimit(Socket->Tcp);
            if (!limit) return false;
            if (chunk > limit) chunk = limit;
            xnetresult result = xrtNetStreamSend(Socket->Tcp,(const char*)Data+offset,chunk);
            if (result != XNET_RESULT_OK && result != XNET_RESULT_AGAIN) return false;
            if (!xrtNetStreamWait(Socket->Tcp,XNET_STREAM_WAIT_DRAIN,Until,Socket->Cancel)) return false;
            if (result == XNET_RESULT_AGAIN) continue;
        }
        offset += chunk;
    }
    return true;
}
static bool MdoRemoteRead(MdoRemoteSocket* Socket, xdeadline Until)
{
    size_t capacity = Socket->Capacity - Socket->InputSize;
    if (!capacity || xrtCancelRequested(Socket->Cancel)) return false;
    xfuture* future = Socket->Tls ? xrtTlsStreamRecvAsync(Socket->Tls,capacity) :
        xrtNetStreamRecvAsync(Socket->Tcp,capacity);
    bool ok = MdoRemoteFuture(future,Until,Socket->Cancel);
    xnetbytes* bytes = ok ? (xnetbytes*)xrtFutureValue(future) : NULL;
    xbytesview view = bytes ? xrtNetBytesView(bytes) : (xbytesview){0};
    ok = ok && bytes && view.Size && view.Size <= capacity;
    if (ok) { memcpy(Socket->Input+Socket->InputSize,view.Data,view.Size); Socket->InputSize += view.Size; }
    /* Copy while Future owns Bytes; never keep its borrowed result. */
    xrtFutureDestroy(future); return ok;
}
static bool MdoRemoteFrame(MdoRemoteSocket* Socket, uint8 Opcode, const void* Data, size_t Size)
{
    xwsframe frame; size_t head = 0u; bool ok = false;
    if (Size > Socket->Limit || (Size && !Data)) return false;
    char* wire = xrtMalloc(Size+XWS_FRAME_HEAD_MAX);
    if (!wire) return false;
    xrtWsFrameInit(&frame); frame.Opcode = Opcode;
    frame.Flags = XWS_FRAME_FIN | XWS_FRAME_MASKED; frame.PayloadSize = Size;
    if (!xrtSecureRandom(frame.Mask,sizeof(frame.Mask)) ||
        !xrtWsFrameWrite(&frame,NULL,wire,Size+XWS_FRAME_HEAD_MAX,&head)) goto done;
    if (Size) memcpy(wire+head,Data,Size);
    if (!xrtWsMask(wire+head,Size,frame.Mask,0u)) goto done;
    ok = MdoRemoteRawSend(Socket,wire,head+Size,xrtDeadlineAfter(MDO_REMOTE_SEND_US));
done:
    xrtSecureZero(wire,Size+XWS_FRAME_HEAD_MAX); xrtFree(wire); return ok;
}
static bool MdoRemoteFail(MdoRemoteSocket* Socket, uint16 Code)
{
    char payload[2]; size_t size;
    if (!Socket->CloseSent && xrtWsCloseWrite(Code,xrtStrView(""),payload,sizeof(payload),&size)) {
        Socket->CloseSent = true;
        (void)MdoRemoteFrame(Socket,XWS_OPCODE_CLOSE,payload,size);
    }
    Socket->CloseCode = Code; Socket->Failed = true; return false;
}
void MdoRemoteSocketDestroy(MdoRemoteSocket* Socket)
{
    if (!Socket) return;
    if (Socket->Ready && !Socket->CloseSent && MdoRemoteSocketAlive(Socket))
        (void)MdoRemoteFail(Socket,1000u);
    /* No callbacks contain application code. Abort bounds teardown even if
     * the peer stopped reading; transport refs/futures remain xrt-owned. */
    if (Socket->Tls) { (void)xrtTlsStreamAbort(Socket->Tls); xrtTlsStreamDestroy(Socket->Tls); }
    if (Socket->Tcp) { (void)xrtNetStreamAbort(Socket->Tcp); xrtNetStreamDestroy(Socket->Tcp); }
    xrtCancelDestroy(Socket->Cancel);
    if (Socket->Input) xrtSecureZero(Socket->Input,Socket->Capacity);
    if (Socket->Message) xrtSecureZero(Socket->Message,Socket->Limit);
    xrtFree(Socket->Input); xrtFree(Socket->Message); xrtFree(Socket);
}
MdoRemoteSocket* MdoRemoteSocketOpen(MdoRemoteNet* Net,
    const MdoRemoteSocketConfig* Config, xcancel* Cancel, uint16* HttpStatus)
{
    xfuture* future = NULL; MdoRemoteSocket* socket = NULL;
    xhttpfield fields[32]; xhttp1head response; xhttp1limits limits;
    xwsupgradeclientconfig upgrade; xwsupgrade selected; xwsmessageconfig message;
    char key[XWS_KEY_CAPACITY], host[512], request[MDO_REMOTE_HEAD]; size_t count, size;
    xdeadline until = xrtDeadlineAfter(MDO_REMOTE_OPEN_US);
    if (HttpStatus) *HttpStatus = 0u;
    if (!Net || !Net->Engine || !Net->Resolver || !Config || !Config->Host || !Config->Host[0] ||
        strlen(Config->Host) > 253u || !Config->Port || !Config->Path || Config->Path[0] != '/' ||
        strlen(Config->Path) > 2048u || !Config->Origin || strlen(Config->Origin) > 1024u ||
        !Config->Protocols || strlen(Config->Protocols) > 2048u || !Config->Protocol ||
        !Config->MessageLimit || Config->MessageLimit > MDO_REMOTE_MESSAGE_MAX ||
        (Cancel && xrtCancelRequested(Cancel))) return NULL;
    socket = xrtCalloc(1,sizeof(*socket)); if (!socket) return NULL;
    socket->Limit = Config->MessageLimit;
    socket->Capacity = Config->MessageLimit + XWS_FRAME_HEAD_MAX;
    if (socket->Capacity < MDO_REMOTE_HEAD) socket->Capacity = MDO_REMOTE_HEAD;
    socket->Cancel = xrtCancelChild(Cancel);
    socket->Input = xrtMalloc(socket->Capacity); socket->Message = xrtMalloc(socket->Limit);
    if (!socket->Cancel || !socket->Input || !socket->Message) goto fail;
    if (Config->Secure) {
        xtlsclientconfig tls; xtlsdialconfig dial;
        if (!Net->Tls || !Net->Verifier) goto fail;
        xrtTlsClientConfigInit(&tls); tls.Context = Net->Tls; tls.Verifier = Net->Verifier;
        /* Dial sets DNS SNI and IP VerifyName correctly from Host. */
        xrtTlsDialConfigInit(&dial); dial.Timeout = MDO_REMOTE_OPEN_US;
        future = xrtTlsDialAsync(Net->Engine,Net->Resolver,Config->Host,Config->Port,&tls,&dial,NULL,NULL);
        if (!MdoRemoteFuture(future,until,socket->Cancel)) goto fail;
        socket->Tls = xrtTlsStreamRef((xtlsstream*)xrtFutureValue(future));
    } else {
        xnetdialconfig dial; xrtNetDialConfigInit(&dial); dial.Timeout = MDO_REMOTE_OPEN_US;
        future = xrtNetDialAsync(Net->Engine,Net->Resolver,Config->Host,Config->Port,&dial,NULL,NULL);
        if (!MdoRemoteFuture(future,until,socket->Cancel)) goto fail;
        socket->Tcp = xrtNetStreamRef((xnetstream*)xrtFutureValue(future));
    }
    xrtFutureDestroy(future); future = NULL;
    if (!MdoRemoteSocketAlive(socket) || !xrtWsKeyGenerate(key,sizeof(key))) goto fail;
    if (snprintf(host,sizeof(host),strchr(Config->Host,':') ? "[%s]:%u" : "%s:%u",
        Config->Host,(unsigned)Config->Port) >= (int)sizeof(host)) goto fail;
    if (!xrtWsUpgradeRequestFields(xrtStrView(host),xrtStrView(key),xrtStrView(Config->Protocols),
        xrtStrView(""),fields,32u,&count) || count >= 32u) goto fail;
    fields[count++] = (xhttpfield){XRT_STR_LITERAL("Origin"),xrtStrView(Config->Origin)};
    if (!xrtHttp1RequestWrite(XRT_STR_LITERAL("GET"),xrtStrView(Config->Path),XHTTP_VERSION_1_1,
        fields,count,request,sizeof(request),&size) || !MdoRemoteRawSend(socket,request,size,until)) goto fail;
    xrtSecureZero(request,sizeof(request));
    xrtHttp1LimitsInit(&limits); limits.MaxHead = MDO_REMOTE_HEAD; limits.MaxFields = 32u;
    for (;;) {
        xrtHttp1HeadInit(&response,fields,32u);
        xhttp1status status = xrtHttp1ResponseParse((xbytesview){(const uint8*)socket->Input,socket->InputSize},
            &response,&limits,NULL);
        if (status == XHTTP1_READY) break;
        if (status != XHTTP1_MORE || socket->InputSize >= MDO_REMOTE_HEAD ||
            !MdoRemoteRead(socket,until)) goto fail;
    }
    if (HttpStatus) *HttpStatus = response.Status;
    xrtWsUpgradeClientConfigInit(&upgrade); upgrade.Protocols = xrtStrView(Config->Protocols);
    if (!xrtWsUpgradeResponseCheck(&response,xrtStrView(key),&upgrade,&selected) ||
        !xrtStrEqual(selected.Protocol,xrtStrView(Config->Protocol))) goto fail;
    socket->InputSize -= response.Bytes;
    memmove(socket->Input,socket->Input+response.Bytes,socket->InputSize);
    xrtWsMessageConfigInitSafe(&message); message.MaxSize = socket->Limit;
    if (!xrtWsMessageInit(&socket->State,&message)) goto fail;
    socket->LastRead = socket->LastPing = xrtClock(); socket->Ready = true;
    return socket;
fail:
    xrtSecureZero(request,sizeof(request));
    if (future) { (void)xrtFutureCancel(future); xrtFutureDestroy(future); }
    MdoRemoteSocketDestroy(socket); return NULL;
}
bool MdoRemoteSocketSend(MdoRemoteSocket* Socket, bool Binary, xbytesview Data)
{
    if (!Socket || !Socket->Ready || Socket->CloseSent || !MdoRemoteSocketAlive(Socket) ||
        Data.Size > Socket->Limit || (Data.Size && !Data.Data)) return false;
    bool ok = MdoRemoteFrame(Socket,Binary ? XWS_OPCODE_BINARY : XWS_OPCODE_TEXT,Data.Data,Data.Size);
    if (!ok) Socket->Failed = true;
    return ok;
}
uint16 MdoRemoteSocketCloseCode(const MdoRemoteSocket* Socket)
{
    return Socket ? Socket->CloseCode : 0u;
}
bool MdoRemoteSocketPeerClosed(const MdoRemoteSocket* Socket)
{ return Socket && Socket->PeerClosed; }
bool MdoRemoteSocketPoll(MdoRemoteSocket* Socket, MdoRemoteMessageProc Proc, void* Data)
{
    if (!Socket || !Socket->Ready || Socket->Failed || !Proc || xrtCancelRequested(Socket->Cancel)) return false;
    /* A coalesced upgrade can already fill Input with complete WS frames.
     * Parse those before trying to receive into a zero-capacity buffer. */
    if (Socket->InputSize < Socket->Capacity && MdoRemoteAvailable(Socket) &&
        !MdoRemoteRead(Socket,xrtDeadlineAfter(MDO_REMOTE_SEND_US))) {
        Socket->CloseCode = 1006u; Socket->Failed = true; return false;
    }
    while (Socket->InputSize) {
        xwsframe frame; xwsframeconfig config; xwsmessageinfo info; xwsmessageerrorinfo error = {0};
        xrtWsFrameConfigInit(&config); config.Mask = XWS_MASK_FORBIDDEN;
        config.MaxPayload = XWS_FRAME_PAYLOAD_MAX;
        xwsframestatus status = xrtWsFrameParse((xbytesview){(const uint8*)Socket->Input,Socket->InputSize},
            &frame,&config,NULL);
        if (status == XWS_FRAME_MORE) break;
        if (status != XWS_FRAME_READY) return MdoRemoteFail(Socket,1002u);
        if (frame.PayloadSize > Socket->Limit) return MdoRemoteFail(Socket,1009u);
        size_t size = (size_t)frame.PayloadSize, total = frame.HeadSize + size;
        if (total > Socket->InputSize) break;
        char* payload = Socket->Input + frame.HeadSize;
        if (!xrtWsMessageFrameBegin(&Socket->State,&frame,&info,&error) ||
            !xrtWsMessagePayload(&Socket->State,(xbytesview){(const uint8*)payload,size},&error) ||
            !xrtWsMessageFrameEnd(&Socket->State,&error))
            return MdoRemoteFail(Socket,error.CloseCode ? error.CloseCode : 1002u);
        Socket->LastRead = xrtClock();
        if (frame.Opcode == XWS_OPCODE_CLOSE) {
            Socket->PeerClosed = true;
            Socket->CloseCode = size >= 2u ?
                (uint16)(((uint32)(uint8)payload[0] << 8u) | (uint8)payload[1]) : (uint16)1000u;
            Socket->CloseSent = true; (void)MdoRemoteFrame(Socket,XWS_OPCODE_CLOSE,payload,size);
            Socket->Failed = true; return false;
        } else if (frame.Opcode == XWS_OPCODE_PING) {
            if (!MdoRemoteFrame(Socket,XWS_OPCODE_PONG,payload,size)) { Socket->Failed = true; return false; }
        } else if (frame.Opcode == XWS_OPCODE_TEXT || frame.Opcode == XWS_OPCODE_BINARY ||
            frame.Opcode == XWS_OPCODE_CONTINUATION) {
            if (frame.Opcode != XWS_OPCODE_CONTINUATION) Socket->MessageSize = 0u;
            if (size > Socket->Limit - Socket->MessageSize) return MdoRemoteFail(Socket,1009u);
            if (size) memcpy(Socket->Message+Socket->MessageSize,payload,size);
            Socket->MessageSize += size;
            if ((frame.Flags & XWS_FRAME_FIN) && !Proc(info.Opcode == XWS_OPCODE_BINARY,
                (xbytesview){(const uint8*)Socket->Message,Socket->MessageSize},Data)) return MdoRemoteFail(Socket,1008u);
        }
        Socket->InputSize -= total; memmove(Socket->Input,Socket->Input+total,Socket->InputSize);
    }
    /* A partial frame cannot fill our header + maximum-payload buffer. */
    if (Socket->InputSize == Socket->Capacity) return MdoRemoteFail(Socket,1009u);
    if (!MdoRemoteSocketAlive(Socket)) { Socket->Failed = true; return false; }
    uint64 now = xrtClock();
    if (now - Socket->LastRead >= MDO_REMOTE_IDLE_US) return MdoRemoteFail(Socket,1001u);
    if (now - Socket->LastPing >= MDO_REMOTE_PING_US) {
        if (!MdoRemoteFrame(Socket,XWS_OPCODE_PING,NULL,0u)) { Socket->Failed = true; return false; }
        Socket->LastPing = xrtClock();
    }
    return true;
}
#include "transfer.inc.c"
