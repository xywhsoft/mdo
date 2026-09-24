#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/config.h"
#include "internal.h"

#define MDO_MIGRATION_MODEL_LIMIT 64u
#define MDO_MIGRATION_PROJECT_LIMIT 1024u

static bool MdoMigrationObjectTake(xvalue* Object, const char* Key,
    xvalue* Value)
{
    bool Ok = Object != NULL && Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), &Value);
    xrtValueRelease(Value);
    return Ok;
}

static bool MdoMigrationObjectTakeOwned(xvalue* Object, const char* Key,
    xvalue** Value)
{
    return Object != NULL && Value != NULL && *Value != NULL &&
        xrtValueObjectSetTake(Object, xrtStrView(Key), Value);
}

static bool MdoMigrationObjectString(xvalue* Object, const char* Key,
    const char* Text)
{
    return MdoMigrationObjectTake(Object, Key,
        xrtValueString(xrtStrView(Text != NULL ? Text : "")));
}

static const xvalue* MdoMigrationObjectGet(const xvalue* Object,
    const char* Key)
{
    return Object != NULL && xrtValueType(Object) == XVALUE_OBJECT ?
        xrtValueObjectGet(Object, xrtStrView(Key)) : NULL;
}

static bool MdoMigrationView(const xvalue* Object, const char* Key,
    xstrview* Text)
{
    const xvalue* Value = MdoMigrationObjectGet(Object, Key);
    return Value != NULL && xrtValueType(Value) == XVALUE_STRING &&
        xrtValueGetString(Value, Text) &&
        memchr(Text->Data, '\0', Text->Size) == NULL &&
        xrtUtf8Valid(*Text, NULL);
}

static char* MdoMigrationStringCopy(const xvalue* Object, const char* Key,
    const char* Fallback)
{
    xstrview Text;
    if ( !MdoMigrationView(Object, Key, &Text) )
        return Fallback != NULL ? xrtStrDup(Fallback) : NULL;
    return xrtStrDupN(Text.Data, Text.Size);
}

static bool MdoMigrationBool(const xvalue* Object, const char* Key,
    bool Fallback)
{
    const xvalue* Value = MdoMigrationObjectGet(Object, Key);
    bool Result;
    return Value != NULL && xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, &Result) ? Result : Fallback;
}

static uint64 MdoMigrationUInt(const xvalue* Object, const char* Key,
    uint64 Fallback)
{
    const xvalue* Value = MdoMigrationObjectGet(Object, Key);
    uint64 Unsigned;
    int64 Signed;
    if ( Value != NULL && xrtValueType(Value) == XVALUE_UINT &&
         xrtValueGetUInt(Value, &Unsigned) ) return Unsigned;
    if ( Value != NULL && xrtValueType(Value) == XVALUE_INT &&
         xrtValueGetInt(Value, &Signed) && Signed >= 0 ) return (uint64)Signed;
    return Fallback;
}

static bool MdoMigrationSetStringMapped(xvalue* Object, const char* Key,
    const xvalue* Legacy, const char* LegacyKey,
    const char* const* From, const char* const* To, size_t Count)
{
    xstrview Text;
    size_t i;
    if ( !MdoMigrationView(Legacy, LegacyKey, &Text) ) return true;
    for ( i = 0u; i < Count; ++i ) {
        size_t Size = strlen(From[i]);
        if ( Text.Size == Size && memcmp(Text.Data, From[i], Size) == 0 )
            return MdoMigrationObjectString(Object, Key, To[i]);
    }
    return true;
}

static bool MdoMigrationAppendString(xvalue* Array, const char* Text)
{
    xvalue* Value = xrtValueString(xrtStrView(Text));
    bool Ok = Value != NULL && xrtValueArrayAppendTake(Array, &Value);
    xrtValueRelease(Value);
    return Ok;
}

