#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mdo/config.h"
#include "../../include/mdo/models.h"

#define MDO_MODELS_ERROR_DOMAIN "mdo.models"
#define MDO_MODELS_MAX_CONFIG_BYTES (1024u * 1024u)
#define MDO_MODELS_MAX_PROVIDERS 128u
#define MDO_MODELS_MAX_MODELS 512u
#define MDO_MODELS_MAX_REASONING_EFFORTS 7u

typedef struct MdoProviderEntry {
    char* Id;
    char* Name;
    char* Endpoints[3];
    char* CredentialReference;
    uint32 TimeoutMilliseconds;
    MdoModelProtocolFlags Protocols;
    bool Builtin;
    bool Editable;
    bool Removable;
    bool VerifyPeer;
} MdoProviderEntry;

typedef struct MdoModelEntry {
    char* Id;
    char* Name;
    char* ProviderId;
    char* WireModel;
    char** ReasoningEfforts;
    size_t ReasoningEffortCount;
    char* DefaultReasoningEffort;
    MdoModelProtocolFlags Protocols;
    MdoModelProtocol DefaultProtocol;
    xllm_capability_flags Capabilities;
    xllm_window_mode WindowMode;
    uint64 ContextWindowTokens;
    uint64 MaxInputTokens;
    uint32 MaxOutputTokens;
    uint32 OutputReserveTokens;
    uint32 SummaryTokens;
    MdoModelAttachmentFlags Attachments;
    bool Builtin;
    bool Free;
    bool Editable;
    bool Removable;
} MdoModelEntry;

struct MdoModelCatalog {
    xatomic32 Refs;
    uint64 Generation;
    MdoProviderEntry* Providers;
    size_t ProviderCount;
    MdoModelEntry* Models;
    size_t ModelCount;
    size_t DefaultModelIndex;
};

typedef struct MdoModelState {
    xmutex* Lock;
    xmutex* ReloadLock;
    MdoModelCatalog* Catalog;
    uint64 NextGeneration;
    bool Initialized;
} MdoModelState;

typedef struct MdoCapabilityName {
    const char* Name;
    xllm_capability_flags Flag;
} MdoCapabilityName;

static MdoModelState g_MdoModels;

static const MdoCapabilityName g_MdoCapabilities[] = {
    { "text-input", XLLM_CAP_TEXT_IN },
    { "tool-result-input", XLLM_CAP_TOOL_RESULT_IN },
    { "text-output", XLLM_CAP_TEXT_OUT },
    { "json-output", XLLM_CAP_JSON_OUT },
    { "tool-call-output", XLLM_CAP_TOOL_CALL_OUT },
    { "reasoning-output", XLLM_CAP_REASONING_OUT },
    { "streaming", XLLM_CAP_STREAM },
    { "reasoning-control", XLLM_CAP_REASONING_CONTROL },
    { "parallel-tool-calls", XLLM_CAP_PARALLEL_TOOL_CALL },
    { "max-completion-tokens", XLLM_CAP_MAX_COMPLETION_TOKENS },
    { "developer-role", XLLM_CAP_DEVELOPER_ROLE },
    { "media-input", XLLM_CAP_IMAGE_IN }
};

