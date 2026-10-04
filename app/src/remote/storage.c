#include <stdio.h>
#include <string.h>
#include "storage.h"

bool MdoRemoteNameValid(cstr Name)
{
    if (!Name || !Name[0] || strlen(Name) > 96u || !xrtUtf8Valid(xrtStrView(Name),NULL)) return false;
    for (size_t i = 0u; Name[i]; i++)
        if ((unsigned char)Name[i] < 32u || (unsigned char)Name[i] == 127u) return false;
    return true;
}
static bool MdoRemoteConfigValid(const MdoRemoteConfig* Config)
{
    return Config && MdoRemoteNameValid(Config->Name) && (!Config->AllowRemote || Config->MemberId);
}
bool MdoRemoteConfigLoad(MdoRemoteConfig* Config)
{
    if (!Config) return false;
    memset(Config,0,sizeof(*Config)); strcpy(Config->Name,"mdo");
    bool exists = false; xfileinfo info; xfile file = NULL; xvalue* value = NULL; bool ok = false;
    char document[2049]; size_t read = 0u;
    if (!MdoHomeExternalStat(MDO_REMOTE_CONFIG_PATH,&exists,&info)) return false;
    if (!exists) return true;
    if (!info.Size || info.Size > sizeof(document)-1u) return false;
    file = MdoHomeOpenRead(MDO_REMOTE_CONFIG_PATH); if (!file) return false;
    /* Stat the opened handle too, so a replaced/grown file cannot make the
     * earlier pathname size authorize unbounded or truncated JSON. */
    if (!xrtFileStat(file,&info) || !info.Size || info.Size > sizeof(document)-1u) goto done;
    while (read < info.Size) {
        size_t count = 0u;
        if (!xrtRead(file,document+read,(size_t)info.Size-read,&count) || !count) goto done;
        read += count;
    }
    document[read] = 0;
    xjsonreadconfig limits; xrtJsonReadConfigInit(&limits);
    limits.MaxInputBytes = sizeof(document)-1u; limits.MaxDepth = 2u;
    limits.MaxValues = 8u; limits.MaxStringBytes = 96u;
    value = xrtJsonRead(xrtStrViewN(document,read),&limits);
    uint64 schema = 0u, member = 0u; bool allow = false;
    cstr name = MdoAccountText(value,"name",96u);
    if (xrtValueType(value) != XVALUE_OBJECT || xrtValueCount(value) != 4u || !MdoRemoteNameValid(name) ||
        !MdoAccountGetUInt(xrtValueObjectGet(value,XRT_STR_LITERAL("schema_version")),&schema) || schema != 1u ||
        !MdoAccountGetUInt(xrtValueObjectGet(value,XRT_STR_LITERAL("member_id")),&member) ||
        !xrtValueGetBool(xrtValueObjectGet(value,XRT_STR_LITERAL("allow_remote")),&allow) || (allow && !member)) goto done;
    strcpy(Config->Name,name); Config->MemberId = member; Config->AllowRemote = allow; ok = true;
done:
    xrtValueRelease(value); xrtClose(file);
    /* Config always stays at the disabled default on a rejected document. */
    return ok;
}
bool MdoRemoteConfigSave(const MdoRemoteConfig* Config)
{
    if (!MdoRemoteConfigValid(Config)) return false;
    xvalue* value = xrtValueObject(); size_t size = 0u; char* document = NULL;
    bool ok = value && MdoAccountSetUInt(value,"schema_version",1u) &&
        MdoAccountSetString(value,"name",Config->Name) &&
        MdoAccountSetUInt(value,"member_id",Config->MemberId) &&
        MdoAccountSetBool(value,"allow_remote",Config->AllowRemote);
    if (ok) document = xrtJsonStringify(value,true,&size);
    ok = document && MdoHomeAtomicWrite(MDO_REMOTE_CONFIG_PATH,document,size,false);
    xrtFree(document); xrtValueRelease(value); return ok;
}
void MdoRemoteIdentityClear(MdoRemoteIdentity* Identity)
{
    if (Identity) xrtSecureZero(Identity,sizeof(*Identity));
}
bool MdoRemoteIdentityLoad(uint64 MemberId, bool Create, MdoRemoteIdentity* Identity)
{
    char path[128], origin[MDO_ACCOUNT_ORIGIN_LIMIT] = ""; bool exists = false, ok = false;
    xfileinfo info; xvalue* value = NULL;
    if (!Identity) return false;
    memset(Identity,0,sizeof(*Identity));
    if (!MemberId || snprintf(path,sizeof(path),"data/remote/accounts/%llu.bin",(unsigned long long)MemberId) >= (int)sizeof(path) ||
        !MdoHomeExternalStat(path,&exists,&info)) return false;
    if (exists) {
        value = MdoAccountCredentialRead(path,origin);
        cstr id = MdoAccountText(value,"device_id",32u), secret = MdoAccountText(value,"device_secret",64u);
        uint64 schema = 0u, member = 0u;
        if (xrtValueType(value) != XVALUE_OBJECT || xrtValueCount(value) != 4u ||
            strcmp(origin,MDO_ACCOUNT_SERVICE_ORIGIN) || !MdoAccountHex(id,32u) || !MdoAccountHex(secret,64u) ||
            !MdoAccountGetUInt(xrtValueObjectGet(value,XRT_STR_LITERAL("schema_version")),&schema) || schema != 1u ||
            !MdoAccountGetUInt(xrtValueObjectGet(value,XRT_STR_LITERAL("member_id")),&member) || member != MemberId) goto done;
        Identity->MemberId = member; strcpy(Identity->Id,id); strcpy(Identity->Secret,secret);
        Identity->Persistent = true; ok = true; goto done;
    }
    if (!Create) return false;
    char random[65] = "";
    if (!MdoAccountRandom(random)) goto done;
    memcpy(Identity->Id,random,32u); Identity->Id[32] = 0;
    xrtSecureZero(random,sizeof(random));
    if (!MdoAccountRandom(Identity->Secret)) goto done;
    Identity->MemberId = MemberId;
    if (!xsCredentialProtectionAvailable()) { ok = true; goto done; }
    value = xrtValueObject();
    ok = value && MdoAccountSetUInt(value,"schema_version",1u) &&
        MdoAccountSetUInt(value,"member_id",MemberId) && MdoAccountSetString(value,"device_id",Identity->Id) &&
        MdoAccountSetString(value,"device_secret",Identity->Secret) &&
        MdoAccountCredentialWrite(path,MDO_ACCOUNT_SERVICE_ORIGIN,value);
    Identity->Persistent = ok;
done:
    MdoAccountSecretValueRelease(value);
    if (!ok) MdoRemoteIdentityClear(Identity); return ok;
}