static char* MdoMigrationEnvelope(xvalue* Patch, size_t* Size)
{
    xvalue* Document = xrtValueObject();
    xvalue* Copy = xrtValueDeepClone(Patch);
    char* Json = NULL;
    if ( Document == NULL || Copy == NULL ||
         !MdoMigrationObjectTake(Document, "schema_version",
            xrtValueUInt(MDO_CONFIG_SCHEMA_VERSION)) ||
         !MdoMigrationObjectTakeOwned(Document, "patch", &Copy) ) goto done;
    Json = xrtJsonStringify(Document, true, Size);
done:
    xrtValueRelease(Copy);
    xrtValueRelease(Document);
    return Json;
}

static bool MdoMigrationValidateAndWriteConfig(MdoMigrationContext* Context,
    MdoConfigDomain Domain, const char* Path, xvalue* Patch,
    xwork_error* Error)
{
    MdoConfigPreview Preview;
    char* Json;
    size_t Size = 0u;
    bool Ok;
    Json = MdoMigrationEnvelope(Patch, &Size);
    if ( Json == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot serialize migrated configuration");
        return false;
    }
    memset(&Preview, 0, sizeof(Preview));
    Preview.Size = sizeof(Preview);
    if ( !MdoConfigPreviewImport(Domain, xrtStrViewN(Json, Size), &Preview) ||
         !Preview.Valid ) {
        xrtFree(Json);
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "migrated configuration failed current schema validation");
        return false;
    }
    Ok = MdoMigrationStageWrite(Context, Path, Json, Size, 0600u, Error);
    xrtFree(Json);
    return Ok;
}

static MdoModelProtocol MdoMigrationProtocol(const char* Dialect)
{
    if ( Dialect != NULL && strcmp(Dialect, "responses") == 0 )
        return MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    if ( Dialect != NULL && strcmp(Dialect, "anthropic") == 0 )
        return MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;
    return MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
}

static const char* MdoMigrationProtocolText(MdoModelProtocol Protocol)
{
    const char* Text = MdoModelProtocolName(Protocol);
    return Text != NULL ? Text : "openai-chat-completions";
}

static const char* MdoMigrationEndpointKey(MdoModelProtocol Protocol)
{
    if ( Protocol == MDO_MODEL_PROTOCOL_OPENAI_RESPONSES ) return "responses";
    if ( Protocol == MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES )
        return "anthropic_messages";
    return "chat_completions";
}

static bool MdoMigrationEffortValid(const char* Effort)
{
    return Effort != NULL &&
        (strcmp(Effort, "none") == 0 || strcmp(Effort, "low") == 0 ||
         strcmp(Effort, "medium") == 0 || strcmp(Effort, "high") == 0);
}

static bool MdoMigrationModelIdUsed(MdoMigrationContext* Context,
    const char* NewId)
{
    size_t i;
    if ( strcmp(NewId, "ling-3.0-tiny") == 0 ) return true;
    for ( i = 0u; i < Context->ModelCount; ++i )
        if ( strcmp(Context->Models[i].NewId, NewId) == 0 ) return true;
    return false;
}

static bool MdoMigrationModelMapAdd(MdoMigrationContext* Context,
    const char* OldId, const char* NewId, MdoModelProtocol Protocol,
    uint64 ContextTokens, uint32 MaxOutputTokens, const char* Effort,
    xwork_error* Error)
{
    MdoMigrationModelMap* Models;
    MdoMigrationModelMap* Map;
    if ( Context->ModelCount >= MDO_MIGRATION_MODEL_LIMIT ||
         MdoMigrationModelFind(Context, OldId) != NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "legacy model identifiers are duplicate or exceed migration limits");
        return false;
    }
    Models = (MdoMigrationModelMap*)xrtRealloc(Context->Models,
        (Context->ModelCount + 1u) * sizeof(*Models));
    if ( Models == NULL ) {
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot grow migrated model map");
        return false;
    }
    Context->Models = Models;
    Map = &Models[Context->ModelCount];
    memset(Map, 0, sizeof(*Map));
    Map->OldId = xrtStrDup(OldId);
    Map->NewId = xrtStrDup(NewId);
    if ( Map->OldId == NULL || Map->NewId == NULL ) {
        xrtFree(Map->OldId);
        xrtFree(Map->NewId);
        memset(Map, 0, sizeof(*Map));
        MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot copy migrated model identity");
        return false;
    }
    Map->Protocol = Protocol;
    Map->ContextTokens = ContextTokens;
    Map->MaxOutputTokens = MaxOutputTokens;
    snprintf(Map->ReasoningEffort, sizeof(Map->ReasoningEffort), "%s",
        MdoMigrationEffortValid(Effort) ? Effort : "medium");
    ++Context->ModelCount;
    return true;
}

