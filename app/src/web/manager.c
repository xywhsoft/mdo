#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/config.h"
#include "../../include/mdo/secrets.h"
#include "../../include/mdo/account.h"
#include "../../include/mdo/web.h"
#include "../../include/mdo/http_url.h"

#define MDO_WEB_SOURCE "mdo.web"
#define MDO_WEB_ARGUMENT_LIMIT (64u * 1024u)
#define MDO_WEB_QUERY_LIMIT 1000u
#define MDO_WEB_URL_LIMIT (16u * 1024u)
#define MDO_WEB_TITLE_LIMIT 1024u
#define MDO_WEB_SNIPPET_LIMIT 8192u
#define MDO_WEB_SECRET_LIMIT 8192u
#define MDO_WEB_TOOL_RESULT_LIMIT (1024u * 1024u)

typedef struct MdoWebDocument {
    char Id[32];
    char* Url;
    char* Title;
    char* ContentType;
    char* Content;
    size_t ContentBytes;
    int64 FetchedAt;
    bool Truncated;
} MdoWebDocument;

typedef struct MdoWebState {
    xatomic32 Refs;
    uint64 Generation;
    xwork_runtime* Runtime;
    xmutex* Lock;
    MdoConfigWebSettings Settings;
    MdoWebTransport Transport;
    MdoWebDocument* Documents;
    size_t DocumentCount;
    uint64 NextDocumentId;
    uint64 RequestsCompleted;
    uint64 RequestsFailed;
    bool ToolsEnabled;
} MdoWebState;

typedef struct MdoWebBuffer {
    char* Data;
    size_t Size;
    size_t Capacity;
    bool PendingSpace;
    bool Truncated;
} MdoWebBuffer;

typedef struct MdoWebManager {
    xmutex* Lock;
    xmutex* ReloadLock;
    MdoWebState* Current;
    bool Initialized;
} MdoWebManager;

static MdoWebManager g_MdoWeb;

static void MdoWebError(xwork_error* pError, xwork_error_code Code,
    const char* Message)
{
    if ( pError == NULL ) return;
    xworkErrorInit(pError);
    pError->eCode = Code;
    snprintf(pError->sMessage, sizeof(pError->sMessage), "%s",
        Message != NULL && Message[0] != '\0' ? Message :
        "Web tool failed");
}

static xwork_result MdoWebFail(xwork_error* pError,
    xwork_error_code Code, const char* Message)
{
    MdoWebError(pError, Code, Message);
    if ( Code == XWORK_ERROR_CANCELLED ) return XWORK_RESULT_CANCELLED;
    if ( Code == XWORK_ERROR_TIMEOUT ) return XWORK_RESULT_TIMEOUT;
    if ( Code == XWORK_ERROR_LIMIT ) return XWORK_RESULT_LIMIT;
    return XWORK_RESULT_ERROR;
}

static xwork_result MdoWebToolFail(xwork_tool_result_writer* pWriter,
    xwork_error* pError, const char* Message)
{
    const char* Text = Message != NULL && Message[0] != '\0' ? Message :
        "Web tool failed";
    if ( xworkToolResultWriterWriteText(pWriter, Text) &&
         xworkToolResultWriterSetSuccess(pWriter, false) )
        return XWORK_RESULT_OK;
    return MdoWebFail(pError, XWORK_ERROR_LIMIT,
        "cannot write the Web tool failure result");
}

static xwork_result MdoWebXrtFailure(xwork_tool_result_writer* pWriter,
    xwork_error* pError, const xwork_tool_context* pContext,
    const char* Fallback)
{
    const xerror* pCause = xrtGetError();
    xerrkind Kind = pCause != NULL ? xrtErrorKind(pCause) : XERR_INTERNAL;
    const char* Message = pCause != NULL ? xrtErrorMessage(pCause) : Fallback;
    char Details[768];
    size_t Used = (size_t)snprintf(Details, sizeof(Details), "%s", Message);
    const xerror* Nested = pCause != NULL ? xrtErrorCause(pCause) : NULL;
    while ( Nested != NULL && Used < sizeof(Details) - 1u ) {
        int Written = snprintf(Details + Used, sizeof(Details) - Used,
            ": %s", xrtErrorMessage(Nested));
        if ( Written < 0 || (size_t)Written >= sizeof(Details) - Used ) break;
        Used += (size_t)Written;
        Nested = xrtErrorCause(Nested);
    }
    Message = Details;
    if ( pContext != NULL && pContext->pCancel != NULL &&
         xrtCancelRequested(pContext->pCancel) )
        return MdoWebFail(pError, XWORK_ERROR_CANCELLED, Message);
    if ( pContext != NULL && pContext->uDeadline != XRT_DEADLINE_NEVER &&
         xrtDeadlineExpired(pContext->uDeadline) )
        return MdoWebFail(pError, XWORK_ERROR_TIMEOUT, Message);
    if ( Kind == XERR_CANCELLED )
        return MdoWebFail(pError, XWORK_ERROR_CANCELLED, Message);
    if ( Kind == XERR_TIMEOUT )
        return MdoWebFail(pError, XWORK_ERROR_TIMEOUT, Message);
    if ( Kind == XERR_MEMORY )
        return MdoWebFail(pError, XWORK_ERROR_OUT_OF_MEMORY, Message);
    if ( Kind == XERR_RANGE )
        return MdoWebFail(pError, XWORK_ERROR_LIMIT, Message);
    return MdoWebToolFail(pWriter, pError, Message);
}

static bool MdoWebDefaultFetch(void* pContext,
    const XS_FetchRequest* pRequest, XS_FetchResponse* pResponse)
{
    (void)pContext;
    return xsFetch(pRequest, pResponse);
}

static void MdoWebDefaultResponseUnit(void* pContext,
    XS_FetchResponse* pResponse)
{
    (void)pContext;
    xsFetchResponseUnit(pResponse);
}

static bool MdoWebStateRef(MdoWebState* pState)
{
    uint32 Refs;
    if ( pState == NULL ) return false;
    Refs = xrtAtomic32Load(&pState->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return false;
        if ( xrtAtomic32CompareExchange(&pState->Refs, &Expected,
                Refs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) ) return true;
        Refs = Expected;
    }
}

static void MdoWebDocumentUnit(MdoWebDocument* pDocument)
{
    if ( pDocument == NULL ) return;
    xrtFree(pDocument->Url);
    xrtFree(pDocument->Title);
    xrtFree(pDocument->ContentType);
    xrtFree(pDocument->Content);
    memset(pDocument, 0, sizeof(*pDocument));
}

static void MdoWebStateRelease(MdoWebState* pState)
{
    uint32 Previous;
    size_t i;
    if ( pState == NULL ) return;
    Previous = xrtAtomic32FetchSub(&pState->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < pState->DocumentCount; ++i )
        MdoWebDocumentUnit(&pState->Documents[i]);
    xrtFree(pState->Documents);
    xworkRuntimeRelease(pState->Runtime);
    if ( pState->Lock != NULL ) (void)xrtMutexDestroy(pState->Lock);
    memset(pState, 0, sizeof(*pState));
    xrtFree(pState);
}

static bool MdoWebOwnerRetain(void* pUserData)
{
    return MdoWebStateRef((MdoWebState*)pUserData);
}

static void MdoWebOwnerRelease(void* pUserData)
{
    MdoWebStateRelease((MdoWebState*)pUserData);
}

static bool MdoWebNoNul(xstrview Text)
{
    return Text.Data != NULL && memchr(Text.Data, 0, Text.Size) == NULL;
}

static bool MdoWebString(const xvalue* pValue, size_t Minimum,
    size_t Maximum, xstrview* pText)
{
    return pText != NULL && pValue != NULL &&
        xrtValueType(pValue) == XVALUE_STRING &&
        xrtValueGetString(pValue, pText) && pText->Size >= Minimum &&
        pText->Size <= Maximum && MdoWebNoNul(*pText);
}