static void MdoModelsSetError(xerrkind Kind, cstr Message)
{
    xerror* pError = xrtErrorCreate(Kind, MDO_MODELS_ERROR_DOMAIN, 1,
        Message != NULL ? Message : "model catalog operation failed");
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static xstrview MdoModelsKey(cstr Text)
{
    return xrtStrView(Text);
}

static bool MdoModelsViewEqual(xstrview View, cstr Text)
{
    size_t Length = strlen(Text);
    return View.Size == Length && memcmp(View.Data, Text, Length) == 0;
}

static bool MdoModelsString(const xvalue* pValue, xstrview* pView)
{
    return pValue != NULL && xrtValueType(pValue) == XVALUE_STRING &&
        xrtValueGetString(pValue, pView);
}

static bool MdoModelsUnsigned(const xvalue* pValue, uint64* pResult)
{
    int64 Signed;
    if ( pValue == NULL ) return false;
    if ( xrtValueType(pValue) == XVALUE_UINT )
        return xrtValueGetUInt(pValue, pResult);
    if ( xrtValueType(pValue) != XVALUE_INT ||
         !xrtValueGetInt(pValue, &Signed) || Signed < 0 ) return false;
    *pResult = (uint64)Signed;
    return true;
}

static bool MdoModelsBool(const xvalue* pObject, cstr Name, bool* pValue)
{
    const xvalue* pItem = xrtValueObjectGet(pObject, MdoModelsKey(Name));
    return pItem != NULL && xrtValueType(pItem) == XVALUE_BOOL &&
        xrtValueGetBool(pItem, pValue);
}

static bool MdoModelsCopyString(const xvalue* pObject, cstr Name,
    char** ppValue, bool Required)
{
    xstrview View;
    const xvalue* pValue = xrtValueObjectGet(pObject, MdoModelsKey(Name));
    *ppValue = NULL;
    if ( pValue == NULL ) return !Required;
    if ( !MdoModelsString(pValue, &View) || View.Size == 0u ) return false;
    *ppValue = xrtStrDupN(View.Data, View.Size);
    return *ppValue != NULL;
}

const char* MdoModelProtocolName(MdoModelProtocol Protocol)
{
    switch ( Protocol ) {
    case MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS:
        return "openai-chat-completions";
    case MDO_MODEL_PROTOCOL_OPENAI_RESPONSES:
        return "openai-responses";
    case MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES:
        return "anthropic-messages";
    default:
        return NULL;
    }
}

xllm_provider MdoModelProtocolProvider(MdoModelProtocol Protocol)
{
    switch ( Protocol ) {
    case MDO_MODEL_PROTOCOL_OPENAI_RESPONSES:
        return XLLM_PROVIDER_OPENAI_RESPONSES;
    case MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES:
        return XLLM_PROVIDER_ANTHROPIC;
    case MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS:
    default:
        return XLLM_PROVIDER_OPENAI_COMPAT;
    }
}

static bool MdoModelsProtocol(xstrview View, MdoModelProtocol* pProtocol,
    MdoModelProtocolFlags* pFlag, size_t* pEndpointIndex)
{
    if ( MdoModelsViewEqual(View, "openai-chat-completions") ) {
        if ( pProtocol != NULL )
            *pProtocol = MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
        if ( pFlag != NULL ) *pFlag = MDO_MODEL_PROTOCOL_FLAG_CHAT_COMPLETIONS;
        if ( pEndpointIndex != NULL ) *pEndpointIndex = 0u;
        return true;
    }
    if ( MdoModelsViewEqual(View, "openai-responses") ) {
        if ( pProtocol != NULL )
            *pProtocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
        if ( pFlag != NULL ) *pFlag = MDO_MODEL_PROTOCOL_FLAG_RESPONSES;
        if ( pEndpointIndex != NULL ) *pEndpointIndex = 1u;
        return true;
    }
    if ( MdoModelsViewEqual(View, "anthropic-messages") ) {
        if ( pProtocol != NULL )
            *pProtocol = MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;
        if ( pFlag != NULL ) *pFlag = MDO_MODEL_PROTOCOL_FLAG_ANTHROPIC;
        if ( pEndpointIndex != NULL ) *pEndpointIndex = 2u;
        return true;
    }
    return false;
}

static MdoModelProtocolFlags MdoModelsProtocolFlag(MdoModelProtocol Protocol)
{
    switch ( Protocol ) {
    case MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS:
        return MDO_MODEL_PROTOCOL_FLAG_CHAT_COMPLETIONS;
    case MDO_MODEL_PROTOCOL_OPENAI_RESPONSES:
        return MDO_MODEL_PROTOCOL_FLAG_RESPONSES;
    case MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES:
        return MDO_MODEL_PROTOCOL_FLAG_ANTHROPIC;
    default:
        return 0u;
    }
}

static void MdoModelsProviderUnit(MdoProviderEntry* pProvider)
{
    size_t i;
    if ( pProvider == NULL ) return;
    xrtFree(pProvider->Id);
    xrtFree(pProvider->Name);
    for ( i = 0u; i < 3u; ++i ) xrtFree(pProvider->Endpoints[i]);
    xrtFree(pProvider->CredentialReference);
    memset(pProvider, 0, sizeof(*pProvider));
}

static void MdoModelsModelUnit(MdoModelEntry* pModel)
{
    size_t i;
    if ( pModel == NULL ) return;
    xrtFree(pModel->Id);
    xrtFree(pModel->Name);
    xrtFree(pModel->ProviderId);
    xrtFree(pModel->WireModel);
    for ( i = 0u; i < pModel->ReasoningEffortCount; ++i )
        xrtFree(pModel->ReasoningEfforts[i]);
    xrtFree(pModel->ReasoningEfforts);
    xrtFree(pModel->DefaultReasoningEffort);
    memset(pModel, 0, sizeof(*pModel));
}

static MdoModelCatalog* MdoModelsCatalogCreate(uint64 Generation)
{
    MdoModelCatalog* pCatalog = (MdoModelCatalog*)xrtCalloc(1u,
        sizeof(*pCatalog));
    if ( pCatalog == NULL ) return NULL;
    xrtAtomic32Init(&pCatalog->Refs, 1u);
    pCatalog->Generation = Generation;
    pCatalog->DefaultModelIndex = SIZE_MAX;
    return pCatalog;
}

MdoModelCatalog* MdoModelCatalogRef(MdoModelCatalog* pCatalog)
{
    uint32 Refs;
    if ( pCatalog == NULL ) return NULL;
    Refs = xrtAtomic32Load(&pCatalog->Refs, XMEMORY_ACQUIRE);
    for ( ; ; ) {
        uint32 Expected = Refs;
        if ( Refs == 0u || Refs == UINT32_MAX ) return NULL;
        if ( xrtAtomic32CompareExchange(&pCatalog->Refs, &Expected,
                Refs + 1u, XMEMORY_ACQ_REL, XMEMORY_ACQUIRE) )
            return pCatalog;
        Refs = Expected;
    }
}

void MdoModelCatalogRelease(MdoModelCatalog* pCatalog)
{
    uint32 Previous;
    size_t i;
    if ( pCatalog == NULL ) return;
    Previous = xrtAtomic32FetchSub(&pCatalog->Refs, 1u, XMEMORY_ACQ_REL);
    if ( Previous > 1u ) return;
    if ( Previous == 0u ) abort();
    for ( i = 0u; i < pCatalog->ProviderCount; ++i )
        MdoModelsProviderUnit(&pCatalog->Providers[i]);
    for ( i = 0u; i < pCatalog->ModelCount; ++i )
        MdoModelsModelUnit(&pCatalog->Models[i]);
    xrtFree(pCatalog->Providers);
    xrtFree(pCatalog->Models);
    xrtFree(pCatalog);
}

static MdoProviderEntry* MdoModelsFindProvider(MdoModelCatalog* pCatalog,
    cstr ProviderId)
{
    size_t i;
    for ( i = 0u; i < pCatalog->ProviderCount; ++i )
        if ( strcmp(pCatalog->Providers[i].Id, ProviderId) == 0 )
            return &pCatalog->Providers[i];
    return NULL;
}

static MdoModelEntry* MdoModelsFindModel(MdoModelCatalog* pCatalog,
    cstr ModelId)
{
    size_t i;
    for ( i = 0u; i < pCatalog->ModelCount; ++i )
        if ( strcmp(pCatalog->Models[i].Id, ModelId) == 0 )
            return &pCatalog->Models[i];
    return NULL;
}

static bool MdoModelsParseProvider(const xvalue* pValue,
    MdoProviderEntry* pProvider)
{
    static const char* const EndpointNames[] = {
        "chat_completions", "responses", "anthropic_messages"
    };
    const xvalue* pEndpoints;
    const xvalue* pCredential;
    uint64 Timeout;
    size_t i;
    if ( !MdoModelsCopyString(pValue, "id", &pProvider->Id, true) ||
         !MdoModelsCopyString(pValue, "name", &pProvider->Name, true) ||
         !MdoModelsBool(pValue, "builtin", &pProvider->Builtin) ||
         !MdoModelsBool(pValue, "editable", &pProvider->Editable) ||
         !MdoModelsBool(pValue, "removable", &pProvider->Removable) ||
         !MdoModelsBool(pValue, "verify_peer", &pProvider->VerifyPeer) ||
         !MdoModelsUnsigned(xrtValueObjectGet(pValue,
            MdoModelsKey("timeout_ms")), &Timeout) ||
         Timeout == 0u || Timeout > UINT32_MAX ) return false;
    pProvider->TimeoutMilliseconds = (uint32)Timeout;
    pEndpoints = xrtValueObjectGet(pValue, MdoModelsKey("endpoints"));
    if ( xrtValueType(pEndpoints) != XVALUE_OBJECT ) return false;
    for ( i = 0u; i < 3u; ++i ) {
        const xvalue* pEndpoint = xrtValueObjectGet(pEndpoints,
            MdoModelsKey(EndpointNames[i]));
        xstrview View;
        if ( pEndpoint == NULL ) continue;
        if ( !MdoModelsString(pEndpoint, &View) || View.Size == 0u )
            return false;
        pProvider->Endpoints[i] = xrtStrDupN(View.Data, View.Size);
        if ( pProvider->Endpoints[i] == NULL ) return false;
        pProvider->Protocols |= (MdoModelProtocolFlags)(1u << i);
    }
    pCredential = xrtValueObjectGet(pValue, MdoModelsKey("credential"));
    if ( pCredential != NULL &&
         !MdoModelsCopyString(pCredential, "secret_ref",
            &pProvider->CredentialReference, true) ) return false;
    return pProvider->Protocols != 0u;
}

static bool MdoModelsParseCapabilities(const xvalue* pArray,
    xllm_capability_flags* pCapabilities)
{
    xllm_capability_flags Flags = 0u;
    size_t i;
    size_t j;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ) return false;
    for ( i = 0u; i < xrtValueCount(pArray); ++i ) {
        xstrview View;
        bool Found = false;
        if ( !MdoModelsString(xrtValueArrayGet(pArray, i), &View) )
            return false;
        for ( j = 0u; j < sizeof(g_MdoCapabilities) /
                sizeof(g_MdoCapabilities[0]); ++j ) {
            if ( !MdoModelsViewEqual(View, g_MdoCapabilities[j].Name) )
                continue;
            if ( (Flags & g_MdoCapabilities[j].Flag) != 0u ) return false;
            Flags |= g_MdoCapabilities[j].Flag;
            Found = true;
            break;
        }
        if ( !Found ) return false;
    }
    *pCapabilities = Flags;
    return true;
}