static bool MdoMigrationAddCustomModel(MdoMigrationContext* Context,
    xvalue* Providers, xvalue* Items, const xvalue* Legacy,
    xwork_error* Error)
{
    static const char* const Capabilities[] = {
        "text-input", "tool-result-input", "text-output", "json-output",
        "tool-call-output", "reasoning-output", "streaming",
        "reasoning-control", "parallel-tool-calls"
    };
    char* OldId = MdoMigrationStringCopy(Legacy, "id", NULL);
    char* Name = NULL;
    char* Endpoint = NULL;
    char* Wire = NULL;
    char* Dialect = NULL;
    char* Effort = NULL;
    char* ApiKey = NULL;
    char NewId[129];
    char ProviderId[129];
    char Hashed[4097];
    char SecretPath[256];
    char SecretReference[280];
    MdoModelProtocol Protocol;
    uint64 ContextTokens;
    uint64 MaxOutput64;
    uint32 MaxOutput;
    xvalue* Provider = NULL;
    xvalue* Endpoints = NULL;
    xvalue* Credential = NULL;
    xvalue* Model = NULL;
    xvalue* Protocols = NULL;
    xvalue* Caps = NULL;
    xvalue* Window = NULL;
    xvalue* Efforts = NULL;
    xvalue* Attachments = NULL;
    size_t i;
    bool Ok = false;
    if ( OldId == NULL ) goto invalid;
    if ( strcmp(OldId, "ling-gpu") == 0 ||
         strcmp(OldId, "ling-3.0-tiny") == 0 ) {
        Ok = MdoMigrationModelMapAdd(Context, OldId, "ling-3.0-tiny",
            MDO_MODEL_PROTOCOL_OPENAI_RESPONSES, 131072u, 16384u,
            "medium", Error);
        goto done;
    }
    Endpoint = MdoMigrationStringCopy(Legacy, "baseUrl", "");
    if ( Endpoint == NULL ) goto memory;
    if ( Endpoint[0] == '\0' ) {
        ++Context->Result->SkippedItems;
        Ok = MdoMigrationModelMapAdd(Context, OldId, "ling-3.0-tiny",
            MDO_MODEL_PROTOCOL_OPENAI_RESPONSES, 131072u, 16384u,
            "medium", Error);
        goto done;
    }
    if ( !MdoMigrationIdentifier(OldId, "model", NewId, sizeof(NewId)) )
        goto invalid;
    if ( MdoMigrationModelIdUsed(Context, NewId) ) {
        if ( snprintf(Hashed, sizeof(Hashed), "legacy:%s", OldId) <= 0 ||
             !MdoMigrationIdentifier(Hashed, "model", NewId,
                sizeof(NewId)) || MdoMigrationModelIdUsed(Context, NewId) )
            goto invalid;
    }
    if ( snprintf(Hashed, sizeof(Hashed), "provider:%s", OldId) <= 0 ||
         !MdoMigrationIdentifier(Hashed, "provider", ProviderId,
            sizeof(ProviderId)) ) goto invalid;
    Name = MdoMigrationStringCopy(Legacy, "name", NewId);
    Wire = MdoMigrationStringCopy(Legacy, "model", NewId);
    Dialect = MdoMigrationStringCopy(Legacy, "dialect", "openai");
    Effort = MdoMigrationStringCopy(Legacy, "reasoning", "medium");
    ApiKey = MdoMigrationStringCopy(Legacy, "apiKey", "");
    if ( Name == NULL || Wire == NULL || Dialect == NULL || Effort == NULL ||
         ApiKey == NULL ) goto memory;
    Protocol = MdoMigrationProtocol(Dialect);
    ContextTokens = MdoMigrationUInt(Legacy, "contextWindow", 131072u);
    if ( ContextTokens < 2u ) ContextTokens = 2u;
    MaxOutput64 = MdoMigrationUInt(Legacy, "maxOutput",
        ContextTokens >= 8u ? ContextTokens / 8u : 1u);
    if ( MaxOutput64 == 0u ) MaxOutput64 = 1u;
    if ( MaxOutput64 > ContextTokens ) MaxOutput64 = ContextTokens;
    if ( MaxOutput64 > UINT32_MAX ) MaxOutput64 = UINT32_MAX;
    MaxOutput = (uint32)MaxOutput64;
    Provider = xrtValueObject();
    Endpoints = xrtValueObject();
    Model = xrtValueObject();
    Protocols = xrtValueArray();
    Caps = xrtValueArray();
    Window = xrtValueObject();
    Efforts = xrtValueArray();
    Attachments = xrtValueArray();
    if ( Provider == NULL || Endpoints == NULL || Model == NULL ||
         Protocols == NULL || Caps == NULL || Window == NULL ||
         Efforts == NULL || Attachments == NULL ) goto memory;
    if ( !MdoMigrationObjectString(Endpoints,
            MdoMigrationEndpointKey(Protocol), Endpoint) ||
         !MdoMigrationObjectString(Provider, "id", ProviderId) ||
         !MdoMigrationObjectString(Provider, "name", Name) ||
         !MdoMigrationObjectTake(Provider, "builtin", xrtValueBool(false)) ||
         !MdoMigrationObjectTake(Provider, "editable", xrtValueBool(true)) ||
         !MdoMigrationObjectTake(Provider, "removable", xrtValueBool(true)) ||
         !MdoMigrationObjectTake(Provider, "verify_peer", xrtValueBool(true)) ||
         !MdoMigrationObjectTake(Provider, "timeout_ms", xrtValueUInt(120000u)) ||
         !MdoMigrationObjectTakeOwned(Provider, "endpoints", &Endpoints) )
        goto memory;
    if ( ApiKey[0] != '\0' ) {
        Credential = xrtValueObject();
        if ( Credential == NULL ||
             snprintf(SecretPath, sizeof(SecretPath),
                "secrets/legacy-%s.key", ProviderId) <= 0 ||
             snprintf(SecretReference, sizeof(SecretReference),
                "file:%s", SecretPath) <= 0 ||
             !MdoMigrationStageWrite(Context, SecretPath, ApiKey,
                strlen(ApiKey), 0600u, Error) ||
             !MdoMigrationObjectString(Credential, "secret_ref",
                SecretReference) ||
             !MdoMigrationObjectTakeOwned(Provider, "credential",
                &Credential) )
            goto done;
    }
    if ( !xrtValueArrayAppendTake(Providers, &Provider) ||
         !MdoMigrationAppendString(Protocols,
            MdoMigrationProtocolText(Protocol)) ) goto memory;
    for ( i = 0u; i < sizeof(Capabilities) / sizeof(Capabilities[0]); ++i )
        if ( !MdoMigrationAppendString(Caps, Capabilities[i]) ) goto memory;
    for ( i = 0u; i < 4u; ++i ) {
        static const char* const Values[] = { "none", "low", "medium", "high" };
        if ( !MdoMigrationAppendString(Efforts, Values[i]) ) goto memory;
    }
    if ( !MdoMigrationObjectString(Window, "mode", "shared-context") ||
         !MdoMigrationObjectTake(Window, "context_tokens",
            xrtValueUInt(ContextTokens)) ||
         !MdoMigrationObjectTake(Window, "max_input_tokens",
            xrtValueUInt(ContextTokens - 1u)) ||
         !MdoMigrationObjectTake(Window, "max_output_tokens",
            xrtValueUInt(MaxOutput)) ||
         !MdoMigrationObjectTake(Window, "output_reserve_tokens",
            xrtValueUInt(MaxOutput / 2u)) ||
         !MdoMigrationObjectTake(Window, "summary_tokens",
            xrtValueUInt(MaxOutput / 4u)) ||
         !MdoMigrationObjectString(Model, "id", NewId) ||
         !MdoMigrationObjectString(Model, "name", Name) ||
         !MdoMigrationObjectString(Model, "provider", ProviderId) ||
         !MdoMigrationObjectString(Model, "wire_model", Wire) ||
         !MdoMigrationObjectTake(Model, "builtin", xrtValueBool(false)) ||
         !MdoMigrationObjectTake(Model, "free", xrtValueBool(false)) ||
         !MdoMigrationObjectTake(Model, "editable", xrtValueBool(true)) ||
         !MdoMigrationObjectTake(Model, "removable", xrtValueBool(true)) ||
         !MdoMigrationObjectTakeOwned(Model, "protocols", &Protocols) ||
         !MdoMigrationObjectString(Model, "default_protocol",
            MdoMigrationProtocolText(Protocol)) ||
         !MdoMigrationObjectTakeOwned(Model, "capabilities", &Caps) ||
         !MdoMigrationObjectTakeOwned(Model, "window", &Window) ||
         !MdoMigrationObjectTakeOwned(Model, "reasoning_efforts",
            &Efforts) ||
         !MdoMigrationObjectString(Model, "default_reasoning_effort",
            MdoMigrationEffortValid(Effort) ? Effort : "medium") ||
         !MdoMigrationObjectTakeOwned(Model, "attachments", &Attachments) ||
         !xrtValueArrayAppendTake(Items, &Model) ||
         !MdoMigrationModelMapAdd(Context, OldId, NewId, Protocol,
            ContextTokens, MaxOutput,
            MdoMigrationEffortValid(Effort) ? Effort : "medium", Error) )
        goto memory;
    ++Context->Result->ImportedModels;
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy model cannot be mapped to a stable identifier");
    goto done;
memory:
    MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot build migrated model configuration");
done:
    xrtValueRelease(Provider);
    xrtValueRelease(Endpoints);
    xrtValueRelease(Credential);
    xrtValueRelease(Model);
    xrtValueRelease(Protocols);
    xrtValueRelease(Caps);
    xrtValueRelease(Window);
    xrtValueRelease(Efforts);
    xrtValueRelease(Attachments);
    if ( ApiKey != NULL ) xrtSecureZero(ApiKey, strlen(ApiKey));
    xrtFree(ApiKey);
    xrtFree(Effort);
    xrtFree(Dialect);
    xrtFree(Wire);
    xrtFree(Endpoint);
    xrtFree(Name);
    xrtFree(OldId);
    return Ok;
}