static bool MdoWebUnsigned(const xvalue* pValue, uint64* pNumber)
{
    int64 Signed;
    if ( pValue == NULL || pNumber == NULL ) return false;
    if ( xrtValueType(pValue) == XVALUE_UINT )
        return xrtValueGetUInt(pValue, pNumber);
    if ( xrtValueType(pValue) != XVALUE_INT ||
         !xrtValueGetInt(pValue, &Signed) || Signed < 0 ) return false;
    *pNumber = (uint64)Signed;
    return true;
}

/* Page links may contain browser-only fragments (for example share links).
 * Keep them in search results, but permissions and HTTP requests use the same
 * fragment-free URL. Validate the whole input before stripping anything;
 * a fragment must not hide controls, credentials in the authority or a bad
 * scheme. Service/configuration URLs retain MdoHttpUrlValid's strict policy. */
static bool MdoWebPageUrl(xstrview Url, bool AllowHttp, xstrview* pRequestUrl)
{
    xstrview RequestUrl = Url;
    size_t i;
    if ( Url.Data == NULL || Url.Size == 0u || Url.Size > MDO_WEB_URL_LIMIT )
        return false;
    for ( i = 0u; i < Url.Size; ++i ) {
        unsigned char c = (unsigned char)Url.Data[i];
        if ( c <= 0x20u || c == 0x7fu || c == '\\' ) return false;
        if ( c == '#' && RequestUrl.Size == Url.Size ) RequestUrl.Size = i;
    }
    if ( !MdoHttpUrlValid(RequestUrl, AllowHttp) ) return false;
    if ( pRequestUrl != NULL ) *pRequestUrl = RequestUrl;
    return true;
}

static bool MdoWebArguments(const char* Json, xvalue** ppRoot)
{
    xjsonreadconfig Config;
    size_t Length;
    if ( Json == NULL || ppRoot == NULL ) return false;
    *ppRoot = NULL;
    Length = strlen(Json);
    if ( Length > MDO_WEB_ARGUMENT_LIMIT ) return false;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_WEB_ARGUMENT_LIMIT;
    Config.MaxDepth = 8u;
    Config.MaxValues = 64u;
    Config.MaxContainerItems = 32u;
    *ppRoot = xrtJsonRead(xrtStrViewN(Json, Length), &Config);
    if ( *ppRoot == NULL || xrtValueType(*ppRoot) != XVALUE_OBJECT ) {
        xrtValueRelease(*ppRoot);
        *ppRoot = NULL;
        return false;
    }
    return true;
}