static bool MdoModelsParseProtocols(const xvalue* pArray,
    MdoModelProtocolFlags* pProtocols)
{
    MdoModelProtocolFlags Flags = 0u;
    size_t i;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ) return false;
    for ( i = 0u; i < xrtValueCount(pArray); ++i ) {
        xstrview View;
        MdoModelProtocolFlags Flag;
        if ( !MdoModelsString(xrtValueArrayGet(pArray, i), &View) ||
             !MdoModelsProtocol(View, NULL, &Flag, NULL) ||
             (Flags & Flag) != 0u ) return false;
        Flags |= Flag;
    }
    *pProtocols = Flags;
    return Flags != 0u;
}

static bool MdoModelsParseReasoningEfforts(const xvalue* pArray,
    MdoModelEntry* pModel)
{
    size_t i;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ||
         xrtValueCount(pArray) == 0u ||
         xrtValueCount(pArray) > MDO_MODELS_MAX_REASONING_EFFORTS )
        return false;
    pModel->ReasoningEffortCount = xrtValueCount(pArray);
    pModel->ReasoningEfforts = (char**)xrtCalloc(
        pModel->ReasoningEffortCount, sizeof(*pModel->ReasoningEfforts));
    if ( pModel->ReasoningEfforts == NULL ) return false;
    for ( i = 0u; i < pModel->ReasoningEffortCount; ++i ) {
        xstrview View;
        if ( !MdoModelsString(xrtValueArrayGet(pArray, i), &View) )
            return false;
        pModel->ReasoningEfforts[i] = xrtStrDupN(View.Data, View.Size);
        if ( pModel->ReasoningEfforts[i] == NULL ) return false;
    }
    return true;
}

