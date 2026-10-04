#include <stdio.h>
#include <string.h>
#include "loopback.h"

#define MDO_REMOTE_HTTP_HEAD 16384u
#define MDO_REMOTE_HTTP_SEND 8192u
#define MDO_REMOTE_HTTP_FIELDS 64u
#define MDO_REMOTE_HTTP_US 60000000u

static bool MdoRemoteHttpIn(xstrview Name, const char* const* Names, size_t Count)
{
    for (size_t i = 0u; i < Count; i++) if (xrtStrCaseEqual(Name,xrtStrView(Names[i]))) return true;
    return false;
}
bool MdoRemoteHttpResponseField(xstrview Name)
{
    static const char* const allowed[] = {"Content-Type","Content-Disposition","Cache-Control",
        "ETag","Last-Modified","Content-Range","Accept-Ranges","X-Mdo-Write-Token","Retry-After","Allow"};
    return MdoRemoteHttpIn(Name,allowed,sizeof(allowed)/sizeof(allowed[0]));
}
static bool MdoRemoteHttpPath(xstrview Path, bool ReadOnly)
{
    /* Keep encoded names usable, while enforcing scope on their decoded
     * spelling too. The request itself still forwards the original target. */
    size_t size = 0u; char* path = (char*)xrtPercentDecodeNew(Path,&size);
    bool ok = path && size >= 8u && !memcmp(path,"/api/v1/",8u) &&
        !memchr(path,0,size) && xrtUtf8Valid(xrtStrViewN(path,size),NULL);
    size_t start = 0u;
    for (size_t i = 0u; ok && i <= size; i++) {
        if (i < size && ((unsigned char)path[i] < 32u || (unsigned char)path[i] == 127u || path[i] == '\\')) ok = false;
        if (i == size || path[i] == '/') {
            size_t segment = i-start;
            if ((segment == 1u && path[start] == '.') || (segment == 2u && !memcmp(path+start,"..",2u))) ok = false;
            start = i+1u;
        }
    }
    if (ok && (xrtStrEqual(xrtStrViewN(path,size),XRT_STR_LITERAL("/api/v1/live")) ||
        (ReadOnly && xrtStrEqual(xrtStrViewN(path,size),XRT_STR_LITERAL("/api/v1/account/callback"))))) ok = false;
    xrtFree(path); return ok;
}
bool MdoRemoteHttpRequestValid(const MdoRemoteHttpRequest* Request)
{
    static const char* const headers[] = {"Accept","Content-Type","If-Match","If-None-Match",
        "X-Mdo-Write-Token","Range"};
    if (!Request || !Request->Method || !Request->Target || strlen(Request->Target) > 4096u ||
        strncmp(Request->Target,"/api/v1/",8u) || Request->HeaderCount > 16u ||
        (Request->HeaderCount && !Request->Headers) || Request->Body.Size > MDO_REMOTE_HTTP_BODY_MAX ||
        (Request->Body.Size && !Request->Body.Data)) return false;
    bool read = !strcmp(Request->Method,"GET") || !strcmp(Request->Method,"HEAD");
    if (!read && strcmp(Request->Method,"POST") && strcmp(Request->Method,"PUT") &&
        strcmp(Request->Method,"PATCH") && strcmp(Request->Method,"DELETE")) return false;
    xhttptarget target;
    if (!xrtHttpTargetParse(xrtStrView(Request->Method),xrtStrView(Request->Target),&target) ||
        !xrtHttp1TargetValid(xrtStrView(Request->Target)) || !MdoRemoteHttpPath(target.Path,Request->ReadOnly)) return false;
    if (read && Request->Body.Size) return false;
    if (Request->ReadOnly && !read) return false;
    /* Live WS has its own authenticated subscription adapter, never this HTTP
     * body pump. A read must not accidentally turn into a protocol tunnel. */
    if (xrtStrEqual(target.Path,XRT_STR_LITERAL("/api/v1/live"))) return false;
    size_t bytes = 0u;
    for (size_t i = 0u; i < Request->HeaderCount; i++) {
        const XS_FetchHeader* field = &Request->Headers[i];
        if (!field->Name || !field->Value || strlen(field->Name) > 64u || strlen(field->Value) > 2048u ||
            !MdoRemoteHttpIn(xrtStrView(field->Name),headers,sizeof(headers)/sizeof(headers[0]))) return false;
        bytes += strlen(field->Name)+strlen(field->Value);
        if (bytes > 4096u) return false;
        for (size_t j = 0u; j < strlen(field->Value); j++)
            if ((unsigned char)field->Value[j] < 32u || (unsigned char)field->Value[j] == 127u) return false;
        for (size_t j = 0u; j < i; j++)
            if (xrtStrCaseEqual(xrtStrView(field->Name),xrtStrView(Request->Headers[j].Name))) return false;
    }
    return true;
}
static bool MdoRemoteHttpGeneration(XS_ServerInfo* Server)
{
    XS_ServerInfo* current = xsServerFind(Server->Name);
    bool ok = current == Server; xsServerRelease(current); return ok;
}
static bool MdoRemoteHttpSend(xnetstream* Stream, xbytesview Bytes, xdeadline Until, xcancel* Cancel)
{
    size_t offset = 0u;
    while (offset < Bytes.Size) {
        size_t size = Bytes.Size-offset, limit = xrtNetStreamWriteLimit(Stream);
        if (!limit || xrtDeadlineExpired(Until) || (Cancel && xrtCancelRequested(Cancel))) return false;
        if (size > 16384u) size = 16384u;
        if (size > limit) size = limit;
        xnetresult result = xrtNetStreamSend(Stream,(const uint8*)Bytes.Data+offset,size);
        if (result != XNET_RESULT_OK && result != XNET_RESULT_AGAIN) return false;
        if (!xrtNetStreamWait(Stream,XNET_STREAM_WAIT_DRAIN,Until,Cancel)) return false;
        if (result == XNET_RESULT_OK) offset += size;
    }
    return true;
}
static bool MdoRemoteHttpRead(xnetstream* Stream, char* Input, size_t* Size,
    bool* End, xdeadline Until, xcancel* Cancel)
{
    if (*Size >= MDO_REMOTE_HTTP_HEAD || xrtDeadlineExpired(Until) || (Cancel && xrtCancelRequested(Cancel))) return false;
    xfuture* future = xrtNetStreamRecvAsync(Stream,MDO_REMOTE_HTTP_HEAD-*Size);
    bool ok = future && xrtFutureWaitUntilCancel(future,Until,Cancel) == XWAIT_OK &&
        xrtFutureState(future) == XFUTURE_RESOLVED;
    xnetbytes* bytes = ok ? (xnetbytes*)xrtFutureValue(future) : NULL;
    xbytesview view = bytes ? xrtNetBytesView(bytes) : (xbytesview){0};
    ok = ok && bytes && view.Size <= MDO_REMOTE_HTTP_HEAD-*Size;
    if (ok && view.Size) { memcpy(Input+*Size,view.Data,view.Size); *Size += view.Size; }
    if (!ok && future) (void)xrtFutureCancel(future);
    xrtFutureDestroy(future);
    if (!ok || !view.Size) {
        xnetstreamstats stats;
        if (Cancel && xrtCancelRequested(Cancel)) return false;
        if (xrtNetStreamStats(Stream,&stats) && stats.ReadEnded && !stats.BufferedBytes) { *End = true; return true; }
        return false;
    }
    return true;
}
bool MdoRemoteLoopbackCall(XS_ServerInfo* Server, const MdoRemoteHttpRequest* Request,
    xcancel* Cancel, const MdoRemoteHttpSink* Sink)
{
    if (!Server || !Server->Engine || !Server->PortBound || !Sink || !Sink->Head || !Sink->Body ||
        !MdoRemoteHttpRequestValid(Request) || (Cancel && xrtCancelRequested(Cancel)) ||
        !MdoRemoteHttpGeneration(Server) || xrtNetEngineCurrent(Server->Engine)) return false;
    xnetaddr address; xnetstreamconfig stream_config; xnetstream* stream = NULL; bool ok = false;
    char host[64],origin[80],length[32],request[MDO_REMOTE_HTTP_SEND],input[MDO_REMOTE_HTTP_HEAD];
    size_t request_size = 0u, input_size = 0u, count = 0u; xhttpfield fields[MDO_REMOTE_HTTP_FIELDS];
    xdeadline until = xrtDeadlineAfter(MDO_REMOTE_HTTP_US);
    snprintf(host,sizeof(host),"127.0.0.1:%u",(unsigned)Server->PortBound);
    snprintf(origin,sizeof(origin),"http://%s",host);
    snprintf(length,sizeof(length),"%llu",(unsigned long long)Request->Body.Size);
    fields[count++] = (xhttpfield){XRT_STR_LITERAL("Host"),xrtStrView(host)};
    fields[count++] = (xhttpfield){XRT_STR_LITERAL("Origin"),xrtStrView(origin)};
    fields[count++] = (xhttpfield){XRT_STR_LITERAL("Connection"),XRT_STR_LITERAL("close")};
    fields[count++] = (xhttpfield){XRT_STR_LITERAL("Content-Length"),xrtStrView(length)};
    for (size_t i = 0u; i < Request->HeaderCount; i++)
        fields[count++] = (xhttpfield){xrtStrView(Request->Headers[i].Name),xrtStrView(Request->Headers[i].Value)};
    if (!xrtHttp1RequestWrite(xrtStrView(Request->Method),xrtStrView(Request->Target),XHTTP_VERSION_1_1,
        fields,count,request,sizeof(request),&request_size) || !xrtNetAddrParse(&address,"127.0.0.1",Server->PortBound)) goto done;
    xrtNetStreamConfigInit(&stream_config); stream_config.ConnectTimeout = 2000000u;
    stream_config.ReadLimit = 65536u; stream_config.WriteLimit = 65536u;
    stream_config.WriteHighWater = 32768u; stream_config.WriteLowWater = 16384u;
    stream = xrtNetStreamConnect(Server->Engine,&address,0u,&stream_config,NULL,NULL);
    if (!stream || !xrtNetStreamWait(stream,XNET_STREAM_WAIT_OPEN,until,Cancel) ||
        !MdoRemoteHttpGeneration(Server) ||
        !MdoRemoteHttpSend(stream,(xbytesview){(const uint8*)request,request_size},until,Cancel) ||
        !MdoRemoteHttpSend(stream,Request->Body,until,Cancel)) goto done;
    xrtSecureZero(request,sizeof(request));
    xhttp1head head; xhttp1limits head_limits; bool end = false;
    xrtHttp1LimitsInit(&head_limits); head_limits.MaxHead = sizeof(input); head_limits.MaxFields = MDO_REMOTE_HTTP_FIELDS;
    for (;;) {
        xrtHttp1HeadInit(&head,fields,MDO_REMOTE_HTTP_FIELDS);
        xhttp1status status = xrtHttp1ResponseParse((xbytesview){(const uint8*)input,input_size},&head,&head_limits,NULL);
        if (status == XHTTP1_READY) break;
        if (status != XHTTP1_MORE || end || !MdoRemoteHttpRead(stream,input,&input_size,&end,until,Cancel)) goto done;
    }
    xhttp1bodyplan plan; xhttp1body body; xhttp1bodylimits body_limits; xhttpfield trailers[16];
    xrtHttp1BodyLimitsInit(&body_limits); body_limits.MaxBody = MDO_REMOTE_HTTP_BODY_MAX;
    body_limits.MaxTrailer = 4096u; body_limits.MaxTrailers = 16u;
    if (head.Status < 200u || !xrtHttp1ResponseBodyPlan(&head,xrtStrView(Request->Method),&plan) ||
        plan.Mode == XHTTP1_BODY_TUNNEL || plan.Mode == XHTTP1_BODY_CLOSE ||
        (plan.Mode == XHTTP1_BODY_FIXED && plan.Length > body_limits.MaxBody) ||
        !xrtHttp1BodyInit(&body,&plan,trailers,16u,&body_limits) || !Sink->Head(&head,Sink->Data)) goto done;
    input_size -= head.Bytes; memmove(input,input+head.Bytes,input_size);
    for (;;) {
        size_t consumed = 0u; xbytesview bytes = {0};
        xhttp1bodystatus status = xrtHttp1BodyRead(&body,(xbytesview){(const uint8*)input,input_size},end,&consumed,&bytes,NULL);
        if (status == XHTTP1_BODY_ERROR || status == XHTTP1_BODY_FIELDS || consumed > input_size ||
            (status == XHTTP1_BODY_DATA && !Sink->Body(bytes,Sink->Data))) goto done;
        input_size -= consumed; memmove(input,input+consumed,input_size);
        if (status == XHTTP1_BODY_DONE) { ok = input_size == 0u && MdoRemoteHttpGeneration(Server); break; }
        if (status == XHTTP1_BODY_MORE && (end || !MdoRemoteHttpRead(stream,input,&input_size,&end,until,Cancel))) goto done;
    }
done:
    xrtSecureZero(request,sizeof(request)); xrtSecureZero(input,sizeof(input));
    if (stream) { (void)xrtNetStreamAbort(stream); xrtNetStreamDestroy(stream); }
    return ok;
}