static bool MdoWebAllowedKeys(const xvalue* pObject,
    const char* const* ppAllowed, size_t AllowedCount)
{
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* pValue;
    xvalueiterresult Result;
    size_t i;
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(pObject, &Iterator) ) return false;
    for ( ; ; ) {
        bool Allowed = false;
        Result = xrtValueIterAdvance(&Iterator, &Key, &pValue);
        if ( Result == XVALUE_ITER_END ) break;
        if ( Result == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
        for ( i = 0u; i < AllowedCount; ++i ) {
            size_t Size = strlen(ppAllowed[i]);
            if ( Key.String.Size == Size &&
                 memcmp(Key.String.Data, ppAllowed[i], Size) == 0 ) {
                Allowed = true;
                break;
            }
        }
        if ( !Allowed ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
    }
    xrtValueIterEnd(&Iterator);
    return true;
}

static bool MdoWebAsciiEqual(const char* Left, const char* Right, size_t Size)
{
    size_t i;
    for ( i = 0u; i < Size; ++i )
        if ( tolower((unsigned char)Left[i]) !=
             tolower((unsigned char)Right[i]) ) return false;
    return true;
}

static bool MdoWebBufferInit(MdoWebBuffer* pBuffer, size_t Maximum)
{
    if ( pBuffer == NULL || Maximum == 0u || Maximum == SIZE_MAX ) return false;
    memset(pBuffer, 0, sizeof(*pBuffer));
    pBuffer->Data = (char*)xrtMalloc(Maximum + 1u);
    if ( pBuffer->Data == NULL ) return false;
    pBuffer->Capacity = Maximum;
    pBuffer->Data[0] = '\0';
    return true;
}

static void MdoWebBufferUnit(MdoWebBuffer* pBuffer)
{
    if ( pBuffer == NULL ) return;
    xrtFree(pBuffer->Data);
    memset(pBuffer, 0, sizeof(*pBuffer));
}

static bool MdoWebBufferBytes(MdoWebBuffer* pBuffer,
    const char* Data, size_t Size)
{
    if ( Size == 0u ) return true;
    if ( pBuffer->PendingSpace && pBuffer->Size != 0u ) {
        if ( pBuffer->Size == pBuffer->Capacity ) {
            pBuffer->Truncated = true;
            return true;
        }
        pBuffer->Data[pBuffer->Size++] = ' ';
        pBuffer->PendingSpace = false;
    }
    if ( Size > pBuffer->Capacity - pBuffer->Size ) {
        pBuffer->Truncated = true;
        return true;
    }
    memcpy(pBuffer->Data + pBuffer->Size, Data, Size);
    pBuffer->Size += Size;
    pBuffer->Data[pBuffer->Size] = '\0';
    return true;
}

static void MdoWebBufferSpace(MdoWebBuffer* pBuffer)
{
    if ( pBuffer != NULL && pBuffer->Size != 0u ) pBuffer->PendingSpace = true;
}

static size_t MdoWebUtf8Size(unsigned char Lead)
{
    if ( Lead < 0x80u ) return 1u;
    if ( (Lead & 0xe0u) == 0xc0u ) return 2u;
    if ( (Lead & 0xf0u) == 0xe0u ) return 3u;
    if ( (Lead & 0xf8u) == 0xf0u ) return 4u;
    return 0u;
}

static size_t MdoWebCodepoint(char Output[4], uint32 Value)
{
    if ( Value <= 0x7fu ) { Output[0] = (char)Value; return 1u; }
    if ( Value <= 0x7ffu ) {
        Output[0] = (char)(0xc0u | (Value >> 6));
        Output[1] = (char)(0x80u | (Value & 0x3fu));
        return 2u;
    }
    if ( Value >= 0xd800u && Value <= 0xdfffu ) return 0u;
    if ( Value <= 0xffffu ) {
        Output[0] = (char)(0xe0u | (Value >> 12));
        Output[1] = (char)(0x80u | ((Value >> 6) & 0x3fu));
        Output[2] = (char)(0x80u | (Value & 0x3fu));
        return 3u;
    }
    if ( Value > 0x10ffffu ) return 0u;
    Output[0] = (char)(0xf0u | (Value >> 18));
    Output[1] = (char)(0x80u | ((Value >> 12) & 0x3fu));
    Output[2] = (char)(0x80u | ((Value >> 6) & 0x3fu));
    Output[3] = (char)(0x80u | (Value & 0x3fu));
    return 4u;
}

static size_t MdoWebEntity(const char* Data, size_t Size, char Output[4])
{
    uint32 Value = 0u;
    size_t i;
    unsigned Base = 10u;
    if ( Size == 3u && memcmp(Data, "amp", 3u) == 0 ) { Output[0] = '&'; return 1u; }
    if ( Size == 2u && memcmp(Data, "lt", 2u) == 0 ) { Output[0] = '<'; return 1u; }
    if ( Size == 2u && memcmp(Data, "gt", 2u) == 0 ) { Output[0] = '>'; return 1u; }
    if ( Size == 4u && memcmp(Data, "quot", 4u) == 0 ) { Output[0] = '"'; return 1u; }
    if ( Size == 4u && memcmp(Data, "apos", 4u) == 0 ) { Output[0] = '\''; return 1u; }
    if ( Size == 4u && memcmp(Data, "nbsp", 4u) == 0 ) { Output[0] = ' '; return 1u; }
    if ( Size < 2u || Data[0] != '#' ) return 0u;
    i = 1u;
    if ( i < Size && (Data[i] == 'x' || Data[i] == 'X') ) { Base = 16u; ++i; }
    if ( i == Size ) return 0u;
    for ( ; i < Size; ++i ) {
        unsigned Digit;
        unsigned char c = (unsigned char)Data[i];
        if ( c >= '0' && c <= '9' ) Digit = (unsigned)(c - '0');
        else if ( Base == 16u && c >= 'a' && c <= 'f' ) Digit = (unsigned)(c - 'a' + 10u);
        else if ( Base == 16u && c >= 'A' && c <= 'F' ) Digit = (unsigned)(c - 'A' + 10u);
        else return 0u;
        if ( Value > (0x10ffffu - Digit) / Base ) return 0u;
        Value = Value * Base + Digit;
    }
    return MdoWebCodepoint(Output, Value);
}

static size_t MdoWebTagEnd(const char* Data, size_t Size, size_t Start)
{
    char Quote = 0;
    size_t i;
    for ( i = Start; i < Size; ++i ) {
        char c = Data[i];
        if ( Quote != 0 ) { if ( c == Quote ) Quote = 0; }
        else if ( c == '\'' || c == '"' ) Quote = c;
        else if ( c == '>' ) return i;
    }
    return SIZE_MAX;
}

static size_t MdoWebFindAscii(const char* Data, size_t Size, size_t Start,
    const char* Needle)
{
    size_t NeedleSize = strlen(Needle);
    size_t i;
    if ( NeedleSize > Size ) return SIZE_MAX;
    for ( i = Start; i + NeedleSize <= Size; ++i )
        if ( MdoWebAsciiEqual(Data + i, Needle, NeedleSize) ) return i;
    return SIZE_MAX;
}

static bool MdoWebTagName(const char* Data, size_t Begin, size_t End,
    bool* pClosing, const char** ppName, size_t* pNameSize)
{
    size_t i = Begin + 1u;
    while ( i < End && isspace((unsigned char)Data[i]) ) ++i;
    *pClosing = i < End && Data[i] == '/';
    if ( *pClosing ) ++i;
    while ( i < End && isspace((unsigned char)Data[i]) ) ++i;
    *ppName = Data + i;
    while ( i < End && (isalnum((unsigned char)Data[i]) ||
           Data[i] == '-' || Data[i] == ':') ) ++i;
    *pNameSize = (size_t)(Data + i - *ppName);
    return *pNameSize != 0u;
}

static bool MdoWebTagIs(const char* Name, size_t NameSize, const char* Expected)
{
    return NameSize == strlen(Expected) &&
        MdoWebAsciiEqual(Name, Expected, NameSize);
}

static bool MdoWebContainsAscii(const char* Text, const char* Needle)
{
    size_t TextSize;
    size_t NeedleSize;
    size_t i;
    if ( Text == NULL || Needle == NULL ) return false;
    TextSize = strlen(Text);
    NeedleSize = strlen(Needle);
    if ( NeedleSize == 0u ) return true;
    for ( i = 0u; i + NeedleSize <= TextSize; ++i )
        if ( MdoWebAsciiEqual(Text + i, Needle, NeedleSize) ) return true;
    return false;
}

static bool MdoWebExtract(const unsigned char* Bytes, size_t Size,
    const char* ContentType, size_t Maximum, char** ppTitle,
    char** ppContent, size_t* pContentBytes, bool* pTruncated)
{
    MdoWebBuffer Content;
    MdoWebBuffer Title;
    xstrview Input = { (const char*)Bytes, Size };
    size_t Utf8Error = 0u;
    bool Html = MdoWebContainsAscii(ContentType, "text/html") ||
        MdoWebContainsAscii(ContentType, "application/xhtml");
    bool InTitle = false;
    size_t i = 0u;
    memset(&Content, 0, sizeof(Content));
    memset(&Title, 0, sizeof(Title));
    if ( ppTitle == NULL || ppContent == NULL || pContentBytes == NULL ||
         pTruncated == NULL || (Bytes == NULL && Size != 0u) ||
         !xrtUtf8Valid(Input, &Utf8Error) ) return false;
    if ( !MdoWebBufferInit(&Content, Maximum) ||
         !MdoWebBufferInit(&Title, MDO_WEB_TITLE_LIMIT) ) goto failed;
    while ( i < Size ) {
        unsigned char c = Bytes[i];
        if ( Html && c == '<' ) {
            size_t End;
            const char* Name;
            size_t NameSize;
            bool Closing;
            if ( i + 4u <= Size && memcmp(Bytes + i, "<!--", 4u) == 0 ) {
                size_t Close = MdoWebFindAscii((const char*)Bytes, Size, i + 4u, "-->");
                i = Close == SIZE_MAX ? Size : Close + 3u;
                MdoWebBufferSpace(&Content);
                continue;
            }
            End = MdoWebTagEnd((const char*)Bytes, Size, i + 1u);
            if ( End == SIZE_MAX ) { MdoWebBufferSpace(&Content); break; }
            if ( MdoWebTagName((const char*)Bytes, i, End,
                    &Closing, &Name, &NameSize) ) {
                if ( MdoWebTagIs(Name, NameSize, "title") ) InTitle = !Closing;
                if ( !Closing && (MdoWebTagIs(Name, NameSize, "script") ||
                     MdoWebTagIs(Name, NameSize, "style") ||
                     MdoWebTagIs(Name, NameSize, "noscript") ||
                     MdoWebTagIs(Name, NameSize, "svg")) ) {
                    char CloseTag[32];
                    size_t Close;
                    if ( NameSize + 3u < sizeof(CloseTag) ) {
                        CloseTag[0] = '<'; CloseTag[1] = '/';
                        memcpy(CloseTag + 2u, Name, NameSize);
                        CloseTag[NameSize + 2u] = '\0';
                        Close = MdoWebFindAscii((const char*)Bytes, Size,
                            End + 1u, CloseTag);
                        if ( Close == SIZE_MAX ) { i = Size; continue; }
                        End = MdoWebTagEnd((const char*)Bytes, Size, Close + 2u);
                        if ( End == SIZE_MAX ) { i = Size; continue; }
                    }
                }
                if ( MdoWebTagIs(Name, NameSize, "p") ||
                     MdoWebTagIs(Name, NameSize, "br") ||
                     MdoWebTagIs(Name, NameSize, "div") ||
                     MdoWebTagIs(Name, NameSize, "li") ||
                     MdoWebTagIs(Name, NameSize, "h1") ||
                     MdoWebTagIs(Name, NameSize, "h2") ||
                     MdoWebTagIs(Name, NameSize, "h3") ||
                     MdoWebTagIs(Name, NameSize, "tr") ) MdoWebBufferSpace(&Content);
            }
            i = End + 1u;
            continue;
        }
        if ( c == '&' ) {
            size_t End = i + 1u;
            char Encoded[4];
            size_t EncodedSize = 0u;
            while ( End < Size && End - i <= 16u && Bytes[End] != ';' &&
                    Bytes[End] != '<' && !isspace(Bytes[End]) ) ++End;
            if ( End < Size && Bytes[End] == ';' )
                EncodedSize = MdoWebEntity((const char*)Bytes + i + 1u,
                    End - i - 1u, Encoded);
            if ( EncodedSize != 0u ) {
                if ( EncodedSize == 1u && isspace((unsigned char)Encoded[0]) ) {
                    MdoWebBufferSpace(&Content);
                    if ( InTitle ) MdoWebBufferSpace(&Title);
                } else {
                    (void)MdoWebBufferBytes(&Content, Encoded, EncodedSize);
                    if ( InTitle ) (void)MdoWebBufferBytes(&Title, Encoded, EncodedSize);
                }
                i = End + 1u;
                continue;
            }
        }
        if ( c < 0x80u && isspace(c) ) {
            MdoWebBufferSpace(&Content);
            if ( InTitle ) MdoWebBufferSpace(&Title);
            ++i;
        } else {
            size_t Unit = MdoWebUtf8Size(c);
            if ( Unit == 0u || Unit > Size - i ) goto failed;
            (void)MdoWebBufferBytes(&Content, (const char*)Bytes + i, Unit);
            if ( InTitle ) (void)MdoWebBufferBytes(&Title,
                (const char*)Bytes + i, Unit);
            i += Unit;
        }
    }
    *ppTitle = Title.Data;
    *ppContent = Content.Data;
    *pContentBytes = Content.Size;
    *pTruncated = Content.Truncated;
    Title.Data = NULL;
    Content.Data = NULL;
    MdoWebBufferUnit(&Title);
    MdoWebBufferUnit(&Content);
    return true;
failed:
    MdoWebBufferUnit(&Title);
    MdoWebBufferUnit(&Content);
    return false;
}

static bool MdoWebTextContentType(const char* ContentType)
{
    size_t i;
    static const char* const Types[] = {
        "text/", "json", "xml", "javascript", "markdown"
    };
    if ( ContentType == NULL || ContentType[0] == '\0' ) return true;
    for ( i = 0u; i < sizeof(Types) / sizeof(Types[0]); ++i )
        if ( MdoWebContainsAscii(ContentType, Types[i]) ) return true;
    return false;
}

static bool MdoWebObjectTake(xvalue* pObject, const char* Key, xvalue* pValue)
{
    bool Ok = pObject != NULL && pValue != NULL &&
        xrtValueObjectSetTake(pObject, xrtStrView(Key), &pValue);
    xrtValueRelease(pValue);
    return Ok;
}

static bool MdoWebObjectString(xvalue* pObject, const char* Key,
    const char* Text, size_t Size)
{
    xvalue* pValue = xrtValueString(xrtStrViewN(Text != NULL ? Text : "",
        Text != NULL ? Size : 0u));
    return MdoWebObjectTake(pObject, Key, pValue);
}

static bool MdoWebObjectBool(xvalue* pObject, const char* Key, bool Value)
{
    return MdoWebObjectTake(pObject, Key, xrtValueBool(Value));
}

static bool MdoWebObjectUInt(xvalue* pObject, const char* Key, uint64 Value)
{
    return MdoWebObjectTake(pObject, Key, xrtValueUInt(Value));
}

static bool MdoWebObjectInt(xvalue* pObject, const char* Key, int64 Value)
{
    return MdoWebObjectTake(pObject, Key, xrtValueInt(Value));
}

static bool MdoWebWriteValue(xwork_tool_result_writer* pWriter,
    xvalue* pValue, xwork_error* pError)
{
    char* Json;
    size_t Size = 0u;
    bool Ok;
    Json = xrtJsonStringify(pValue, false, &Size);
    if ( Json == NULL ) {
        MdoWebError(pError, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot encode Web tool result");
        return false;
    }
    Ok = xworkToolResultWriterWrite(pWriter, Json, Size) &&
        xworkToolResultWriterSetSuccess(pWriter, true);
    xrtFree(Json);
    if ( !Ok ) MdoWebError(pError, XWORK_ERROR_LIMIT,
        "Web tool result exceeds its output limit");
    return Ok;
}

static bool MdoWebFetchRequest(MdoWebState* pState,
    const xwork_tool_context* pContext, const char* Url,
    const XS_FetchHeader* pHeaders, size_t HeaderCount,
    bool SearchService, const char* Body, size_t BodySize,
    XS_FetchResponse* pResponse)
{
    XS_FetchRequest Request;
    uint64 Timeout = (uint64)pState->Settings.TimeoutMilliseconds * 1000u;
    uint64 IdleTimeout =
        (uint64)pState->Settings.IdleTimeoutMilliseconds * 1000u;
    if ( pContext->uDeadline != XRT_DEADLINE_NEVER ) {
        uint64 Remaining = xrtDeadlineRemaining(pContext->uDeadline);
        if ( Remaining == 0u ) return false;
        if ( Timeout > Remaining ) Timeout = Remaining;
        if ( IdleTimeout > Remaining ) IdleTimeout = Remaining;
    }
    memset(&Request, 0, sizeof(Request));
    Request.Size = sizeof(Request);
    Request.Version = XS_FETCH_REQUEST_VERSION;
    Request.Url = Url;
    Request.Method = SearchService ? "POST" : "GET";
    Request.Body = Body;
    Request.BodySize = BodySize;
    Request.Headers = pHeaders;
    Request.HeaderCount = HeaderCount;
    Request.Timeout = Timeout;
    Request.IdleTimeout = IdleTimeout;
    Request.MaxBodyBytes = pState->Settings.MaxResponseBytes;
    /* The explicit API address may be local/LAN. Never redirect a request
     * carrying account credentials. Ordinary pages retain public-only DNS. */
    Request.MaxRedirects = SearchService ? 0u : 5u;
    Request.Flags = XS_FETCH_DECOMPRESS;
    if ( !SearchService ) Request.Flags |= XS_FETCH_FOLLOW_REDIRECTS;
    if ( !pState->Settings.AllowPrivateNetworks && !SearchService )
        Request.Flags |= XS_FETCH_PUBLIC_ADDRESSES_ONLY;
    Request.Cancel = pContext->pCancel;
    return pState->Transport.Fetch(pState->Transport.Context,
        &Request, pResponse);
}

static void MdoWebRequestFinished(MdoWebState* pState, bool Success)
{
    if ( pState->Lock == NULL || !xrtMutexLock(pState->Lock) ) return;
    if ( Success ) ++pState->RequestsCompleted;
    else ++pState->RequestsFailed;
    (void)xrtMutexUnlock(pState->Lock);
}

#include "search_api.inc.c"

static bool MdoWebDocumentResult(xvalue* pOutput,
    const MdoWebDocument* pDocument, cstr Type, bool IncludeContent)
{
    return MdoWebObjectString(pOutput, "type", Type, strlen(Type)) &&
        MdoWebObjectString(pOutput, "document_id", pDocument->Id,
            strlen(pDocument->Id)) &&
        MdoWebObjectString(pOutput, "url", pDocument->Url,
            strlen(pDocument->Url)) &&
        MdoWebObjectString(pOutput, "title", pDocument->Title,
            strlen(pDocument->Title)) &&
        MdoWebObjectString(pOutput, "content_type", pDocument->ContentType,
            strlen(pDocument->ContentType)) &&
        MdoWebObjectString(pOutput, "source", "web_open", 8u) &&
        MdoWebObjectInt(pOutput, "fetched_at", pDocument->FetchedAt) &&
        MdoWebObjectBool(pOutput, "truncated", pDocument->Truncated) &&
        MdoWebObjectBool(pOutput, "untrusted", true) &&
        (!IncludeContent || MdoWebObjectString(pOutput, "content",
            pDocument->Content, pDocument->ContentBytes));
}

static bool MdoWebCacheInsert(MdoWebState* pState, MdoWebDocument* pDocument)
{
    size_t Index;
    if ( !xrtMutexLock(pState->Lock) ) return false;
    if ( pState->DocumentCount == pState->Settings.MaxDocuments ) {
        MdoWebDocumentUnit(&pState->Documents[0]);
        if ( pState->DocumentCount > 1u )
            memmove(&pState->Documents[0], &pState->Documents[1],
                (pState->DocumentCount - 1u) * sizeof(*pState->Documents));
        --pState->DocumentCount;
    }
    Index = pState->DocumentCount++;
    pState->Documents[Index] = *pDocument;
    memset(pDocument, 0, sizeof(*pDocument));
    (void)xrtMutexUnlock(pState->Lock);
    return true;
}

static xwork_result MdoWebOpenExecute(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_tool_result_writer* pWriter, xwork_error* pError)
{
    static const char* const Keys[] = { "url", "max_characters" };
    MdoWebState* pState = (MdoWebState*)pUserData;
    xvalue* pArguments = NULL;
    xvalue* pOutput = NULL;
    xstrview UrlView;
    uint64 Maximum = pState->Settings.MaxTextBytes;
    char* Url = NULL;
    XS_FetchHeader Headers[2];
    XS_FetchResponse Response;
    MdoWebDocument Document;
    bool FetchOk = false;
    xwork_result Result = XWORK_RESULT_ERROR;
    memset(&Response, 0, sizeof(Response));
    memset(&Document, 0, sizeof(Document));
    if ( !MdoWebArguments(ArgumentsJson, &pArguments) ||
         !MdoWebAllowedKeys(pArguments, Keys, 2u) ||
         !MdoWebString(xrtValueObjectGet(pArguments, xrtStrView("url")),
            1u, MDO_WEB_URL_LIMIT, &UrlView) ||
         !MdoWebPageUrl(UrlView, pState->Settings.AllowHttp, &UrlView) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_open requires an allowed HTTP(S) URL");
        goto done;
    }
    if ( xrtValueObjectGet(pArguments, xrtStrView("max_characters")) != NULL &&
         (!MdoWebUnsigned(xrtValueObjectGet(pArguments,
            xrtStrView("max_characters")), &Maximum) || Maximum < 256u ||
          Maximum > pState->Settings.MaxTextBytes) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_open max_characters exceeds the configured limit");
        goto done;
    }
    Url = xrtStrDupN(UrlView.Data, UrlView.Size);
    if ( Url == NULL ) goto memory_failed;
    Headers[0].Name = "Accept";
    Headers[0].Value = "text/html,application/xhtml+xml,application/json,text/plain;q=0.9,*/*;q=0.1";
    Headers[1].Name = "User-Agent"; Headers[1].Value = "mdo/1 web_open";
    FetchOk = MdoWebFetchRequest(pState, pContext, Url, Headers, 2u, false, NULL, 0u, &Response);
    MdoWebRequestFinished(pState, FetchOk);
    if ( !FetchOk ) { Result = MdoWebXrtFailure(pWriter, pError, pContext,
        "web_open request failed"); goto done; }
    if ( Response.Status < 200u || Response.Status >= 300u ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_open returned a non-success status");
        goto done;
    }
    if ( !MdoWebTextContentType(Response.ContentType) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_open received a non-text response");
        goto done;
    }
    Document.Url = xrtStrDup(Response.FinalUrl != NULL ? Response.FinalUrl : Url);
    Document.ContentType = xrtStrDup(Response.ContentType != NULL ?
        Response.ContentType : "application/octet-stream");
    Document.FetchedAt = Response.FetchedAt;
    if ( Document.Url == NULL || Document.ContentType == NULL ||
         !MdoWebExtract(Response.Body, Response.BodySize, Response.ContentType,
            (size_t)Maximum, &Document.Title, &Document.Content,
            &Document.ContentBytes, &Document.Truncated) ) {
        if ( xrtGetError() == NULL )
            Result = MdoWebToolFail(pWriter, pError,
                "web_open response is not valid UTF-8 text");
        else goto memory_failed;
        goto done;
    }
    if ( !MdoWebPageUrl(xrtStrView(Document.Url),
            pState->Settings.AllowHttp, NULL) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_open final URL violates the URL policy");
        goto done;
    }
    if ( !xrtMutexLock(pState->Lock) ) goto memory_failed;
    ++pState->NextDocumentId;
    snprintf(Document.Id, sizeof(Document.Id), "doc-%016llx",
        (unsigned long long)pState->NextDocumentId);
    (void)xrtMutexUnlock(pState->Lock);
    pOutput = xrtValueObject();
    if ( pOutput == NULL || !MdoWebDocumentResult(pOutput, &Document,
            "web_document", true) )
        goto memory_failed;
    if ( !MdoWebCacheInsert(pState, &Document) ) goto memory_failed;
    if ( !MdoWebWriteValue(pWriter, pOutput, pError) ) {
        Result = XWORK_RESULT_LIMIT; goto done;
    }
    Result = XWORK_RESULT_OK;
    goto done;
memory_failed:
    Result = MdoWebFail(pError, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot retain web_open result");
done:
    MdoWebDocumentUnit(&Document);
    pState->Transport.ResponseUnit(pState->Transport.Context, &Response);
    xrtFree(Url);
    xrtValueRelease(pOutput);
    xrtValueRelease(pArguments);
    return Result;
}

static bool MdoWebDocumentCopy(MdoWebState* pState, const char* Id,
    MdoWebDocument* pCopy)
{
    size_t i;
    bool Ok = false;
    memset(pCopy, 0, sizeof(*pCopy));
    if ( !xrtMutexLock(pState->Lock) ) return false;
    for ( i = 0u; i < pState->DocumentCount; ++i ) {
        MdoWebDocument* pDocument = &pState->Documents[i];
        if ( strcmp(pDocument->Id, Id) != 0 ) continue;
        memcpy(pCopy->Id, pDocument->Id, sizeof(pCopy->Id));
        pCopy->Url = xrtStrDup(pDocument->Url);
        pCopy->Title = xrtStrDup(pDocument->Title);
        pCopy->ContentType = xrtStrDup(pDocument->ContentType);
        pCopy->Content = xrtStrDupN(pDocument->Content,
            pDocument->ContentBytes);
        pCopy->ContentBytes = pDocument->ContentBytes;
        pCopy->FetchedAt = pDocument->FetchedAt;
        pCopy->Truncated = pDocument->Truncated;
        Ok = pCopy->Url != NULL && pCopy->Title != NULL &&
            pCopy->ContentType != NULL && pCopy->Content != NULL;
        break;
    }
    (void)xrtMutexUnlock(pState->Lock);
    if ( !Ok ) MdoWebDocumentUnit(pCopy);
    return Ok;
}

static bool MdoWebMatch(const char* Text, const char* Query,
    size_t QuerySize, bool CaseSensitive)
{
    size_t i;
    if ( CaseSensitive ) return memcmp(Text, Query, QuerySize) == 0;
    for ( i = 0u; i < QuerySize; ++i ) {
        unsigned char Left = (unsigned char)Text[i];
        unsigned char Right = (unsigned char)Query[i];
        if ( Left < 0x80u ) Left = (unsigned char)tolower(Left);
        if ( Right < 0x80u ) Right = (unsigned char)tolower(Right);
        if ( Left != Right ) return false;
    }
    return true;
}

static size_t MdoWebUtf8BoundaryLeft(const char* Text, size_t Offset)
{
    while ( Offset != 0u &&
            (((unsigned char)Text[Offset] & 0xc0u) == 0x80u) ) --Offset;
    return Offset;
}

static size_t MdoWebUtf8BoundaryRight(const char* Text,
    size_t Size, size_t Offset)
{
    if ( Offset > Size ) Offset = Size;
    while ( Offset < Size &&
            (((unsigned char)Text[Offset] & 0xc0u) == 0x80u) ) ++Offset;
    return Offset;
}

static xwork_result MdoWebFindExecute(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_tool_result_writer* pWriter, xwork_error* pError)
{
    static const char* const Keys[] = {
        "document_id", "query", "max_results", "context_characters",
        "case_sensitive"
    };
    MdoWebState* pState = (MdoWebState*)pUserData;
    xvalue* pArguments = NULL;
    xvalue* pOutput = NULL;
    xvalue* pMatches = NULL;
    xstrview Id;
    xstrview Query;
    uint64 MaxResults = 10u;
    uint64 Context = 160u;
    bool CaseSensitive = false;
    const xvalue* pCase;
    char* IdText = NULL;
    MdoWebDocument Document;
    size_t i;
    xwork_result Result = XWORK_RESULT_ERROR;
    (void)pContext;
    memset(&Document, 0, sizeof(Document));
    if ( !MdoWebArguments(ArgumentsJson, &pArguments) ||
         !MdoWebAllowedKeys(pArguments, Keys, 5u) ||
         !MdoWebString(xrtValueObjectGet(pArguments,
            xrtStrView("document_id")), 1u, 31u, &Id) ||
         !MdoWebString(xrtValueObjectGet(pArguments,
            xrtStrView("query")), 1u, MDO_WEB_QUERY_LIMIT, &Query) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_find requires document_id and a bounded query");
        goto done;
    }
    if ( xrtValueObjectGet(pArguments, xrtStrView("max_results")) != NULL &&
         (!MdoWebUnsigned(xrtValueObjectGet(pArguments,
            xrtStrView("max_results")), &MaxResults) ||
          MaxResults == 0u || MaxResults > 20u) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_find max_results must be between 1 and 20");
        goto done;
    }
    if ( xrtValueObjectGet(pArguments,
            xrtStrView("context_characters")) != NULL &&
         (!MdoWebUnsigned(xrtValueObjectGet(pArguments,
            xrtStrView("context_characters")), &Context) ||
          Context < 32u || Context > 1000u) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_find context_characters must be between 32 and 1000");
        goto done;
    }
    pCase = xrtValueObjectGet(pArguments, xrtStrView("case_sensitive"));
    if ( pCase != NULL && (xrtValueType(pCase) != XVALUE_BOOL ||
         !xrtValueGetBool(pCase, &CaseSensitive)) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_find case_sensitive must be a boolean");
        goto done;
    }
    IdText = xrtStrDupN(Id.Data, Id.Size);
    if ( IdText == NULL ) goto memory_failed;
    if ( !MdoWebDocumentCopy(pState, IdText, &Document) ) {
        Result = MdoWebToolFail(pWriter, pError,
            "web_find document is missing or has been evicted");
        goto done;
    }
    pOutput = xrtValueObject();
    pMatches = xrtValueArray();
    if ( pOutput == NULL || pMatches == NULL ||
         !MdoWebDocumentResult(pOutput, &Document,
            "web_find_results", false) ) goto memory_failed;
    for ( i = 0u; i + Query.Size <= Document.ContentBytes &&
         xrtValueCount(pMatches) < (size_t)MaxResults; ++i ) {
        size_t Begin;
        size_t End;
        xvalue* pMatch;
        if ( !MdoWebMatch(Document.Content + i, Query.Data,
                Query.Size, CaseSensitive) ) continue;
        Begin = i > (size_t)Context ? i - (size_t)Context : 0u;
        End = i + Query.Size + (size_t)Context;
        if ( End > Document.ContentBytes ) End = Document.ContentBytes;
        Begin = MdoWebUtf8BoundaryLeft(Document.Content, Begin);
        End = MdoWebUtf8BoundaryRight(Document.Content,
            Document.ContentBytes, End);
        pMatch = xrtValueObject();
        if ( pMatch == NULL ||
             !MdoWebObjectUInt(pMatch, "offset", (uint64)i) ||
             !MdoWebObjectString(pMatch, "text", Document.Content + Begin,
                End - Begin) ||
             !xrtValueArrayAppendTake(pMatches, &pMatch) ) {
            xrtValueRelease(pMatch);
            goto memory_failed;
        }
        if ( Query.Size > 1u ) i += Query.Size - 1u;
    }
    if ( !MdoWebObjectString(pOutput, "query", Query.Data, Query.Size) ||
         !MdoWebObjectTake(pOutput, "matches", pMatches) ) goto memory_failed;
    pMatches = NULL;
    if ( !MdoWebWriteValue(pWriter, pOutput, pError) ) {
        Result = XWORK_RESULT_LIMIT; goto done;
    }
    Result = XWORK_RESULT_OK;
    goto done;
memory_failed:
    Result = MdoWebFail(pError, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot build web_find result");
done:
    xrtFree(IdText);
    MdoWebDocumentUnit(&Document);
    xrtValueRelease(pMatches);
    xrtValueRelease(pOutput);
    xrtValueRelease(pArguments);
    return Result;
}

static xwork_result MdoWebSearchPermissions(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_permission_resource_writer* pWriter, xwork_error* pError)
{
    static const char* const Keys[] = { "query", "count" };
    (void)pUserData;
    xvalue* pArguments = NULL;
    xstrview Query;
    uint64 Count;
    const xvalue* pCount;
    xwork_result Result;
    (void)pContext;
    if ( !MdoWebArguments(ArgumentsJson, &pArguments) ||
         !MdoWebAllowedKeys(pArguments, Keys, 2u) ||
         !MdoWebString(xrtValueObjectGet(pArguments, xrtStrView("query")),
            1u, MDO_WEB_QUERY_LIMIT, &Query) || !MdoWebSearchQueryValid(Query) ) {
        Result = MdoWebFail(pError, XWORK_ERROR_INVALID_ARGUMENT,
            "web_search requires a bounded query and optional count");
        goto done;
    }
    pCount = xrtValueObjectGet(pArguments, xrtStrView("count"));
    if ( pCount != NULL && (!MdoWebUnsigned(pCount, &Count) || Count == 0u ||
         Count > MDO_WEB_SEARCH_MAX_RESULTS) ) {
        Result = MdoWebFail(pError, XWORK_ERROR_INVALID_ARGUMENT,
            "web_search count exceeds the configured result limit");
        goto done;
    }
    if ( !xworkPermissionResourceWriterAdd(pWriter, XWORK_RESOURCE_NETWORK,
            XWORK_RESOURCE_ACCESS_CONNECT, MdoAccountSearchEndpoint()) ||
         !xworkPermissionResourceWriterAdd(pWriter, XWORK_RESOURCE_EXTERNAL_SERVICE,
            XWORK_RESOURCE_ACCESS_USE, "xadmin.search") ||
         !xworkPermissionResourceWriterAdd(pWriter, XWORK_RESOURCE_SECRET,
            XWORK_RESOURCE_ACCESS_USE, MDO_WEB_SEARCH_TOKEN_REF) )
        Result = MdoWebFail(pError, XWORK_ERROR_LIMIT,
            "web_search permission resources exceed their limit");
    else Result = XWORK_RESULT_OK;
done:
    xrtValueRelease(pArguments);
    return Result;
}

static xwork_result MdoWebOpenPermissions(void* pUserData,
    const xwork_tool_context* pContext, const char* ArgumentsJson,
    xwork_permission_resource_writer* pWriter, xwork_error* pError)
{
    static const char* const Keys[] = { "url", "max_characters" };
    MdoWebState* pState = (MdoWebState*)pUserData;
    xvalue* pArguments = NULL;
    xstrview Url;
    uint64 Maximum;
    const xvalue* pMaximum;
    char* Resource = NULL;
    xwork_result Result;
    (void)pContext;
    if ( !MdoWebArguments(ArgumentsJson, &pArguments) ||
         !MdoWebAllowedKeys(pArguments, Keys, 2u) ||
         !MdoWebString(xrtValueObjectGet(pArguments, xrtStrView("url")),
            1u, MDO_WEB_URL_LIMIT, &Url) ||
         !MdoWebPageUrl(Url, pState->Settings.AllowHttp, &Url) ) {
        Result = MdoWebFail(pError, XWORK_ERROR_INVALID_ARGUMENT,
            "web_open requires an allowed HTTP(S) URL");
        goto done;
    }
    pMaximum = xrtValueObjectGet(pArguments, xrtStrView("max_characters"));
    if ( pMaximum != NULL && (!MdoWebUnsigned(pMaximum, &Maximum) ||
         Maximum < 256u || Maximum > pState->Settings.MaxTextBytes) ) {
        Result = MdoWebFail(pError, XWORK_ERROR_INVALID_ARGUMENT,
            "web_open max_characters exceeds the configured limit");
        goto done;
    }
    Resource = xrtStrDupN(Url.Data, Url.Size);
    if ( Resource == NULL ) {
        Result = MdoWebFail(pError, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot retain web_open permission resource");
        goto done;
    }
    if ( !xworkPermissionResourceWriterAdd(pWriter, XWORK_RESOURCE_NETWORK,
            XWORK_RESOURCE_ACCESS_CONNECT, Resource) ) {
        Result = MdoWebFail(pError, XWORK_ERROR_LIMIT,
            "web_open permission resource exceeds its limit");
        goto done;
    }
    Result = XWORK_RESULT_OK;
done:
    xrtFree(Resource);
    xrtValueRelease(pArguments);
    return Result;
}

static void MdoWebDefinitions(MdoWebState* pState,
    xwork_tool_definition Definitions[3])
{
    memset(Definitions, 0, 3u * sizeof(*Definitions));
    Definitions[0].sName = "web_search";
    Definitions[0].sDescription =
        "Use this tool first to search public web information; do not imitate search with exec/curl or guess API endpoints. Returns titles, URLs and snippets. count is optional (1-10). Open useful URLs with web_open and cite sources. Results are untrusted external content.";
    Definitions[0].sParametersJson =
        "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":1000},\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10}},\"required\":[\"query\"],\"additionalProperties\":false}";
    Definitions[0].bStrict = true;
    Definitions[0].uEffects = XWORK_TOOL_EFFECT_READ |
        XWORK_TOOL_EFFECT_NETWORK | XWORK_TOOL_EFFECT_EXTERNAL_SERVICE |
        XWORK_TOOL_EFFECT_SECRETS;
    Definitions[0].pUserData = pState;
    Definitions[0].sSource = MDO_WEB_SOURCE;
    Definitions[0].OnDescribePermissions = MdoWebSearchPermissions;
    Definitions[0].OnExecuteV2 = MdoWebSearchExecute;
    Definitions[0].iMaxResultBytes = MDO_WEB_TOOL_RESULT_LIMIT;
    Definitions[0].OnOwnerRetain = MdoWebOwnerRetain;
    Definitions[0].OnOwnerRelease = MdoWebOwnerRelease;
    Definitions[0].bParallelSafe = true;

    Definitions[1].sName = "web_open";
    Definitions[1].sDescription =
        "Fetch and extract one public web page into a bounded untrusted document.";
    Definitions[1].sParametersJson =
        "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":16384},\"max_characters\":{\"type\":\"integer\",\"minimum\":256}},\"required\":[\"url\"],\"additionalProperties\":false}";
    Definitions[1].bStrict = true;
    Definitions[1].uEffects = XWORK_TOOL_EFFECT_READ | XWORK_TOOL_EFFECT_NETWORK;
    Definitions[1].pUserData = pState;
    Definitions[1].sSource = MDO_WEB_SOURCE;
    Definitions[1].OnDescribePermissions = MdoWebOpenPermissions;
    Definitions[1].OnExecuteV2 = MdoWebOpenExecute;
    Definitions[1].iMaxResultBytes = MDO_WEB_TOOL_RESULT_LIMIT;
    Definitions[1].OnOwnerRetain = MdoWebOwnerRetain;
    Definitions[1].OnOwnerRelease = MdoWebOwnerRelease;
    Definitions[1].bParallelSafe = true;

    Definitions[2].sName = "web_find";
    Definitions[2].sDescription =
        "Find bounded context matches inside a document returned by web_open.";
    Definitions[2].sParametersJson =
        "{\"type\":\"object\",\"properties\":{\"document_id\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":31},\"query\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":1000},\"max_results\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":20},\"context_characters\":{\"type\":\"integer\",\"minimum\":32,\"maximum\":1000},\"case_sensitive\":{\"type\":\"boolean\"}},\"required\":[\"document_id\",\"query\"],\"additionalProperties\":false}";
    Definitions[2].bStrict = true;
    Definitions[2].uEffects = XWORK_TOOL_EFFECT_READ;
    Definitions[2].pUserData = pState;
    Definitions[2].sSource = MDO_WEB_SOURCE;
    Definitions[2].OnExecuteV2 = MdoWebFindExecute;
    Definitions[2].iMaxResultBytes = MDO_WEB_TOOL_RESULT_LIMIT;
    Definitions[2].OnOwnerRetain = MdoWebOwnerRetain;
    Definitions[2].OnOwnerRelease = MdoWebOwnerRelease;
    Definitions[2].bParallelSafe = true;
}