static bool MdoModelsParseAttachments(const xvalue* pArray,
    MdoModelAttachmentFlags* pAttachments)
{
    MdoModelAttachmentFlags Flags = 0u;
    size_t i;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ) return false;
    for ( i = 0u; i < xrtValueCount(pArray); ++i ) {
        xstrview View;
        MdoModelAttachmentFlags Flag;
        if ( !MdoModelsString(xrtValueArrayGet(pArray, i), &View) )
            return false;
        if ( MdoModelsViewEqual(View, "image") )
            Flag = MDO_MODEL_ATTACHMENT_IMAGE;
        else if ( MdoModelsViewEqual(View, "audio") )
            Flag = MDO_MODEL_ATTACHMENT_AUDIO;
        else if ( MdoModelsViewEqual(View, "file") )
            Flag = MDO_MODEL_ATTACHMENT_FILE;
        else return false;
        if ( (Flags & Flag) != 0u ) return false;
        Flags |= Flag;
    }
    *pAttachments = Flags;
    return true;
}

static bool MdoModelsParseModel(const xvalue* pValue,
    MdoModelCatalog* pCatalog, MdoModelEntry* pModel)
{
    const xvalue* pWindow;
    xstrview View;
    uint64 Context;
    uint64 MaxInput;
    uint64 MaxOutput;
    uint64 Reserve;
    uint64 Summary;
    if ( !MdoModelsCopyString(pValue, "id", &pModel->Id, true) ||
         !MdoModelsCopyString(pValue, "name", &pModel->Name, true) ||
         !MdoModelsCopyString(pValue, "provider", &pModel->ProviderId, true) ||
         !MdoModelsCopyString(pValue, "wire_model", &pModel->WireModel, true) ||
         !MdoModelsBool(pValue, "builtin", &pModel->Builtin) ||
         !MdoModelsBool(pValue, "free", &pModel->Free) ||
         !MdoModelsBool(pValue, "editable", &pModel->Editable) ||
         !MdoModelsBool(pValue, "removable", &pModel->Removable) ||
         MdoModelsFindProvider(pCatalog, pModel->ProviderId) == NULL ||
         !MdoModelsParseProtocols(xrtValueObjectGet(pValue,
            MdoModelsKey("protocols")), &pModel->Protocols) ||
         !MdoModelsString(xrtValueObjectGet(pValue,
            MdoModelsKey("default_protocol")), &View) ||
         !MdoModelsProtocol(View, &pModel->DefaultProtocol, NULL, NULL) ||
         (pModel->Protocols & MdoModelsProtocolFlag(
            pModel->DefaultProtocol)) == 0u ||
         !MdoModelsParseCapabilities(xrtValueObjectGet(pValue,
            MdoModelsKey("capabilities")), &pModel->Capabilities) ) return false;
    pWindow = xrtValueObjectGet(pValue, MdoModelsKey("window"));
    if ( xrtValueType(pWindow) != XVALUE_OBJECT ||
         !MdoModelsString(xrtValueObjectGet(pWindow,
            MdoModelsKey("mode")), &View) ) return false;
    if ( MdoModelsViewEqual(View, "shared-context") )
        pModel->WindowMode = XLLM_WINDOW_SHARED_CONTEXT;
    else if ( MdoModelsViewEqual(View, "split-input-output") )
        pModel->WindowMode = XLLM_WINDOW_SPLIT_INPUT_OUTPUT;
    else return false;
    if ( !MdoModelsUnsigned(xrtValueObjectGet(pWindow,
            MdoModelsKey("context_tokens")), &Context) ||
         !MdoModelsUnsigned(xrtValueObjectGet(pWindow,
            MdoModelsKey("max_input_tokens")), &MaxInput) ||
         !MdoModelsUnsigned(xrtValueObjectGet(pWindow,
            MdoModelsKey("max_output_tokens")), &MaxOutput) ||
         !MdoModelsUnsigned(xrtValueObjectGet(pWindow,
            MdoModelsKey("output_reserve_tokens")), &Reserve) ||
         !MdoModelsUnsigned(xrtValueObjectGet(pWindow,
            MdoModelsKey("summary_tokens")), &Summary) ||
         MaxOutput > UINT32_MAX || Reserve > UINT32_MAX ||
         Summary > UINT32_MAX ) return false;
    pModel->ContextWindowTokens = Context;
    pModel->MaxInputTokens = MaxInput;
    pModel->MaxOutputTokens = (uint32)MaxOutput;
    pModel->OutputReserveTokens = (uint32)Reserve;
    pModel->SummaryTokens = (uint32)Summary;
    if ( !MdoModelsParseReasoningEfforts(xrtValueObjectGet(pValue,
            MdoModelsKey("reasoning_efforts")), pModel) ||
         !MdoModelsCopyString(pValue, "default_reasoning_effort",
            &pModel->DefaultReasoningEffort, true) ||
         !MdoModelsParseAttachments(xrtValueObjectGet(pValue,
            MdoModelsKey("attachments")), &pModel->Attachments) ) return false;
    return true;
}

