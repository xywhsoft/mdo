#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "write_admission.h"
#include "../../include/mdo/session_backup.h"
#include "../account/internal.h"

static bool MdoApiConnectionSend(MdoApiContext* Context, const void* pData,
    size_t Size)
{
    size_t Written = 0u;
    XS_HttpReq* pRequest = Context != NULL ? Context->Request : NULL;

    if ( pRequest == NULL || (pData == NULL && Size != 0u) ) return false;
    if ( Size == 0u ) return true;
    if ( Context->SendDeadline != 0u ) return MdoApiDownloadSend(Context, pData, Size);
    if ( pRequest->tls != NULL ) {
        return xrtTlsStreamSend(pRequest->tls, pData, Size, &Written) ==
            XTLS_OK && Written == Size;
    }
    return pRequest->tcp != NULL &&
        xrtNetStreamSend(pRequest->tcp, pData, Size) == XNET_RESULT_OK;
}

static bool MdoApiReplyRaw(MdoApiContext* pContext, uint16 Status,
    const void* pBody, size_t BodySize, cstr Allow, cstr EntityTag,
    cstr ContentType, cstr ContentDisposition)
{
    char Head[1536];
    char Length[32];
    xhttpfield Fields[11];
    char WriteToken[MDO_API_WRITE_TOKEN_CAPACITY];
    size_t FieldCount = 0u;
    size_t HeadSize = 0u;
    xstrview Reason;

    if ( pContext == NULL || pContext->Request == NULL ||
         pContext->Request->head == NULL ||
         (pBody == NULL && BodySize != 0u) ) return false;
    (void)snprintf(Length, sizeof(Length), "%llu",
        (unsigned long long)BodySize);
    Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("Content-Length"), xrtStrView(Length) };
    Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("Content-Type"),
        xrtStrView(ContentType != NULL ? ContentType :
            "application/json; charset=utf-8") };
    Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("Cache-Control"), XRT_STR_LITERAL("no-store") };
    Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("X-Content-Type-Options"), XRT_STR_LITERAL("nosniff") };
    Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("Referrer-Policy"), XRT_STR_LITERAL("no-referrer") };
    Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("X-Request-Id"), xrtStrView(pContext->RequestId) };
    if ( MdoApiWriteToken(WriteToken) ) Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("X-Mdo-Write-Token"), xrtStrView(WriteToken) };
    if ( Allow != NULL ) {
        Fields[FieldCount++] = (xhttpfield){
            XRT_STR_LITERAL("Allow"), xrtStrView(Allow) };
    }
    if ( EntityTag != NULL ) {
        Fields[FieldCount++] = (xhttpfield){
            XRT_STR_LITERAL("ETag"), xrtStrView(EntityTag) };
    }
    if ( ContentDisposition != NULL ) {
        Fields[FieldCount++] = (xhttpfield){
            XRT_STR_LITERAL("Content-Disposition"),
            xrtStrView(ContentDisposition) };
    }
    if ( pContext->CloseResponse ) Fields[FieldCount++] = (xhttpfield){
        XRT_STR_LITERAL("Connection"), XRT_STR_LITERAL("close") };
    Reason = xrtHttpStatusText(Status);
    if ( !xrtHttp1ResponseWrite(XHTTP_VERSION_1_1, Status, Reason,
            Fields, FieldCount, Head, sizeof(Head), &HeadSize) ||
         !MdoApiConnectionSend(pContext, Head, HeadSize) ) {
        return false;
    }
    if ( BodySize != 0u &&
         pContext->Request->head->MethodCode != XHTTP_METHOD_HEAD ) {
        return MdoApiConnectionSend(pContext, pBody, BodySize);
    }
    return true;
}

bool MdoApiReplyAccountHtml(MdoApiContext* Context, uint16 Status, cstr Html)
{
    return MdoApiReplyRaw(Context, Status, Html, strlen(Html), NULL, NULL,
        "text/html; charset=utf-8", NULL);
}

static bool MdoApiEnvelopeBase(xvalue* pEnvelope, MdoApiContext* pContext,
    bool Ok)
{
    return pEnvelope != NULL && pContext != NULL &&
        xrtValueObjectSetNew(pEnvelope, XRT_STR_LITERAL("schema_version"),
            xrtValueUInt(MDO_API_SCHEMA_VERSION)) &&
        xrtValueObjectSetNew(pEnvelope, XRT_STR_LITERAL("ok"),
            xrtValueBool(Ok)) &&
        xrtValueObjectSetNew(pEnvelope, XRT_STR_LITERAL("request_id"),
            xrtValueString(xrtStrView(pContext->RequestId)));
}