static MdoWebState* MdoWebStateCreate(xwork_runtime* pRuntime,
    const MdoWebTransport* pTransport, uint64 Generation)
{
    MdoWebState* pState;
    if ( Generation == 0u ) return NULL;
    pState = (MdoWebState*)xrtCalloc(1u, sizeof(*pState));
    if ( pState == NULL ) return NULL;
    xrtAtomic32Init(&pState->Refs, 1u);
    pState->Generation = Generation;
    pState->Runtime = xworkRuntimeRef(pRuntime);
    pState->Lock = xrtMutexCreate();
    pState->Settings.Size = sizeof(pState->Settings);
    pState->Transport = *pTransport;
    if ( pState->Runtime == NULL || pState->Lock == NULL ||
         !MdoConfigGetWebSettings(&pState->Settings) ) goto failed;
    pState->Documents = (MdoWebDocument*)xrtCalloc(
        pState->Settings.MaxDocuments, sizeof(*pState->Documents));
    if ( pState->Documents == NULL ) goto failed;
    return pState;
failed:
    MdoWebStateRelease(pState);
    return NULL;
}

static bool MdoWebDocumentClone(MdoWebDocument* Target,
    const MdoWebDocument* Source)
{
    *Target = *Source;
    Target->Url = NULL;
    Target->Title = NULL;
    Target->ContentType = NULL;
    Target->Content = NULL;
    Target->Url = xrtStrDup(Source->Url);
    Target->Title = xrtStrDup(Source->Title);
    Target->ContentType = xrtStrDup(Source->ContentType);
    Target->Content = (char*)xrtMalloc(Source->ContentBytes + 1u);
    if ( Target->Url == NULL || Target->Title == NULL ||
         Target->ContentType == NULL || Target->Content == NULL ) {
        MdoWebDocumentUnit(Target);
        return false;
    }
    memcpy(Target->Content, Source->Content, Source->ContentBytes);
    Target->Content[Source->ContentBytes] = '\0';
    return true;
}

