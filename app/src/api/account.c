#include "internal.h"
#include "../account/internal.h"
#include "../../include/mdo/web.h"

static bool MdoApiAccountPathEquals(xstrview value, cstr text)
{ return value.Size == strlen(text) && !memcmp(value.Data, text, value.Size); }
static void MdoApiAccountBodyUnit(MdoApiJsonBody* Body)
{
    xstrview password;
    if (xrtValueGetString(xrtValueObjectGet(Body->Value, xrtStrView("password")), &password))
        xrtSecureZero((void*)password.Data, password.Size);
    if (Body->Document) xrtSecureZero(Body->Document, Body->Size);
    MdoApiJsonBodyUnit(Body);
}

/* Browser callback data is parsed once, strictly. PKCE, the pending nonce and
 * exchanged tokens remain owned by native code. */
static bool MdoApiAccountCallbackQuery(xstrview Query)
{
    char state[65] = "", code[65] = "", error[32] = "";
    unsigned seen = 0; size_t at = 0;
    if (!Query.Size || Query.Size > 1024) return false;
    while (at < Query.Size) {
        size_t end = at, equal; char *key = NULL, *value = NULL; bool ok = false;
        while (end < Query.Size && Query.Data[end] != '&') end++;
        equal = at; while (equal < end && Query.Data[equal] != '=') equal++;
        if (equal == end) return false;
        size_t kn = 0, vn = 0;
        key = (char*)xrtPercentDecodeNew((xstrview){Query.Data + at, equal - at}, &kn);
        value = (char*)xrtPercentDecodeNew((xstrview){Query.Data + equal + 1, end - equal - 1}, &vn);
        unsigned bit = 0; char* destination = NULL; size_t limit = 0;
        if (key && value && !memchr(key, 0, kn) && !memchr(value, 0, vn)) {
            if (!strcmp(key, "state")) { bit = 1; destination = state; limit = sizeof(state); }
            else if (!strcmp(key, "code")) { bit = 2; destination = code; limit = sizeof(code); }
            else if (!strcmp(key, "error")) { bit = 4; destination = error; limit = sizeof(error); }
            if (bit && !(seen & bit) && vn < limit) { memcpy(destination, value, vn); destination[vn] = 0; seen |= bit; ok = true; }
        }
        xrtFree(key); xrtFree(value); if (!ok) return false;
        at = end + 1; if (at == Query.Size) return false;
    }
    return (seen == 3 || seen == 5) && MdoAccountCallback(state, code, error);
}
bool MdoApiAccountRoute(MdoApiContext* Context)
{
    if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account")) {
        xvalue* snapshot = MdoAccountSnapshot();
        (void)MdoWebManagerSyncAccount();
        return MdoApiReplySuccessTake(Context, 200, snapshot, NULL);
    }
    if (MdoApiAccountPathEquals(Context->Target.Path, MDO_ACCOUNT_CALLBACK_PATH) &&
        Context->Request->head->MethodCode == XHTTP_METHOD_GET) {
        bool ok = MdoApiAccountCallbackQuery(Context->Target.Query);
        cstr html = ok ? "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>墨斗登录</title><p>已收到登录结果，请返回墨斗。<p>Login response received. Return to mdo.</p>" :
            "<!doctype html><meta charset=utf-8><title>墨斗登录</title><p>登录请求已过期或不匹配，请返回墨斗重新登录。</p>";
        return MdoApiReplyAccountHtml(Context, ok ? 200 : 400, html);
    }
    MdoApiJsonBody body; MdoApiBodyStatus status = MdoApiJsonBodyRead(Context, &body);
    if (status != MDO_API_BODY_OK) return MdoApiReplyBodyError(Context, status);
    if (xrtValueType(body.Value) != XVALUE_OBJECT) {
        MdoApiAccountBodyUnit(&body); return MdoApiReplyError(Context, 400, "invalid_account_request", "A JSON object is required", NULL);
    }
    bool ok = false; xvalue* out = NULL;
    if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account/login")) {
        bool remember = false; const xhttpfield* host = NULL; char origin[600] = "";
        cstr identifier = MdoAccountText(body.Value, "identifier", 254);
        cstr password = MdoAccountText(body.Value, "password", 128);
        if (xrtValueCount(body.Value) == 3 && identifier && password &&
            xrtValueGetBool(xrtValueObjectGet(body.Value, xrtStrView("remember")), &remember)) {
            ok = MdoAccountLoginPassword(identifier, password, remember);
        } else if (xrtValueCount(body.Value) == 1 && xrtValueGetBool(xrtValueObjectGet(body.Value, xrtStrView("remember")), &remember) &&
            xrtHttpFieldGetUnique(Context->Request->head->Fields, Context->Request->head->FieldCount,
                XRT_STR_LITERAL("Host"), &host) == XHTTP_NEXT_ITEM && host && host->Value.Size < 550 &&
            !memchr(host->Value.Data, 0, host->Value.Size)) {
            snprintf(origin, sizeof(origin), "http://%.*s", (int)host->Value.Size, host->Value.Data);
            ok = MdoAccountLoginStart(origin, remember, &out);
        }
    } else if (MdoApiAccountPathEquals(Context->Target.Path, MDO_ACCOUNT_CALLBACK_PATH)) {
        cstr url = MdoAccountText(body.Value, "callback_url", 4096);
        size_t base = strlen(MDO_ACCOUNT_ANDROID_CALLBACK);
        ok = xrtValueCount(body.Value) == 1 && url && !strncmp(url, MDO_ACCOUNT_ANDROID_CALLBACK, base) &&
            url[base] == '?' && !strchr(url, '#') && MdoApiAccountCallbackQuery(xrtStrView(url + base + 1));
    } else if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account/search/skip")) {
        uint64 id = 0; ok = xrtValueCount(body.Value) == 1 &&
            MdoAccountGetUInt(xrtValueObjectGet(body.Value, xrtStrView("id")), &id) && MdoAccountSkipSearch(id);
    } else if (xrtValueCount(body.Value) == 0) {
        if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account/cancel")) ok = MdoAccountLoginCancel();
        else if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account/logout")) ok = MdoAccountLogout();
        else if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account/refresh")) ok = MdoAccountRefresh();
        else if (MdoApiAccountPathEquals(Context->Target.Path, "/api/v1/account/website")) ok = MdoAccountOpenWebsite("/account/index.html");
    }
    MdoApiAccountBodyUnit(&body);
    if (!ok) { xrtValueRelease(out); return MdoApiReplyError(Context, 409, "account_action_unavailable",
        "Account action is unavailable; check the input or start a new login", NULL); }
    if (!out) out = MdoAccountSnapshot();
    (void)MdoWebManagerSyncAccount();
    return MdoApiReplySuccessTake(Context, 200, out, NULL);
}
