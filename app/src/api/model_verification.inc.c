/* Verification is evidence for an exact local profile, not a capability
 * catalog. Store no prompts, responses, endpoints or resolved credentials. */
#include "../../include/mdo/home.h"

static bool MdoModelVerificationHash(const void* Bytes, size_t Size, char Hash[65])
{
    uint8 Digest[32];
    if (!xrtSha256(Bytes, Size, Digest)) return false;
    for (size_t i = 0u; i < 32u; ++i) snprintf(Hash + i * 2u, 3u, "%02x", Digest[i]);
    return true;
}

static const xvalue* MdoModelVerificationFind(const xvalue* Items, cstr Id)
{
    for (size_t i = 0u; i < xrtValueCount(Items); ++i) {
        const xvalue* Item = xrtValueArrayGet(Items, i); xstrview Name = {0};
        if (xrtValueGetString(xrtValueObjectGet(Item, XRT_STR_LITERAL("id")), &Name) &&
            Name.Size == strlen(Id) && !memcmp(Name.Data, Id, Name.Size)) return Item;
    }
    return NULL;
}

static bool MdoModelVerificationKey(const xvalue* Models, const xvalue* Transport,
    cstr Id, char Hash[65])
{
    const xvalue* Model = MdoModelVerificationFind(xrtValueObjectGet(Models, XRT_STR_LITERAL("items")), Id);
    xstrview ProviderId = {0}; char ProviderName[129];
    if (!Model || !xrtValueGetString(xrtValueObjectGet(Model, XRT_STR_LITERAL("provider")), &ProviderId) ||
        ProviderId.Size >= sizeof(ProviderName)) return false;
    memcpy(ProviderName, ProviderId.Data, ProviderId.Size); ProviderName[ProviderId.Size] = 0;
    const xvalue* Provider = MdoModelVerificationFind(xrtValueObjectGet(Models, XRT_STR_LITERAL("providers")), ProviderName);
    xvalue* Binding = xrtValueArray(); size_t Size = 0u; char* Json = NULL;
    /* Stable effective JSON also includes immutable vault references. Changing
     * default selection or unrelated appearance settings keeps valid evidence. */
    bool Ok = Provider && Binding && xrtValueArrayAppend(Binding, (xvalue*)Model) &&
        xrtValueArrayAppend(Binding, (xvalue*)Provider) &&
        (!Transport || xrtValueArrayAppend(Binding, (xvalue*)Transport));
    if (Ok) Json = xrtJsonStringify(Binding, false, &Size);
    Ok = Json && MdoModelVerificationHash(Json, Size, Hash);
    xrtFree(Json); xrtValueRelease(Binding); return Ok;
}

bool MdoApiModelVerificationSnapshot(cstr Id, char Hash[65])
{
    MdoConfigSnapshot Before = {0}, After = {0}; Before.Size = After.Size = sizeof(Before);
    size_t Size = 0u; char* Json = NULL; xvalue* Root = NULL; bool Ok = false;
    if (MdoConfigGetSnapshot(&Before)) Json = MdoConfigEffectiveJson(&Size);
    if (Json) Root = xrtJsonParse(xrtStrViewN(Json, Size));
    const xvalue* Settings = xrtValueObjectGet(Root, XRT_STR_LITERAL("settings"));
    Ok = Root && MdoModelVerificationKey(xrtValueObjectGet(Root, XRT_STR_LITERAL("models")),
        xrtValueObjectGet(Settings, XRT_STR_LITERAL("transport")), Id, Hash) &&
        MdoConfigGetSnapshot(&After) && Before.Revision == After.Revision;
    xrtFree(Json); xrtValueRelease(Root); return Ok;
}

static bool MdoModelVerificationPath(cstr Id, char Path[112])
{
    char Hash[65];
    if (!MdoModelVerificationHash(Id, strlen(Id), Hash)) return false;
    snprintf(Path, 112u, "data/model-verification/%s.json", Hash); return true;
}

static bool MdoModelVerificationPassed(const xvalue* Record, cstr Phase)
{
    xstrview State = {0};
    return xrtValueGetString(xrtValueObjectGet(xrtValueObjectGet(Record, xrtStrView(Phase)),
        XRT_STR_LITERAL("state")), &State) && State.Size == 6u && !memcmp(State.Data, "passed", 6u);
}

static bool MdoModelVerificationResult(xvalue* Record, bool NeedsTools)
{
    return MdoApiValueSetBool(Record, "verified", MdoModelVerificationPassed(Record, "reply") &&
        (!NeedsTools || MdoModelVerificationPassed(Record, "tools")));
}