static bool MdoApiReplySerializationFailure(MdoApiContext* pContext,
    uint16 Status)
{
    char Body[384];
    int Count = snprintf(Body, sizeof(Body),
        "{\"schema_version\":1,\"ok\":false,\"request_id\":\"%s\","
        "\"error\":{\"code\":\"serialization_failed\","
        "\"message\":\"Response serialization failed\"}}",
        pContext != NULL ? pContext->RequestId : "unavailable");

    if ( Count < 0 || (size_t)Count >= sizeof(Body) ) return false;
    return MdoApiReplyRaw(pContext, Status, Body, (size_t)Count, NULL, NULL,
        NULL, NULL);
}

static bool MdoApiReplyValue(MdoApiContext* pContext, uint16 Status,
    xvalue* pEnvelope, cstr Allow, cstr EntityTag)
{
    str Json;
    size_t JsonSize = 0u;
    bool Result;

    if ( pEnvelope == NULL )
        return MdoApiReplySerializationFailure(pContext, 500u);
    Json = xrtJsonStringify(pEnvelope, false, &JsonSize);
    xrtValueRelease(pEnvelope);
    /* Community source downloads are bounded separately from ordinary API
     * envelopes; all other routes retain their existing small response cap. */
    size_t Limit = xrtStrEqual(pContext->Target.Path,xrtStrView("/api/v1/ecosystem"))
        ? 1152u * 1024u : MDO_API_RESPONSE_MAX_BYTES;
    if ( Json == NULL || JsonSize > Limit ) {
        xrtFree(Json);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    Result = MdoApiReplyRaw(pContext, Status, Json, JsonSize, Allow,
        EntityTag, NULL, NULL);
    xrtFree(Json);
    return Result;
}

bool MdoApiReplyDownload(MdoApiContext* pContext, const void* pBody,
    size_t BodySize, cstr ContentDisposition, cstr EntityTag)
{
    if ( pBody == NULL || BodySize == 0u ||
         BodySize > MDO_API_DOWNLOAD_MAX_BYTES ||
         ContentDisposition == NULL || ContentDisposition[0] == '\0' ||
         EntityTag == NULL || EntityTag[0] == '\0' ) return false;
    return MdoApiReplyRaw(pContext, 200u, pBody, BodySize, NULL, EntityTag,
        "application/octet-stream", ContentDisposition);
}

bool MdoApiReplyImage(MdoApiContext* pContext, const void* pBody,
    size_t BodySize, cstr ContentType)
{
    if ( pBody == NULL || BodySize == 0u ||
         BodySize > MDO_API_IMAGE_MAX_BYTES || ContentType == NULL )
        return false;
    return MdoApiReplyRaw(pContext, 200u, pBody, BodySize, NULL, NULL,
        ContentType, NULL);
}

bool MdoApiReplyBackupDownload(MdoApiContext* Context, const void* Body,
    size_t Bytes, cstr Disposition, cstr EntityTag)
{
    if ( Context == NULL || Context->SendDeadline == 0u ||
         !Context->CloseResponse || Body == NULL || Bytes == 0u ||
         Bytes > MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES ||
         Disposition == NULL || EntityTag == NULL ) return false;
    return MdoApiReplyRaw(Context, 200u, Body, Bytes, NULL, EntityTag,
        "application/json; charset=utf-8", Disposition);
}

bool MdoApiReplySuccessTake(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, cstr Allow)
{
    xvalue* Envelope = xrtValueObject();

    if ( pData == NULL || Envelope == NULL ||
         !MdoApiEnvelopeBase(Envelope, pContext, true) ) {
        xrtValueRelease(pData);
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    if ( !xrtValueObjectSetNew(Envelope, XRT_STR_LITERAL("data"), pData) ) {
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    return MdoApiReplyValue(pContext, Status, Envelope, Allow, NULL);
}

/* Private ticket reply: transfer, serialize once, then clear both the value
 * and JSON buffer. No shared snapshot/scalar may be passed to this function. */
bool MdoApiReplySecretSuccessTake(MdoApiContext* Context, xvalue* Data)
{
    xvalue* envelope = xrtValueObject(); size_t size = 0u; char* json = NULL;
    if (!Data || !envelope || !MdoApiEnvelopeBase(envelope,Context,true)) {
        MdoAccountSecretValueRelease(Data); xrtValueRelease(envelope);
        return MdoApiReplySerializationFailure(Context,500u);
    }
    if (xrtValueObjectSetNew(envelope,XRT_STR_LITERAL("data"),Data)) json = xrtJsonStringify(envelope,false,&size);
    MdoAccountSecretValueRelease(envelope);
    bool ok = json && size <= MDO_API_RESPONSE_MAX_BYTES ?
        MdoApiReplyRaw(Context,200u,json,size,NULL,NULL,NULL,NULL) : MdoApiReplySerializationFailure(Context,500u);
    if (json) xrtSecureZero(json,size);
    xrtFree(json); return ok;
}

bool MdoApiReplySuccessTakeEntityTag(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, cstr EntityTag)
{
    xvalue* Envelope = xrtValueObject();
    if ( pData == NULL || EntityTag == NULL || EntityTag[0] == '\0' ||
         Envelope == NULL || !MdoApiEnvelopeBase(Envelope, pContext, true) ) {
        xrtValueRelease(pData);
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    if ( !xrtValueObjectSetNew(Envelope, XRT_STR_LITERAL("data"), pData) ) {
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    return MdoApiReplyValue(pContext, Status, Envelope, NULL, EntityTag);
}

bool MdoApiReplySuccessTakeRevision(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, uint64 Revision)
{
    char EntityTag[64];
    int Count = snprintf(EntityTag, sizeof(EntityTag),
        "\"mdo-config-%llu\"", (unsigned long long)Revision);

    if ( Count <= 0 || (size_t)Count >= sizeof(EntityTag) ) {
        xrtValueRelease(pData);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    return MdoApiReplySuccessTakeEntityTag(pContext, Status, pData,
        EntityTag);
}

static bool MdoApiReplyErrorImpl(MdoApiContext* pContext, uint16 Status, cstr Code,
    cstr Message, cstr Allow, xvalue* Details)
{
    xvalue* Envelope = xrtValueObject();
    xvalue* Error = xrtValueObject();

    if ( Error == NULL || Envelope == NULL ||
         !xrtValueObjectSetNew(Error, XRT_STR_LITERAL("code"),
            xrtValueString(xrtStrView(Code != NULL ? Code : "internal_error"))) ||
         !xrtValueObjectSetNew(Error, XRT_STR_LITERAL("message"),
            xrtValueString(xrtStrView(Message != NULL ? Message :
                "Internal server error"))) ) {
        xrtValueRelease(Details);
        xrtValueRelease(Error);
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    if ( Details != NULL && !xrtValueObjectSetNew(Error, XRT_STR_LITERAL("details"), Details) ) {
        xrtValueRelease(Error);
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    if ( !MdoApiEnvelopeBase(Envelope, pContext, false) ) {
        xrtValueRelease(Error);
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    if ( !xrtValueObjectSetNew(Envelope, XRT_STR_LITERAL("error"), Error) ) {
        xrtValueRelease(Envelope);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    return MdoApiReplyValue(pContext, Status, Envelope, Allow, NULL);
}

bool MdoApiReplyError(MdoApiContext* Context, uint16 Status, cstr Code,
    cstr Message, cstr Allow)
{
    return MdoApiReplyErrorImpl(Context, Status, Code, Message, Allow, NULL);
}

bool MdoApiReplyErrorDetailsTake(MdoApiContext* Context, uint16 Status,
    cstr Code, cstr Message, xvalue* Details)
{
    return MdoApiReplyErrorImpl(Context, Status, Code, Message, NULL, Details);
}

bool MdoApiReplyOptions(MdoApiContext* pContext, cstr Allow)
{
    xvalue* Data = xrtValueObject();

    if ( Data == NULL ||
         !xrtValueObjectSetNew(Data, XRT_STR_LITERAL("allow"),
            xrtValueString(xrtStrView(Allow != NULL ? Allow : "OPTIONS"))) ) {
        xrtValueRelease(Data);
        return MdoApiReplySerializationFailure(pContext, 500u);
    }
    return MdoApiReplySuccessTake(pContext, 200u, Data, Allow);
}
