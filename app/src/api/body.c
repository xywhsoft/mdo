#include <string.h>

#include "internal.h"

static bool MdoApiAsciiEqual(xstrview Left, cstr Right)
{
    size_t Index;
    size_t Size = strlen(Right);

    if ( Left.Size != Size ) return false;
    for ( Index = 0u; Index < Size; Index++ ) {
        unsigned char A = (unsigned char)Left.Data[Index];
        unsigned char B = (unsigned char)Right[Index];
        if ( A >= 'A' && A <= 'Z' ) A = (unsigned char)(A + ('a' - 'A'));
        if ( B >= 'A' && B <= 'Z' ) B = (unsigned char)(B + ('a' - 'A'));
        if ( A != B ) return false;
    }
    return true;
}

static bool MdoApiJsonContentType(xstrview Value)
{
    size_t Separator = 0u;
    xstrview Media;
    xstrview Parameters;

    Value = xrtStrTrim(Value);
    while ( Separator < Value.Size && Value.Data[Separator] != ';' )
        Separator++;
    Media = xrtStrTrim(xrtStrViewN(Value.Data, Separator));
    if ( !MdoApiAsciiEqual(Media, "application/json") ) return false;
    if ( Separator == Value.Size ) return true;
    Parameters = xrtStrTrim(xrtStrViewN(Value.Data + Separator + 1u,
        Value.Size - Separator - 1u));
    if ( Parameters.Size == 0u ) return false;
    {
        size_t Equals = 0u;
        xstrview Name;
        xstrview Charset;
        while ( Equals < Parameters.Size && Parameters.Data[Equals] != '=' )
            Equals++;
        if ( Equals == Parameters.Size ) return false;
        Name = xrtStrTrim(xrtStrViewN(Parameters.Data, Equals));
        Charset = xrtStrTrim(xrtStrViewN(Parameters.Data + Equals + 1u,
            Parameters.Size - Equals - 1u));
        return MdoApiAsciiEqual(Name, "charset") &&
            MdoApiAsciiEqual(Charset, "utf-8");
    }
}

static MdoApiBodyStatus MdoApiBodyType(const MdoApiContext* Context)
{
    const xhttpfield* Field = NULL;
    xhttpnext Next;

    Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("Content-Type"),
        &Field);
    if ( Next == XHTTP_NEXT_ERROR ) return MDO_API_BODY_INVALID;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ||
         !MdoApiJsonContentType(Field->Value) )
        return MDO_API_BODY_UNSUPPORTED_TYPE;
    return MDO_API_BODY_OK;
}

static bool MdoApiBodyReserve(char** pBuffer, size_t* pCapacity,
    size_t Required, size_t Limit)
{
    size_t Capacity = *pCapacity;
    char* Buffer;

    if ( Required <= Capacity ) return true;
    if ( Capacity == 0u ) Capacity = 4096u;
    while ( Capacity < Required ) {
        size_t Next = Capacity < Limit / 2u ?
            Capacity * 2u : Limit + 1u;
        if ( Next <= Capacity || Next > Limit + 1u )
            Next = Limit + 1u;
        Capacity = Next;
    }
    Buffer = (char*)xrtRealloc(*pBuffer, Capacity);
    if ( Buffer == NULL ) return false;
    *pBuffer = Buffer;
    *pCapacity = Capacity;
    return true;
}