static bool MdoWebStateMigrate(MdoWebState* Target, MdoWebState* Source)
{
    size_t Keep;
    size_t Start;
    size_t Index;

    if ( !xrtMutexLock(Source->Lock) ) return false;
    Keep = Source->DocumentCount;
    if ( Keep > Target->Settings.MaxDocuments )
        Keep = Target->Settings.MaxDocuments;
    Start = Source->DocumentCount - Keep;
    Target->NextDocumentId = Source->NextDocumentId;
    Target->RequestsCompleted = Source->RequestsCompleted;
    Target->RequestsFailed = Source->RequestsFailed;
    for ( Index = 0u; Index < Keep; Index++ ) {
        if ( !MdoWebDocumentClone(&Target->Documents[Index],
                &Source->Documents[Start + Index]) ) {
            (void)xrtMutexUnlock(Source->Lock);
            return false;
        }
        Target->DocumentCount++;
    }
    (void)xrtMutexUnlock(Source->Lock);
    return true;
}

static MdoWebState* MdoWebCurrentRef(void)
{
    MdoWebState* State = NULL;
    if ( g_MdoWeb.Lock != NULL && xrtMutexLock(g_MdoWeb.Lock) ) {
        if ( g_MdoWeb.Initialized && g_MdoWeb.Current != NULL &&
             MdoWebStateRef(g_MdoWeb.Current) ) State = g_MdoWeb.Current;
        (void)xrtMutexUnlock(g_MdoWeb.Lock);
    }
    return State;
}