bool MdoMigrationConvertConfig(MdoMigrationContext* Context,
    xwork_error* Error)
{
    static const char* const ThemeFrom[] = { "auto", "light", "dark" };
    static const char* const ThemeTo[] = { "system", "light", "dark" };
    static const char* const FontFrom[] = { "sm", "md", "lg" };
    static const char* const FontTo[] = { "small", "normal", "large" };
    static const char* const ModeFrom[] = { "queue", "guide" };
    static const char* const ModeTo[] = { "queue", "guide" };
    static const char* const LangFrom[] = { "zh", "en", "ru" };
    static const char* const LangTo[] = { "zh-CN", "en-US", "ru-RU" };
    MdoMigrationFile* File = MdoMigrationFind(&Context->Scan, "config.json");
    xstrview DefaultsText;
    xvalue* Defaults = NULL;
    xvalue* Legacy = NULL;
    xvalue* Settings = NULL;
    xvalue* Models = NULL;
    xvalue* Appearance;
    xvalue* Composer;
    xvalue* Notifications;
    xvalue* Agent;
    xvalue* Providers;
    xvalue* Items;
    const xvalue* LegacySettings;
    const xvalue* LegacyModels;
    char* DefaultModel = NULL;
    char* SystemPrompt = NULL;
    char* ProxyPassword = NULL;
    size_t i;
    bool Ok = false;
    if ( File == NULL || !MdoBuiltinDefaults(&DefaultsText) ) goto invalid;
    Defaults = xrtJsonParse(DefaultsText);
    Legacy = MdoMigrationParseJson(Context->SourceRoot, File, Error);
    if ( Defaults == NULL || Legacy == NULL ) goto done;
    Settings = xrtValueDeepClone(MdoMigrationObjectGet(Defaults, "settings"));
    Models = xrtValueDeepClone(MdoMigrationObjectGet(Defaults, "models"));
    if ( Settings == NULL || Models == NULL ) goto memory;
    LegacySettings = MdoMigrationObjectGet(Legacy, "settings");
    Appearance = xrtValueObjectGet(Settings, xrtStrView("appearance"));
    Composer = xrtValueObjectGet(Settings, xrtStrView("composer"));
    Notifications = xrtValueObjectGet(Settings,
        xrtStrView("notifications"));
    Agent = xrtValueObjectGet(Settings, xrtStrView("agent"));
    if ( xrtValueType(Appearance) != XVALUE_OBJECT ||
         xrtValueType(Composer) != XVALUE_OBJECT ||
         xrtValueType(Notifications) != XVALUE_OBJECT ||
         xrtValueType(Agent) != XVALUE_OBJECT ) goto invalid;
    if ( LegacySettings != NULL ) {
        if ( !MdoMigrationSetStringMapped(Appearance, "theme", LegacySettings,
                "theme", ThemeFrom, ThemeTo, 3u) ||
             !MdoMigrationSetStringMapped(Appearance, "font_size",
                LegacySettings, "fontSize", FontFrom, FontTo, 3u) ||
             !MdoMigrationSetStringMapped(Composer, "submit_mode",
                LegacySettings, "interactMode", ModeFrom, ModeTo, 2u) ||
             !MdoMigrationSetStringMapped(Settings, "locale", LegacySettings,
                "lang", LangFrom, LangTo, 3u) ||
             !MdoMigrationObjectTake(Notifications, "sound", xrtValueBool(
                MdoMigrationBool(LegacySettings, "sound", false))) ||
             !MdoMigrationObjectTake(Agent, "web_search", xrtValueBool(
                MdoMigrationBool(LegacySettings, "webSearchEnabled", true))) ||
             !MdoMigrationObjectTake(Agent, "memory", xrtValueBool(
                MdoMigrationBool(LegacySettings, "memoryEnabled", true))) ||
             !MdoMigrationObjectTake(Agent, "schedules", xrtValueBool(
                MdoMigrationBool(LegacySettings, "schedulesEnabled", true))) )
            goto memory;
        SystemPrompt = MdoMigrationStringCopy(LegacySettings,
            "systemPrompt", "");
        ProxyPassword = MdoMigrationStringCopy(LegacySettings,
            "proxyPass", "");
        if ( SystemPrompt == NULL || ProxyPassword == NULL ) goto memory;
        if ( SystemPrompt[0] != '\0' ) {
            Context->LegacySystemPrompt = true;
            ++Context->Result->SkippedItems;
            if ( !MdoMigrationStageWrite(Context,
                    "migration/legacy-system-prompt.txt", SystemPrompt,
                    strlen(SystemPrompt), 0600u, Error) ) goto done;
        }
        if ( MdoMigrationBool(LegacySettings, "proxyEnabled", false) ||
             ProxyPassword[0] != '\0' ||
             MdoMigrationObjectGet(LegacySettings, "caCertPath") != NULL ) {
            Context->LegacyNetworkSettings = true;
            ++Context->Result->SkippedItems;
            if ( ProxyPassword[0] != '\0' &&
                 !MdoMigrationStageWrite(Context,
                    "secrets/legacy-proxy-password.key", ProxyPassword,
                    strlen(ProxyPassword), 0600u, Error) ) goto done;
        }
        if ( MdoMigrationObjectGet(LegacySettings, "winX") != NULL ||
             MdoMigrationObjectGet(LegacySettings, "winY") != NULL ||
             MdoMigrationObjectGet(LegacySettings, "winW") != NULL ||
             MdoMigrationObjectGet(LegacySettings, "winH") != NULL ) {
            Context->LegacyWindowSettings = true;
            ++Context->Result->SkippedItems;
        }
    }
    if ( !MdoMigrationValidateAndWriteConfig(Context, MDO_CONFIG_SETTINGS,
            "config/settings.json", Settings, Error) ) goto done;
    Providers = xrtValueObjectGet(Models, xrtStrView("providers"));
    Items = xrtValueObjectGet(Models, xrtStrView("items"));
    LegacyModels = MdoMigrationObjectGet(Legacy, "models");
    if ( xrtValueType(Providers) != XVALUE_ARRAY ||
         xrtValueType(Items) != XVALUE_ARRAY ||
         xrtValueType(LegacyModels) != XVALUE_ARRAY ) goto invalid;
    for ( i = 0u; i < xrtValueCount(LegacyModels); ++i ) {
        const xvalue* Model = xrtValueArrayGet(LegacyModels, i);
        if ( Model == NULL || xrtValueType(Model) != XVALUE_OBJECT ||
             !MdoMigrationAddCustomModel(Context, Providers, Items, Model,
                Error) ) goto done;
    }
    if ( MdoMigrationModelFind(Context, "ling-gpu") == NULL &&
         !MdoMigrationModelMapAdd(Context, "ling-gpu", "ling-3.0-tiny",
            MDO_MODEL_PROTOCOL_OPENAI_RESPONSES, 131072u, 16384u,
            "medium", Error) ) goto done;
    DefaultModel = MdoMigrationStringCopy(Legacy, "defaultModel", "ling-gpu");
    if ( DefaultModel == NULL ) goto memory;
    {
        MdoMigrationModelMap* Map = MdoMigrationModelFind(Context, DefaultModel);
        if ( Map == NULL ) Map = MdoMigrationModelFind(Context, "ling-gpu");
        if ( Map == NULL || !MdoMigrationObjectString(Models, "default_model",
                Map->NewId) ) goto memory;
    }
    if ( !MdoMigrationValidateAndWriteConfig(Context, MDO_CONFIG_MODELS,
            "config/models.json", Models, Error) ) goto done;
    Ok = true;
    goto done;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy configuration cannot be converted");
    goto done;
memory:
    MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot build migrated configuration");
done:
    if ( ProxyPassword != NULL )
        xrtSecureZero(ProxyPassword, strlen(ProxyPassword));
    xrtFree(ProxyPassword);
    xrtFree(SystemPrompt);
    xrtFree(DefaultModel);
    xrtValueRelease(Models);
    xrtValueRelease(Settings);
    xrtValueRelease(Legacy);
    xrtValueRelease(Defaults);
    return Ok;
}

