#include "internal.h"

bool MdoAccountSetString(xvalue* Object, cstr Key, cstr Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueString(xrtStrView(Value ? Value : "")));
}
bool MdoAccountSetUInt(xvalue* Object, cstr Key, uint64 Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueUInt(Value));
}
bool MdoAccountSetBool(xvalue* Object, cstr Key, bool Value)
{
    return xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueBool(Value));
}
bool MdoAccountGetUInt(const xvalue* Value, uint64* Output)
{
    int64 signed_value;
    if (xrtValueType(Value) == XVALUE_UINT) return xrtValueGetUInt(Value, Output);
    if (xrtValueType(Value) != XVALUE_INT || !xrtValueGetInt(Value, &signed_value) || signed_value < 0) return false;
    *Output = (uint64)signed_value; return true;
}
cstr MdoAccountText(const xvalue* Value, cstr Key, size_t Limit)
{
    xstrview text;
    if (!xrtValueGetString(xrtValueObjectGet(Value, xrtStrView(Key)), &text) ||
        text.Size > Limit || memchr(text.Data, 0, text.Size)) return NULL;
    return text.Data;
}
bool MdoAccountHex(cstr Text, size_t Length)
{
    size_t i;
    if (!Text || strlen(Text) != Length) return false;
    for (i = 0; i < Length; i++) if (!((Text[i] >= '0' && Text[i] <= '9') || (Text[i] >= 'a' && Text[i] <= 'f'))) return false;
    return true;
}
bool MdoAccountRandom(char Output[65])
{
    uint8 random[32]; size_t i;
    if (!xrtSecureRandom(random, sizeof(random))) return false;
    for (i = 0; i < sizeof(random); i++) snprintf(Output + i * 2, 3, "%02x", random[i]);
    xrtSecureZero(random, sizeof(random)); return true;
}
bool MdoAccountTokenValid(cstr Text)
{
    size_t i, n = Text ? strlen(Text) : 0;
    if (!n || n >= MDO_ACCOUNT_TOKEN_LIMIT) return false;
    for (i = 0; i < n; i++) if (!((Text[i] >= 'a' && Text[i] <= 'z') || (Text[i] >= 'A' && Text[i] <= 'Z') ||
        (Text[i] >= '0' && Text[i] <= '9') || Text[i] == '.' || Text[i] == '-' || Text[i] == '_')) return false;
    return true;
}
bool MdoAccountOrigin(cstr Url, char Output[MDO_ACCOUNT_ORIGIN_LIMIT])
{
    size_t scheme, end, i;
    if (!Url || !MdoHttpUrlValid(xrtStrView(Url), true)) return false;
    scheme = MdoHttpUrlAsciiEqual(Url, "https://", 8) ? 8 : 7;
    end = scheme; while (Url[end] && Url[end] != '/' && Url[end] != '?') end++;
    if (end >= MDO_ACCOUNT_ORIGIN_LIMIT) return false;
    for (i = 0; i < end; i++) Output[i] = (char)tolower((unsigned char)Url[i]);
    Output[end] = 0;
    if (scheme == 7 && strncmp(Output + 7, "127.0.0.1:", 10) && strcmp(Output + 7, "127.0.0.1") &&
        strncmp(Output + 7, "[::1]:", 6) && strcmp(Output + 7, "[::1]")) return false;
    if (scheme == 8 && end > 4 && !strcmp(Output + end - 4, ":443")) Output[end - 4] = 0;
    if (scheme == 7 && end > 3 && !strcmp(Output + end - 3, ":80")) Output[end - 3] = 0;
    return true;
}
xvalue* MdoAccountClient(cstr Origin, cstr Path, cstr Method, const xvalue* Body,
    cstr Access, xcancel* Cancel, uint16* Status)
{
    char url[1024], authorization[MDO_ACCOUNT_TOKEN_LIMIT + 8];
    XS_FetchRequest request; XS_FetchResponse response;
    XS_FetchHeader headers[4]; size_t count = 0, body_size = 0;
    char* body = NULL; xvalue* envelope = NULL; xvalue* data = NULL;
    char canonical[MDO_ACCOUNT_ORIGIN_LIMIT];
    if (Status) *Status = 0;
    if (!MdoAccountOrigin(Origin, canonical) || strcmp(Origin, canonical) || !Path || Path[0] != '/' || Path[1] == '/') return NULL;
    if (snprintf(url, sizeof(url), "%s%s", Origin, Path) >= (int)sizeof(url)) return NULL;
    if (Body && !(body = xrtJsonStringify(Body, false, &body_size))) return NULL;
    memset(&request, 0, sizeof(request)); memset(&response, 0, sizeof(response));
    memset(authorization, 0, sizeof(authorization));
    headers[count++] = (XS_FetchHeader){"Accept", "application/json"};
    headers[count++] = (XS_FetchHeader){"User-Agent", "mdo/1 account"};
    if (Body) headers[count++] = (XS_FetchHeader){"Content-Type", "application/json"};
    if (Access) {
        if (!MdoAccountTokenValid(Access)) goto done;
        snprintf(authorization, sizeof(authorization), "Bearer %s", Access);
        headers[count++] = (XS_FetchHeader){"Authorization", authorization};
    }
    request.Size = sizeof(request); request.Version = XS_FETCH_REQUEST_VERSION;
    request.Url = url; request.Method = Method; request.Headers = headers; request.HeaderCount = count;
    request.Body = body; request.BodySize = body_size; request.Cancel = Cancel;
    request.Timeout = 15000000u; request.IdleTimeout = 10000000u; request.MaxBodyBytes = 64u * 1024u;
    /* Never forward credentials across a redirect or retry an ambiguous POST. */
    if (!xsFetch(&request, &response)) goto done;
    if (Status) *Status = response.Status;
    if (response.Status != 200 || !response.Body || !xrtUtf8Valid((xstrview){(cstr)response.Body, response.BodySize}, NULL)) goto done;
    xjsonreadconfig limits; xrtJsonReadConfigInit(&limits);
    limits.MaxInputBytes = 64u * 1024u; limits.MaxDepth = 6; limits.MaxValues = 256; limits.MaxStringBytes = 8192;
    envelope = xrtJsonRead((xstrview){(cstr)response.Body, response.BodySize}, &limits);
    uint64 code = 1;
    if (!envelope || !MdoAccountGetUInt(xrtValueObjectGet(envelope, xrtStrView("code")), &code) || code != 0) {
        if (Status) *Status = 502;
        goto done;
    }
    const xvalue* value = xrtValueObjectGet(envelope, xrtStrView("data"));
    /* Logout may return null data; callers inspect the actual HTTP status. */
    if (xrtValueType(value) == XVALUE_OBJECT) data = xrtValueClone(value);
done:
    if (body) { xrtSecureZero(body, body_size); xrtFree(body); }
    if (response.Body) xrtSecureZero(response.Body, response.BodySize);
    xsFetchResponseUnit(&response); xrtValueRelease(envelope);
    xrtSecureZero(authorization, sizeof(authorization)); return data;
}
bool MdoAccountClientTokens(const xvalue* Data, MdoAccountTokens* Tokens)
{
    cstr access = MdoAccountText(Data, "access_token", MDO_ACCOUNT_TOKEN_LIMIT - 1);
    cstr refresh = MdoAccountText(Data, "refresh_token", 64);
    cstr type = MdoAccountText(Data, "token_type", 16);
    uint64 expiry = 0, id = 0;
    if (!MdoAccountTokenValid(access) || !MdoAccountHex(refresh, 64) || !type || strcmp(type, "Bearer") ||
        !MdoAccountGetUInt(xrtValueObjectGet(Data, xrtStrView("expires_in")), &expiry) || !expiry || expiry > 900 ||
        !MdoAccountGetUInt(xrtValueObjectGet(Data, xrtStrView("id")), &id) || !id) return false;
    memset(Tokens, 0, sizeof(*Tokens)); strcpy(Tokens->Access, access); strcpy(Tokens->Refresh, refresh);
    Tokens->Expires = xrtDeadlineAfter(expiry * 1000000u); Tokens->MemberId = id; return true;
}