static MdoModelCatalog* MdoModelsBuildCandidate(uint64 Generation)
{
    xjsonreadconfig JsonConfig;
    MdoModelCatalog* pCatalog = NULL;
    xvalue* pRoot = NULL;
    const xvalue* pModels;
    const xvalue* pProviders;
    const xvalue* pItems;
    xstrview DefaultId;
    str Json = NULL;
    size_t JsonBytes = 0u;
    size_t i;

    Json = MdoConfigEffectiveJson(&JsonBytes);
    if ( Json == NULL ) return NULL;
    xrtJsonReadConfigInit(&JsonConfig);
    JsonConfig.MaxInputBytes = MDO_MODELS_MAX_CONFIG_BYTES;
    JsonConfig.MaxDepth = 64u;
    JsonConfig.MaxValues = 16384u;
    JsonConfig.MaxContainerItems = 8192u;
    pRoot = xrtJsonRead(xrtStrViewN(Json, JsonBytes), &JsonConfig);
    xrtFree(Json);
    if ( pRoot == NULL ) return NULL;
    pModels = xrtValueObjectGet(pRoot, MdoModelsKey("models"));
    if ( xrtValueType(pModels) != XVALUE_OBJECT ) goto fail;
    pProviders = xrtValueObjectGet(pModels, MdoModelsKey("providers"));
    pItems = xrtValueObjectGet(pModels, MdoModelsKey("items"));
    if ( xrtValueType(pProviders) != XVALUE_ARRAY ||
         xrtValueCount(pProviders) == 0u ||
         xrtValueCount(pProviders) > MDO_MODELS_MAX_PROVIDERS ||
         xrtValueType(pItems) != XVALUE_ARRAY ||
         xrtValueCount(pItems) == 0u ||
         xrtValueCount(pItems) > MDO_MODELS_MAX_MODELS ||
         !MdoModelsString(xrtValueObjectGet(pModels,
            MdoModelsKey("default_model")), &DefaultId) ) goto fail;
    pCatalog = MdoModelsCatalogCreate(Generation);
    if ( pCatalog == NULL ) goto fail;
    pCatalog->ProviderCount = xrtValueCount(pProviders);
    pCatalog->Providers = (MdoProviderEntry*)xrtCalloc(
        pCatalog->ProviderCount, sizeof(*pCatalog->Providers));
    if ( pCatalog->Providers == NULL ) goto fail;
    for ( i = 0u; i < pCatalog->ProviderCount; ++i ) {
        if ( !MdoModelsParseProvider(xrtValueArrayGet(pProviders, i),
                &pCatalog->Providers[i]) ||
             MdoModelsFindProvider(pCatalog, pCatalog->Providers[i].Id) !=
                &pCatalog->Providers[i] ) goto fail;
    }
    pCatalog->ModelCount = xrtValueCount(pItems);
    pCatalog->Models = (MdoModelEntry*)xrtCalloc(pCatalog->ModelCount,
        sizeof(*pCatalog->Models));
    if ( pCatalog->Models == NULL ) goto fail;
    for ( i = 0u; i < pCatalog->ModelCount; ++i ) {
        MdoModelEntry* pModel = &pCatalog->Models[i];
        if ( !MdoModelsParseModel(xrtValueArrayGet(pItems, i), pCatalog,
                pModel) || MdoModelsFindModel(pCatalog, pModel->Id) != pModel )
            goto fail;
        if ( strlen(pModel->Id) == DefaultId.Size &&
             memcmp(pModel->Id, DefaultId.Data, DefaultId.Size) == 0 )
            pCatalog->DefaultModelIndex = i;
    }
    if ( pCatalog->DefaultModelIndex == SIZE_MAX ) goto fail;
    xrtValueRelease(pRoot);
    return pCatalog;

fail:
    MdoModelCatalogRelease(pCatalog);
    xrtValueRelease(pRoot);
    if ( xrtGetError() == NULL )
        MdoModelsSetError(XERR_ARGUMENT, "effective model configuration is invalid");
    return NULL;
}