static bool MdoMigrationProjectIdUsed(MdoMigrationContext* Context,
    const char* Id)
{
    size_t i;
    for ( i = 0u; i < Context->ProjectCount; ++i )
        if ( strcmp(Context->Projects[i].NewId, Id) == 0 ) return true;
    return false;
}

static bool MdoMigrationProjectAdd(MdoMigrationContext* Context,
    const char* OldId, xwork_error* Error)
{
    MdoMigrationProjectMap* Projects;
    MdoMigrationProjectMap* Map;
    char Id[65];
    char Hashed[4097];
    if ( MdoMigrationProjectFind(Context, OldId) != NULL ) return true;
    if ( Context->ProjectCount >= MDO_MIGRATION_PROJECT_LIMIT ) {
        MdoMigrationError(Error, XWORK_ERROR_LIMIT,
            "legacy project count exceeds migration limits");
        return false;
    }
    if ( strcmp(OldId, "_tasks") == 0 ) snprintf(Id, sizeof(Id), "tasks");
    else if ( !MdoMigrationIdentifier(OldId, "project", Id, sizeof(Id)) )
        goto invalid;
    if ( MdoMigrationProjectIdUsed(Context, Id) ) {
        if ( snprintf(Hashed, sizeof(Hashed), "legacy:%s", OldId) <= 0 ||
             !MdoMigrationIdentifier(Hashed, "project", Id, sizeof(Id)) ||
             MdoMigrationProjectIdUsed(Context, Id) ) goto invalid;
    }
    Projects = (MdoMigrationProjectMap*)xrtRealloc(Context->Projects,
        (Context->ProjectCount + 1u) * sizeof(*Projects));
    if ( Projects == NULL ) goto memory;
    Context->Projects = Projects;
    Map = &Projects[Context->ProjectCount];
    memset(Map, 0, sizeof(*Map));
    Map->OldId = xrtStrDup(OldId);
    Map->NewId = xrtStrDup(Id);
    Map->WorkspaceRoot = xrtStrDup(Context->Preview.SourcePath);
    if ( Map->OldId == NULL || Map->NewId == NULL ||
         Map->WorkspaceRoot == NULL ) {
        xrtFree(Map->OldId); xrtFree(Map->NewId);
        xrtFree(Map->WorkspaceRoot);
        memset(Map, 0, sizeof(*Map));
        goto memory;
    }
    ++Context->ProjectCount;
    return true;
invalid:
    MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
        "legacy project cannot be mapped to a stable identifier");
    return false;