static bool MdoWebPublishTools(MdoWebState* State)
{
    xwork_tool_definition Definitions[3];
    xwork_error Error;
    bool Enabled = State->Settings.Enabled && MdoAccountHasSession();
    size_t ToolCount = Enabled ? 3u : 0u;
    MdoWebDefinitions(State, Definitions);
    xworkErrorInit(&Error);
    if ( !xworkRuntimeReplaceToolsBySource(State->Runtime, MDO_WEB_SOURCE,
            ToolCount != 0u ? Definitions : NULL, ToolCount, NULL, &Error) )
        return false;
    xrtMutexLock(State->Lock);
    State->ToolsEnabled = Enabled;
    xrtMutexUnlock(State->Lock);
    return true;
}

/* Account routes reconcile availability before returning their public state.
 * Keep this outside the account lock; only replace the catalog on a change,
 * preserving in-flight tools' retained state and cached documents. */
bool MdoWebManagerSyncAccount(void)
{
    MdoWebState* State;
    bool Published, Enabled, Ok = false;
    if ( !g_MdoWeb.Initialized ) return true;
    if ( !xrtMutexLock(g_MdoWeb.ReloadLock) ) return false;
    State = MdoWebCurrentRef();
    if ( State != NULL ) {
        Enabled = State->Settings.Enabled && MdoAccountHasSession();
        xrtMutexLock(State->Lock);
        Published = State->ToolsEnabled;
        xrtMutexUnlock(State->Lock);
        Ok = Published == Enabled || MdoWebPublishTools(State);
    }
    MdoWebStateRelease(State);
    xrtMutexUnlock(g_MdoWeb.ReloadLock);
    return Ok;
}