static MdoApiBodyStatus MdoApiBodyDecode(MdoApiContext* Context,
    size_t Limit, char** pDocument, size_t* pSize)
{
    const xhttp1head* Head = Context->Request->head;
    const xnetbuf* Network;
    unsigned char Chunk[4096];
    char* Output = NULL;
    size_t Capacity = 0u;
    size_t OutputSize = 0u;
    size_t Offset = 0u;
    size_t Available;

    if ( (Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u &&
         Head->ContentLength > Limit )
        return MDO_API_BODY_TOO_LARGE;
    if ( Context->Request->body == NULL ||
         ((Head->Flags & ((uint32)XHTTP1_CONTENT_LENGTH |
            (uint32)XHTTP1_TRANSFER_ENCODING)) == 0u) ||
         (((Head->Flags & (uint32)XHTTP1_CONTENT_LENGTH) != 0u) &&
            Head->ContentLength == 0u) ) return MDO_API_BODY_MISSING;
    Network = Context->Request->tls != NULL ?
        xrtTlsStreamBuffer(Context->Request->tls) :
        xrtNetStreamBuffer(Context->Request->tcp);
    Available = Network != NULL ? xrtNetBufSize(Network) : 0u;
    if ( Available == 0u ) return MDO_API_BODY_MISSING;

    for ( ; ; ) {
        size_t Read = Available - Offset;
        size_t Consumed = 0u;
        xbytesview Input;
        xbytesview Data;
        xhttp1errorinfo Error;
        xhttp1bodystatus Status;

        if ( Read > sizeof(Chunk) ) Read = sizeof(Chunk);
        if ( Read != 0u &&
             xrtNetBufPeek(Network, Offset, Chunk, Read) != Read ) {
            xrtFree(Output);
            return MDO_API_BODY_READ_FAILED;
        }
        Input.Data = Read != 0u ? Chunk : NULL;
        Input.Size = Read;
        memset(&Data, 0, sizeof(Data));
        memset(&Error, 0, sizeof(Error));
        Status = xrtHttp1BodyRead(Context->Request->body, Input, false,
            &Consumed, &Data, &Error);
        if ( Consumed > Read ) {
            xrtFree(Output);
            return MDO_API_BODY_READ_FAILED;
        }
        Offset += Consumed;
        if ( Data.Size > Limit - OutputSize ) {
            xrtFree(Output);
            return MDO_API_BODY_TOO_LARGE;
        }
        if ( Data.Size != 0u ) {
            if ( !MdoApiBodyReserve(&Output, &Capacity,
                    OutputSize + Data.Size + 1u, Limit) ) {
                xrtFree(Output);
                return MDO_API_BODY_READ_FAILED;
            }
            memcpy(Output + OutputSize, Data.Data, Data.Size);
            OutputSize += Data.Size;
        }
        if ( Status == XHTTP1_BODY_DONE ) break;
        if ( Status == XHTTP1_BODY_ERROR || Status == XHTTP1_BODY_FIELDS ||
             (Consumed == 0u && Data.Size == 0u) || Offset > Available ) {
            xrtFree(Output);
            return MDO_API_BODY_READ_FAILED;
        }
    }
    if ( OutputSize == 0u ) {
        xrtFree(Output);
        return MDO_API_BODY_MISSING;
    }
    if ( !MdoApiBodyReserve(&Output, &Capacity, OutputSize + 1u,
            Limit) ) {
        xrtFree(Output);
        return MDO_API_BODY_READ_FAILED;
    }
    Output[OutputSize] = '\0';
    *pDocument = Output;
    *pSize = OutputSize;
    return MDO_API_BODY_OK;
}

MdoApiBodyStatus MdoApiJsonBodyRead(MdoApiContext* Context,
    MdoApiJsonBody* Body)
{
    MdoApiBodyStatus Status;

    if ( Body == NULL || Context == NULL || Context->Request == NULL ||
         Context->Request->head == NULL ) return MDO_API_BODY_READ_FAILED;
    memset(Body, 0, sizeof(*Body));
    Status = MdoApiBodyType(Context);
    if ( Status != MDO_API_BODY_OK ) return Status;
    Status = MdoApiBodyDecode(Context, MDO_API_REQUEST_MAX_BYTES,
        &Body->Document, &Body->Size);
    if ( Status != MDO_API_BODY_OK ) return Status;
    Body->Value = xrtJsonParse(xrtStrViewN(Body->Document, Body->Size));
    if ( Body->Value == NULL ) {
        MdoApiJsonBodyUnit(Body);
        return MDO_API_BODY_INVALID;
    }
    return MDO_API_BODY_OK;
}

MdoApiBodyStatus MdoApiBinaryBodyRead(MdoApiContext* Context,
    size_t Limit, char** Data, size_t* Size)
{
    if ( Context == NULL || Context->Request == NULL ||
         Context->Request->head == NULL || Data == NULL || Size == NULL ||
         Limit == 0u || Limit > MDO_API_IMAGE_MAX_BYTES )
        return MDO_API_BODY_READ_FAILED;
    *Data = NULL;
    *Size = 0u;
    return MdoApiBodyDecode(Context, Limit, Data, Size);
}

void MdoApiJsonBodyUnit(MdoApiJsonBody* Body)
{
    if ( Body == NULL ) return;
    xrtValueRelease(Body->Value);
    /* Login and model setup bodies can contain credentials, including invalid
     * JSON. Clear their original bytes before releasing the shared buffer. */
    if (Body->Document) xrtSecureZero(Body->Document, Body->Size);
    xrtFree(Body->Document);
    memset(Body, 0, sizeof(*Body));
}

bool MdoApiReplyBodyError(MdoApiContext* Context, MdoApiBodyStatus Status)
{
    switch ( Status ) {
    case MDO_API_BODY_MISSING:
        return MdoApiReplyError(Context, 400u, "body_required",
            "A JSON request body is required", NULL);
    case MDO_API_BODY_UNSUPPORTED_TYPE:
        return MdoApiReplyError(Context, 415u, "unsupported_media_type",
            "Content-Type must be application/json with optional UTF-8 charset",
            NULL);
    case MDO_API_BODY_TOO_LARGE:
        return MdoApiReplyError(Context, 413u, "body_too_large",
            "The request body exceeds the 256 KiB limit", NULL);
    case MDO_API_BODY_INVALID:
        return MdoApiReplyError(Context, 400u, "invalid_json",
            "The request body is not valid strict JSON", NULL);
    default:
        return MdoApiReplyError(Context, 400u, "body_read_failed",
            "The request body could not be decoded", NULL);
    }
}