bool MdoModelManagerReload(void)
{
    MdoModelCatalog* pCandidate;
    MdoModelCatalog* pOld;
    uint64 Generation;
    if ( g_MdoModels.ReloadLock == NULL ||
         !xrtMutexLock(g_MdoModels.ReloadLock) ) {
        MdoModelsSetError(XERR_STATE, "model manager is not initialized");
        return false;
    }
    if ( !xrtMutexLock(g_MdoModels.Lock) ) {
        (void)xrtMutexUnlock(g_MdoModels.ReloadLock);
        MdoModelsSetError(XERR_STATE, "model manager is not initialized");
        return false;
    }
    if ( !g_MdoModels.Initialized ) {
        (void)xrtMutexUnlock(g_MdoModels.Lock);
        (void)xrtMutexUnlock(g_MdoModels.ReloadLock);
        MdoModelsSetError(XERR_STATE, "model manager is not initialized");
        return false;
    }
    Generation = g_MdoModels.NextGeneration + 1u;
    (void)xrtMutexUnlock(g_MdoModels.Lock);
    pCandidate = MdoModelsBuildCandidate(Generation);
    if ( pCandidate == NULL ) {
        (void)xrtMutexUnlock(g_MdoModels.ReloadLock);
        return false;
    }
    if ( !xrtMutexLock(g_MdoModels.Lock) ) {
        MdoModelCatalogRelease(pCandidate);
        (void)xrtMutexUnlock(g_MdoModels.ReloadLock);
        MdoModelsSetError(XERR_STATE, "model manager stopped during reload");
        return false;
    }
    if ( !g_MdoModels.Initialized ) {
        (void)xrtMutexUnlock(g_MdoModels.Lock);
        MdoModelCatalogRelease(pCandidate);
        (void)xrtMutexUnlock(g_MdoModels.ReloadLock);
        MdoModelsSetError(XERR_STATE, "model manager stopped during reload");
        return false;
    }
    pOld = g_MdoModels.Catalog;
    g_MdoModels.Catalog = MdoModelCatalogRef(pCandidate);
    g_MdoModels.NextGeneration = Generation;
    (void)xrtMutexUnlock(g_MdoModels.Lock);
    MdoModelCatalogRelease(pOld);
    MdoModelCatalogRelease(pCandidate);
    (void)xrtMutexUnlock(g_MdoModels.ReloadLock);
    return true;
}

