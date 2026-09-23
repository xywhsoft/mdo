#include <stdio.h>
#include <string.h>

#include "internal.h"

static bool MdoApiConnectionSend(XS_HttpReq* pRequest, const void* pData,
    size_t Size)
{
    size_t Written = 0u;

    if ( pRequest == NULL || (pData == NULL && Size != 0u) ) return false;
    if ( Size == 0u ) return true;
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
    xhttpfield Fields[9];
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
    Reason = xrtHttpStatusText(Status);
    if ( !xrtHttp1ResponseWrite(XHTTP_VERSION_1_1, Status, Reason,
            Fields, FieldCount, Head, sizeof(Head), &HeadSize) ||
         !MdoApiConnectionSend(pContext->Request, Head, HeadSize) ) {
        return false;
    }
    if ( BodySize != 0u &&
         pContext->Request->head->MethodCode != XHTTP_METHOD_HEAD ) {
        return MdoApiConnectionSend(pContext->Request, pBody, BodySize);
    }
    return true;
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
    if ( Json == NULL || JsonSize > MDO_API_RESPONSE_MAX_BYTES ) {
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

bool MdoApiReplyError(MdoApiContext* pContext, uint16 Status, cstr Code,
    cstr Message, cstr Allow)
{
    xvalue* Envelope = xrtValueObject();
    xvalue* Error = xrtValueObject();

    if ( Error == NULL || Envelope == NULL ||
         !xrtValueObjectSetNew(Error, XRT_STR_LITERAL("code"),
            xrtValueString(xrtStrView(Code != NULL ? Code : "internal_error"))) ||
         !xrtValueObjectSetNew(Error, XRT_STR_LITERAL("message"),
            xrtValueString(xrtStrView(Message != NULL ? Message :
                "Internal server error"))) ) {
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
