#include "internal.h"

/* The only on-disk credential is a device-bound encrypted envelope. No
 * plaintext fallback, backup copy, token in config, or provider key here. */
bool MdoAccountCredentialWrite(cstr Path, cstr Origin, const xvalue* Value)
{
    char scope[600]; char* plain = NULL; bytes envelope = NULL; bytes document = NULL;
    size_t plain_size = 0, envelope_size = 0, head_size; bool ok = false;
    if (snprintf(scope, sizeof(scope), "mdo.account:%s:%s", Path, Origin) >= (int)sizeof(scope)) return false;
    plain = xrtJsonStringify(Value, false, &plain_size);
    if (!plain || !xsCredentialSeal(scope, plain, plain_size, &envelope, &envelope_size)) goto done;
    head_size = strlen("MDOACCOUNT1\n") + strlen(Origin) + 1;
    document = xrtMalloc(head_size + envelope_size);
    if (!document) goto done;
    memcpy(document, "MDOACCOUNT1\n", strlen("MDOACCOUNT1\n"));
    memcpy(document + strlen("MDOACCOUNT1\n"), Origin, strlen(Origin)); document[head_size - 1] = '\n';
    memcpy(document + head_size, envelope, envelope_size);
    ok = MdoHomeAtomicWrite(Path, document, head_size + envelope_size, false);
done:
    if (plain) { xrtSecureZero(plain, plain_size); xrtFree(plain); }
    xrtFree(envelope); xrtFree(document); return ok;
}
xvalue* MdoAccountCredentialRead(cstr Path, char Origin[MDO_ACCOUNT_ORIGIN_LIMIT])
{
    xfile file = NULL; bytes document = NULL, plain = NULL; size_t size = 0, plain_size = 0;
    xvalue* result = NULL; xfileinfo info; char scope[600], canonical[MDO_ACCOUNT_ORIGIN_LIMIT];
    file = MdoHomeOpenRead(Path);
    if (!file || !xrtFileStat(file, &info) || info.Size > XS_CREDENTIAL_ENVELOPE_LIMIT + 600 || info.Size < 13) goto done;
    size = (size_t)info.Size; document = xrtMalloc(size);
    if (!document) goto done;
    size_t read = 0;
    while (read < size) {
        size_t n = 0;
        if (!xrtRead(file, document + read, size - read, &n) || !n) goto done;
        read += n;
    }
    const size_t magic = strlen("MDOACCOUNT1\n");
    if (memcmp(document, "MDOACCOUNT1\n", magic)) goto done;
    bytes newline = memchr(document + magic, '\n', size - magic);
    if (!newline || (size_t)(newline - document - magic) >= MDO_ACCOUNT_ORIGIN_LIMIT) goto done;
    size_t origin_size = (size_t)(newline - document - magic);
    memcpy(Origin, document + magic, origin_size); Origin[origin_size] = 0;
    if (memchr(Origin, 0, origin_size) || !MdoAccountOrigin(Origin, canonical) || strcmp(Origin, canonical)) goto done;
    snprintf(scope, sizeof(scope), "mdo.account:%s:%s", Path, Origin);
    size_t head_size = (size_t)(newline - document) + 1;
    if (!xsCredentialUnseal(scope, document + head_size, size - head_size, &plain, &plain_size)) goto done;
    xjsonreadconfig limits; xrtJsonReadConfigInit(&limits);
    limits.MaxInputBytes = XS_CREDENTIAL_PLAIN_LIMIT; limits.MaxDepth = 5; limits.MaxValues = 64;
    result = xrtJsonRead((xstrview){(cstr)plain, plain_size}, &limits);
    if (xrtValueType(result) != XVALUE_OBJECT) { xrtValueRelease(result); result = NULL; }
done:
    if (file) xrtClose(file);
    if (plain) { xrtSecureZero(plain, plain_size); xrtFree(plain); }
    xrtFree(document); return result;
}
bool MdoAccountCredentialSaveLocked(void)
{
    if (!g_MdoAccount.Remember) {
        g_MdoAccount.Saved = false;
        return MdoHomeRemove(MDO_ACCOUNT_SESSION_PATH, false);
    }
    xvalue* value = xrtValueObject();
    bool ok = value && MdoAccountSetUInt(value, "schema_version", 1) &&
        MdoAccountSetString(value, "refresh_token", g_MdoAccount.Tokens.Refresh) &&
        MdoAccountSetUInt(value, "member_id", g_MdoAccount.Tokens.MemberId);
    if (ok && g_MdoAccount.Profile) ok = xrtValueObjectSet(value, xrtStrView("profile"), g_MdoAccount.Profile);
    ok = ok && MdoAccountCredentialWrite(MDO_ACCOUNT_SESSION_PATH, g_MdoAccount.Origin, value);
    cstr refresh = MdoAccountText(value, "refresh_token", 64);
    if (refresh) xrtSecureZero((void*)refresh, strlen(refresh));
    xrtValueRelease(value); g_MdoAccount.Saved = ok;
    if (!ok) {
        /* A rotated refresh must never leave an older replayable credential
         * on disk. Current memory login remains usable until this run ends. */
        (void)MdoHomeRemove(MDO_ACCOUNT_SESSION_PATH, false);
        strcpy(g_MdoAccount.Message, "credential_save_failed");
    }
    return ok;
}