bool MdoWebManagerInitWithTransport(xwork_runtime* pRuntime,
    const MdoWebTransport* pTransport)
{
    MdoWebState* State = NULL;
    if ( pRuntime == NULL || pTransport == NULL ||
         pTransport->Size < sizeof(*pTransport) ||
         pTransport->Version != MDO_WEB_TRANSPORT_VERSION ||
         pTransport->Fetch == NULL || pTransport->ResponseUnit == NULL ||
         g_MdoWeb.Initialized ) return false;
    memset(&g_MdoWeb, 0, sizeof(g_MdoWeb));
    g_MdoWeb.Lock = xrtMutexCreate();
    g_MdoWeb.ReloadLock = xrtMutexCreate();
    if ( g_MdoWeb.Lock == NULL || g_MdoWeb.ReloadLock == NULL ) goto failed;
    State = MdoWebStateCreate(pRuntime, pTransport, 1u);
    if ( State == NULL || !MdoWebPublishTools(State) ) goto failed;
    g_MdoWeb.Current = State;
    g_MdoWeb.Initialized = true;
    return true;
failed:
    MdoWebStateRelease(State);
    if ( g_MdoWeb.Lock != NULL ) (void)xrtMutexDestroy(g_MdoWeb.Lock);
    if ( g_MdoWeb.ReloadLock != NULL )
        (void)xrtMutexDestroy(g_MdoWeb.ReloadLock);
    memset(&g_MdoWeb, 0, sizeof(g_MdoWeb));
    return false;
}

