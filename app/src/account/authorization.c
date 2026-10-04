#include "internal.h"

bool MdoAccountAuthorizationStart(cstr Origin, cstr LocalOrigin, bool Remember,
    MdoAccountAuthorization* Authorization)
{
    uint8 digest[32]; char encoded[45], challenge[44]; size_t size = 0, i;
    char canonical[MDO_ACCOUNT_ORIGIN_LIMIT];
    memset(Authorization, 0, sizeof(*Authorization));
    if (!MdoAccountOrigin(Origin, canonical) || strcmp(Origin, canonical) ||
        !MdoAccountRandom(Authorization->State) || !MdoAccountRandom(Authorization->Verifier)) return false;
    strcpy(Authorization->Origin, Origin);
#if defined(__ANDROID__)
    strcpy(Authorization->Client, "mdo-android");
    /* Android App Links are bound to the published domain and package. A
     * self-hosted service must register its own signed Android client. */
    if (strcmp(Origin, "https://ai.xywhsoft.com")) return false;
    strcpy(Authorization->Redirect, MDO_ACCOUNT_ANDROID_CALLBACK);
#else
    char local[MDO_ACCOUNT_ORIGIN_LIMIT];
    if (!MdoAccountOrigin(LocalOrigin, local) || strcmp(LocalOrigin, local) || strncmp(local, "http://", 7) ||
        (strncmp(local + 7, "127.0.0.1:", 10) && strncmp(local + 7, "[::1]:", 6))) return false;
    strcpy(Authorization->Client, "mdo-desktop");
    snprintf(Authorization->Redirect, sizeof(Authorization->Redirect), "%s%s", local, MDO_ACCOUNT_CALLBACK_PATH);
#endif
    if (!xrtSha256(Authorization->Verifier, strlen(Authorization->Verifier), digest) ||
        !xrtBase64Encode(digest, sizeof(digest), encoded, sizeof(encoded), &size, NULL) || size != 44) return false;
    for (i = 0; i < 43; i++) challenge[i] = encoded[i] == '+' ? '-' : encoded[i] == '/' ? '_' : encoded[i];
    challenge[43] = 0;
    char* callback = xrtPercentEncodeNew(Authorization->Redirect, strlen(Authorization->Redirect), xrtStrView(""), NULL);
    bool ok = callback && snprintf(Authorization->Url, sizeof(Authorization->Url),
        "%s/api/v1/auth/authorize?response_type=code&client_id=%s&redirect_uri=%s&state=%s&code_challenge=%s&code_challenge_method=S256",
        Origin, Authorization->Client, callback, Authorization->State, challenge) < (int)sizeof(Authorization->Url);
    xrtFree(callback); xrtSecureZero(digest, sizeof(digest));
    Authorization->Remember = Remember && xsCredentialProtectionAvailable();
    Authorization->Expires = xrtNow() / 1000000 + 600;
    return ok;
}
bool MdoAccountAuthorizationSave(const MdoAccountAuthorization* Authorization)
{
#if defined(__ANDROID__)
    xvalue* value = xrtValueObject();
    bool ok = value && MdoAccountSetString(value, "state", Authorization->State) &&
        MdoAccountSetString(value, "verifier", Authorization->Verifier) &&
        MdoAccountSetUInt(value, "expires_at", (uint64)Authorization->Expires) &&
        MdoAccountSetBool(value, "remember", Authorization->Remember) &&
        MdoAccountCredentialWrite(MDO_ACCOUNT_PENDING_PATH, Authorization->Origin, value);
    cstr verifier = MdoAccountText(value, "verifier", 64);
    if (verifier) xrtSecureZero((void*)verifier, strlen(verifier));
    xrtValueRelease(value); return ok;
#else
    (void)Authorization; return true;
#endif
}
bool MdoAccountAuthorizationLoad(MdoAccountAuthorization* Authorization)
{
#if defined(__ANDROID__)
    char origin[MDO_ACCOUNT_ORIGIN_LIMIT];
    xvalue* value = MdoAccountCredentialRead(MDO_ACCOUNT_PENDING_PATH, origin);
    cstr state = MdoAccountText(value, "state", 64);
    cstr verifier = MdoAccountText(value, "verifier", 64);
    uint64 expires = 0; bool remember = false;
    bool ok = value && !strcmp(origin, "https://ai.xywhsoft.com") && MdoAccountHex(state, 64) && MdoAccountHex(verifier, 64) &&
        MdoAccountGetUInt(xrtValueObjectGet(value, xrtStrView("expires_at")), &expires) &&
        expires > (uint64)(xrtNow() / 1000000) && expires <= (uint64)(xrtNow() / 1000000 + 600) &&
        xrtValueGetBool(xrtValueObjectGet(value, xrtStrView("remember")), &remember);
    if (ok) {
        memset(Authorization, 0, sizeof(*Authorization));
        strcpy(Authorization->State, state); strcpy(Authorization->Verifier, verifier);
        strcpy(Authorization->Client, "mdo-android"); strcpy(Authorization->Redirect, MDO_ACCOUNT_ANDROID_CALLBACK);
        strcpy(Authorization->Origin, origin); Authorization->Expires = (int64)expires; Authorization->Remember = remember;
    }
    if (verifier) xrtSecureZero((void*)verifier, strlen(verifier));
    xrtValueRelease(value);
    if (!ok) (void)MdoHomeRemove(MDO_ACCOUNT_PENDING_PATH, false);
    return ok;
#else
    (void)Authorization; return false;
#endif
}