bool MdoModelManagerInit(void)
{
    if ( g_MdoModels.Initialized ) return true;
    memset(&g_MdoModels, 0, sizeof(g_MdoModels));
    g_MdoModels.Lock = xrtMutexCreate();
    g_MdoModels.ReloadLock = xrtMutexCreate();
    g_MdoModels.Catalog = MdoModelsCatalogCreate(0u);
    if ( g_MdoModels.Lock == NULL || g_MdoModels.ReloadLock == NULL ||
         g_MdoModels.Catalog == NULL ) {
        MdoModelManagerUnit();
        MdoModelsSetError(XERR_MEMORY, "cannot allocate model manager state");
        return false;
    }
    g_MdoModels.Initialized = true;
    if ( !MdoModelManagerReload() ) {
        MdoModelManagerUnit();
        return false;
    }
    return true;
}

void MdoModelManagerUnit(void)
{
    MdoModelCatalog* pCatalog = g_MdoModels.Catalog;
    xmutex* pLock = g_MdoModels.Lock;
    xmutex* pReloadLock = g_MdoModels.ReloadLock;
    memset(&g_MdoModels, 0, sizeof(g_MdoModels));
    MdoModelCatalogRelease(pCatalog);
    if ( pLock != NULL ) xrtMutexDestroy(pLock);
    if ( pReloadLock != NULL ) xrtMutexDestroy(pReloadLock);
}

uint64 MdoModelManagerGeneration(void)
{
    uint64 Generation = 0u;
    if ( g_MdoModels.Lock != NULL && xrtMutexLock(g_MdoModels.Lock) ) {
        Generation = g_MdoModels.NextGeneration;
        (void)xrtMutexUnlock(g_MdoModels.Lock);
    }
    return Generation;
}

MdoModelCatalog* MdoModelCatalogSnapshot(void)
{
    MdoModelCatalog* pCatalog = NULL;
    if ( g_MdoModels.Lock != NULL && xrtMutexLock(g_MdoModels.Lock) ) {
        pCatalog = MdoModelCatalogRef(g_MdoModels.Catalog);
        (void)xrtMutexUnlock(g_MdoModels.Lock);
    }
    return pCatalog;
}

size_t MdoModelCatalogProviderCount(const MdoModelCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->ProviderCount : 0u;
}

size_t MdoModelCatalogModelCount(const MdoModelCatalog* pCatalog)
{
    return pCatalog != NULL ? pCatalog->ModelCount : 0u;
}

static bool MdoModelsProviderInfo(const MdoModelCatalog* pCatalog,
    const MdoProviderEntry* pProvider, MdoProviderInfo* pInfo)
{
    if ( pInfo == NULL || pInfo->Size < sizeof(*pInfo) ) return false;
    pInfo->Generation = pCatalog->Generation;
    pInfo->Builtin = pProvider->Builtin;
    pInfo->Editable = pProvider->Editable;
    pInfo->Removable = pProvider->Removable;
    pInfo->VerifyPeer = pProvider->VerifyPeer;
    pInfo->HasCredentialReference = pProvider->CredentialReference != NULL;
    pInfo->Id = pProvider->Id;
    pInfo->Name = pProvider->Name;
    pInfo->TimeoutMilliseconds = pProvider->TimeoutMilliseconds;
    pInfo->Protocols = pProvider->Protocols;
    pInfo->ChatCompletionsEndpoint = pProvider->Endpoints[0];
    pInfo->ResponsesEndpoint = pProvider->Endpoints[1];
    pInfo->AnthropicMessagesEndpoint = pProvider->Endpoints[2];
    return true;
}

bool MdoModelCatalogProviderAt(const MdoModelCatalog* pCatalog,
    size_t Index, MdoProviderInfo* pInfo)
{
    return pCatalog != NULL && Index < pCatalog->ProviderCount &&
        MdoModelsProviderInfo(pCatalog, &pCatalog->Providers[Index], pInfo);
}

bool MdoModelCatalogProviderFind(const MdoModelCatalog* pCatalog,
    const char* ProviderId, MdoProviderInfo* pInfo)
{
    size_t i;
    if ( pCatalog == NULL || ProviderId == NULL ) return false;
    for ( i = 0u; i < pCatalog->ProviderCount; ++i )
        if ( strcmp(pCatalog->Providers[i].Id, ProviderId) == 0 )
            return MdoModelsProviderInfo(pCatalog, &pCatalog->Providers[i],
                pInfo);
    return false;
}