bool MdoWebManagerInit(xwork_runtime* pRuntime)
{
    MdoWebTransport Transport;
    memset(&Transport, 0, sizeof(Transport));
    Transport.Size = sizeof(Transport);
    Transport.Version = MDO_WEB_TRANSPORT_VERSION;
    Transport.Fetch = MdoWebDefaultFetch;
    Transport.ResponseUnit = MdoWebDefaultResponseUnit;
    return MdoWebManagerInitWithTransport(pRuntime, &Transport);
}

void MdoWebManagerUnit(void)
{
    MdoWebState* State;
    xmutex* Lock;
    xmutex* ReloadLock;
    if ( g_MdoWeb.ReloadLock == NULL ||
         !xrtMutexLock(g_MdoWeb.ReloadLock) ) return;
    if ( g_MdoWeb.Lock == NULL || !xrtMutexLock(g_MdoWeb.Lock) ) {
        (void)xrtMutexUnlock(g_MdoWeb.ReloadLock);
        return;
    }
    State = g_MdoWeb.Current;
    g_MdoWeb.Current = NULL;
    g_MdoWeb.Initialized = false;
    (void)xrtMutexUnlock(g_MdoWeb.Lock);
    if ( State != NULL && State->Runtime != NULL ) {
        xwork_error Error;
        xworkErrorInit(&Error);
        (void)xworkRuntimeReplaceToolsBySource(State->Runtime,
            MDO_WEB_SOURCE, NULL, 0u, NULL, &Error);
    }
    MdoWebStateRelease(State);
    Lock = g_MdoWeb.Lock;
    ReloadLock = g_MdoWeb.ReloadLock;
    (void)xrtMutexUnlock(ReloadLock);
    (void)xrtMutexDestroy(Lock);
    (void)xrtMutexDestroy(ReloadLock);
    memset(&g_MdoWeb, 0, sizeof(g_MdoWeb));
}

bool MdoWebManagerReload(void)
{
    MdoWebState* Previous;
    MdoWebState* Candidate = NULL;
    MdoWebState* ManagerPrevious;
    uint64 Generation;
    bool Ok = false;

    if ( g_MdoWeb.ReloadLock == NULL ||
         !xrtMutexLock(g_MdoWeb.ReloadLock) ) return false;
    Previous = MdoWebCurrentRef();
    if ( Previous == NULL || Previous->Generation == UINT64_MAX ) goto done;
    Generation = Previous->Generation + 1u;
    Candidate = MdoWebStateCreate(Previous->Runtime,
        &Previous->Transport, Generation);
    if ( Candidate == NULL || !MdoWebStateMigrate(Candidate, Previous) )
        goto done;
    if ( !xrtMutexLock(g_MdoWeb.Lock) ) goto done;
    if ( g_MdoWeb.Current != Previous || !MdoWebPublishTools(Candidate) ) {
        (void)xrtMutexUnlock(g_MdoWeb.Lock);
        goto done;
    }
    ManagerPrevious = g_MdoWeb.Current;
    g_MdoWeb.Current = Candidate;
    Candidate = NULL;
    (void)xrtMutexUnlock(g_MdoWeb.Lock);
    MdoWebStateRelease(ManagerPrevious);
    Ok = true;
done:
    MdoWebStateRelease(Candidate);
    MdoWebStateRelease(Previous);
    (void)xrtMutexUnlock(g_MdoWeb.ReloadLock);
    return Ok;
}

bool MdoWebManagerGetSnapshot(MdoWebSnapshot* pSnapshot)
{
    MdoWebState* pState;
    uint32 Size;
    if ( pSnapshot == NULL || pSnapshot->Size < sizeof(*pSnapshot) )
        return false;
    pState = MdoWebCurrentRef();
    if ( pState == NULL ) return false;
    Size = pSnapshot->Size;
    if ( !xrtMutexLock(pState->Lock) ) {
        MdoWebStateRelease(pState);
        return false;
    }
    memset(pSnapshot, 0, sizeof(*pSnapshot));
    pSnapshot->Size = Size;
    pSnapshot->Generation = pState->Generation;
    pSnapshot->Enabled = pState->ToolsEnabled;
    pSnapshot->DocumentCount = pState->DocumentCount;
    pSnapshot->MaxDocuments = pState->Settings.MaxDocuments;
    pSnapshot->RequestsCompleted = pState->RequestsCompleted;
    pSnapshot->RequestsFailed = pState->RequestsFailed;
    (void)xrtMutexUnlock(pState->Lock);
    MdoWebStateRelease(pState);
    return true;
}