static xvalue* MdoModelVerificationRead(cstr Id, cstr Hash)
{
    char Path[112], Bytes[4097]; xfileinfo Info; bool Exists = false; xfile File = NULL; xvalue* Record = NULL;
    if (!MdoModelVerificationPath(Id, Path) || !MdoHomeExternalStat(Path, &Exists, &Info) || !Exists ||
        Info.Type != XFILE_TYPE_FILE || !(Info.Available & XFILE_INFO_SIZE) || Info.Size > 4096u) goto done;
    File = MdoHomeOpenRead(Path);
    if (!File || !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) goto done;
    Record = xrtJsonParse(xrtStrViewN(Bytes, (size_t)Info.Size));
    xstrview Fingerprint = {0}; uint64 Schema = 0u; int64 SignedSchema = 0;
    const xvalue* Version = xrtValueObjectGet(Record, XRT_STR_LITERAL("schema_version"));
    bool VersionOk = (xrtValueGetUInt(Version, &Schema) && Schema == 1u) ||
        (xrtValueGetInt(Version, &SignedSchema) && SignedSchema == 1);
    if (!Record || !VersionOk ||
        !xrtValueGetString(xrtValueObjectGet(Record, XRT_STR_LITERAL("fingerprint")), &Fingerprint) ||
        Fingerprint.Size != 64u || memcmp(Fingerprint.Data, Hash, 64u)) {
        xrtValueRelease(Record); Record = NULL;
    }
done:
    if (File) xrtClose(File);
    xrtClearError(); return Record;
}

xvalue* MdoApiModelVerificationUpdate(cstr Id, cstr Hash, bool Tools,
    bool NeedsTools, cstr State, uint64 Latency, cstr Code)
{
    char Path[112]; size_t Size = 0u; char* Json = NULL;
    xvalue* Record = MdoModelVerificationRead(Id, Hash); xvalue* Phase = xrtValueObject();
    if (!Record) Record = xrtValueObject();
    bool Ok = Record && Phase && MdoApiValueSetUInt(Record, "schema_version", 1u) &&
        MdoApiValueSetString(Record, "fingerprint", Hash) && MdoApiValueSetString(Phase, "state", State) &&
        MdoApiValueSetUInt(Phase, "latency_ms", Latency) && MdoApiValueSetString(Phase, "code", Code ? Code : "") &&
        MdoApiValueSetTake(Record, Tools ? "tools" : "reply", &Phase) && MdoModelVerificationResult(Record, NeedsTools);
    if (Ok) Json = xrtJsonStringify(Record, false, &Size);
    Ok = Json && MdoModelVerificationPath(Id, Path) && MdoHomeAtomicWrite(Path, Json, Size, false);
    xrtFree(Json); xrtValueRelease(Phase);
    if (!Ok) { xrtValueRelease(Record); Record = NULL; }
    xrtClearError(); return Record;
}

bool MdoApiModelVerificationAttach(xvalue* Models, const xvalue* Transport)
{
    xvalue* Results = xrtValueObject(); const xvalue* Items = xrtValueObjectGet(Models, XRT_STR_LITERAL("items"));
    bool Ok = Results != NULL;
    for (size_t i = 0u; Ok && i < xrtValueCount(Items); ++i) {
        const xvalue* Model = xrtValueArrayGet(Items, i); xstrview Id = {0}; char Name[129], Hash[65];
        if (!xrtValueGetString(xrtValueObjectGet(Model, XRT_STR_LITERAL("id")), &Id) || Id.Size >= sizeof(Name)) continue;
        memcpy(Name, Id.Data, Id.Size); Name[Id.Size] = 0;
        if (!MdoModelVerificationKey(Models, Transport, Name, Hash)) continue;
        xvalue* Record = MdoModelVerificationRead(Name, Hash); if (!Record) continue;
        const xvalue* Caps = xrtValueObjectGet(Model, XRT_STR_LITERAL("capabilities")); bool NeedsTools = false;
        for (size_t c = 0u; c < xrtValueCount(Caps); ++c) {
            xstrview Cap = {0}; xrtValueGetString(xrtValueArrayGet(Caps, c), &Cap);
            if (Cap.Size == 16u && !memcmp(Cap.Data, "tool-call-output", 16u)) NeedsTools = true;
        }
        Ok = MdoModelVerificationResult(Record, NeedsTools) && MdoApiValueSetTake(Results, Name, &Record);
        xrtValueRelease(Record);
    }
    Ok = Ok && MdoApiValueSetTake(Models, "verification", &Results);
    xrtValueRelease(Results); return Ok;
}