static bool MdoModelsModelInfo(const MdoModelCatalog* pCatalog,
    const MdoModelEntry* pModel, MdoModelInfo* pInfo)
{
    if ( pInfo == NULL || pInfo->Size < sizeof(*pInfo) ) return false;
    pInfo->Generation = pCatalog->Generation;
    pInfo->Builtin = pModel->Builtin;
    pInfo->Free = pModel->Free;
    pInfo->Editable = pModel->Editable;
    pInfo->Removable = pModel->Removable;
    pInfo->Id = pModel->Id;
    pInfo->Name = pModel->Name;
    pInfo->ProviderId = pModel->ProviderId;
    pInfo->WireModel = pModel->WireModel;
    pInfo->Protocols = pModel->Protocols;
    pInfo->DefaultProtocol = pModel->DefaultProtocol;
    pInfo->Capabilities = pModel->Capabilities;
    pInfo->WindowMode = pModel->WindowMode;
    pInfo->ContextWindowTokens = pModel->ContextWindowTokens;
    pInfo->MaxInputTokens = pModel->MaxInputTokens;
    pInfo->MaxOutputTokens = pModel->MaxOutputTokens;
    pInfo->OutputReserveTokens = pModel->OutputReserveTokens;
    pInfo->SummaryTokens = pModel->SummaryTokens;
    pInfo->ReasoningEfforts = (const char* const*)pModel->ReasoningEfforts;
    pInfo->ReasoningEffortCount = pModel->ReasoningEffortCount;
    pInfo->DefaultReasoningEffort = pModel->DefaultReasoningEffort;
    pInfo->Attachments = pModel->Attachments;
    return true;
}

bool MdoModelCatalogModelAt(const MdoModelCatalog* pCatalog,
    size_t Index, MdoModelInfo* pInfo)
{
    return pCatalog != NULL && Index < pCatalog->ModelCount &&
        MdoModelsModelInfo(pCatalog, &pCatalog->Models[Index], pInfo);
}

bool MdoModelCatalogModelFind(const MdoModelCatalog* pCatalog,
    const char* ModelId, MdoModelInfo* pInfo)
{
    size_t i;
    if ( pCatalog == NULL || ModelId == NULL ) return false;
    for ( i = 0u; i < pCatalog->ModelCount; ++i )
        if ( strcmp(pCatalog->Models[i].Id, ModelId) == 0 )
            return MdoModelsModelInfo(pCatalog, &pCatalog->Models[i], pInfo);
    return false;
}

bool MdoModelCatalogDefault(const MdoModelCatalog* pCatalog,
    MdoModelInfo* pInfo)
{
    return pCatalog != NULL &&
        pCatalog->DefaultModelIndex < pCatalog->ModelCount &&
        MdoModelsModelInfo(pCatalog,
            &pCatalog->Models[pCatalog->DefaultModelIndex], pInfo);
}

static void MdoModelsProfileError(xllm_error* pError, cstr Message)
{
    if ( pError == NULL ) return;
    xllmErrorInit(pError);
    pError->eCode = XLLM_ERROR_INVALID_ARGUMENT;
    snprintf(pError->sMessage, sizeof(pError->sMessage), "%s", Message);
}

bool MdoModelCatalogProfile(const MdoModelCatalog* pCatalog,
    const char* ModelId, MdoModelProtocol Protocol,
    xllm_model_profile* pProfile, xllm_error* pError)
{
    const MdoModelEntry* pModel = NULL;
    MdoModelProtocolFlags Flag;
    size_t i;
    if ( pCatalog == NULL || pProfile == NULL ) {
        MdoModelsProfileError(pError, "catalog and profile output are required");
        return false;
    }
    if ( ModelId == NULL || ModelId[0] == '\0' ) {
        if ( pCatalog->DefaultModelIndex < pCatalog->ModelCount )
            pModel = &pCatalog->Models[pCatalog->DefaultModelIndex];
    } else {
        for ( i = 0u; i < pCatalog->ModelCount; ++i )
            if ( strcmp(pCatalog->Models[i].Id, ModelId) == 0 ) {
                pModel = &pCatalog->Models[i];
                break;
            }
    }
    if ( pModel == NULL ) {
        MdoModelsProfileError(pError, "model was not found");
        return false;
    }
    if ( Protocol == 0 ) Protocol = pModel->DefaultProtocol;
    Flag = MdoModelsProtocolFlag(Protocol);
    if ( Flag == 0u || (pModel->Protocols & Flag) == 0u ) {
        MdoModelsProfileError(pError, "model does not support the selected protocol");
        return false;
    }
    xllmModelProfileInit(pProfile);
    pProfile->sId = pModel->Id;
    pProfile->sModel = pModel->WireModel;
    pProfile->eProvider = MdoModelProtocolProvider(Protocol);
    pProfile->uCapabilities = pModel->Capabilities;
    pProfile->eWindowMode = pModel->WindowMode;
    pProfile->uContextWindowTokens = pModel->ContextWindowTokens;
    pProfile->uMaxInputTokens = pModel->MaxInputTokens;
    pProfile->uMaxOutputTokens = pModel->MaxOutputTokens;
    pProfile->uRecommendedOutputReserveTokens =
        pModel->OutputReserveTokens;
    pProfile->uRecommendedSummaryTokens = pModel->SummaryTokens;
    return xllmModelProfileValidate(pProfile, pError);
}