memory:
    MdoMigrationError(Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot grow migrated project map");
    return false;
}

static bool MdoMigrationBucket(const char* Path, char* Bucket,
    size_t Capacity)
{
    const char* Start;
    const char* End;
    size_t Size;
    if ( strncmp(Path, "projects/", 9u) != 0 ) return false;
    Start = Path + 9u;
    End = strchr(Start, '/');
    if ( End == NULL || End == Start ) return false;
    Size = (size_t)(End - Start);
    if ( Size >= Capacity ) return false;
    memcpy(Bucket, Start, Size);
    Bucket[Size] = '\0';
    return true;
}

bool MdoMigrationConvertProjects(MdoMigrationContext* Context,
    xwork_error* Error)
{
    size_t i;
    for ( i = 0u; i < Context->Scan.Count; ++i ) {
        char Bucket[257];
        if ( MdoMigrationBucket(Context->Scan.Files[i].Path, Bucket,
                sizeof(Bucket)) &&
             !MdoMigrationProjectAdd(Context, Bucket, Error) ) return false;
    }
    if ( MdoMigrationProjectFind(Context, "_tasks") == NULL &&
         !MdoMigrationProjectAdd(Context, "_tasks", Error) ) return false;
    for ( i = 0u; i < Context->Scan.Count; ++i ) {
        MdoMigrationFile* File = &Context->Scan.Files[i];
        char Bucket[257];
        MdoMigrationProjectMap* Map;
        xvalue* Project;
        char* Workspace;
        if ( File->Kind != MDO_MIGRATION_FILE_PROJECT ||
             !MdoMigrationBucket(File->Path, Bucket, sizeof(Bucket)) ) continue;
        Map = MdoMigrationProjectFind(Context, Bucket);
        Project = MdoMigrationParseJson(Context->SourceRoot, File, Error);
        if ( Map == NULL || Project == NULL ) {
            xrtValueRelease(Project);
            return false;
        }
        Workspace = MdoMigrationStringCopy(Project, "path",
            Context->Preview.SourcePath);
        xrtValueRelease(Project);
        if ( Workspace == NULL || strlen(Workspace) >= 2049u ||
             !xrtUtf8Valid(xrtStrView(Workspace), NULL) ) {
            xrtFree(Workspace);
            MdoMigrationError(Error, XWORK_ERROR_INVALID_ARGUMENT,
                "legacy project workspace is invalid or too long");
            return false;
        }
        xrtFree(Map->WorkspaceRoot);
        Map->WorkspaceRoot = Workspace;
    }
    Context->Result->ImportedProjects = Context->ProjectCount;
    return true;
}
