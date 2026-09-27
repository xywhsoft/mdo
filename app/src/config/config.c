#include <stdio.h>
#include <string.h>

#include "../../include/mdo/config.h"
#include "../../include/mdo/home.h"

#define MDO_CONFIG_ERROR_DOMAIN "mdo.config"
#define MDO_CONFIG_MAX_FILE (1024u * 1024u)

typedef enum MdoConfigError {
    MDO_CONFIG_ERROR_ARGUMENT = 1,
    MDO_CONFIG_ERROR_STATE,
    MDO_CONFIG_ERROR_PARSE,
    MDO_CONFIG_ERROR_SCHEMA,
    MDO_CONFIG_ERROR_SECRET,
    MDO_CONFIG_ERROR_PROTECTED,
    MDO_CONFIG_ERROR_STORAGE
} MdoConfigError;

typedef struct MdoConfigState {
    xmutex* Lock;
    xvalue* Defaults;
    xvalue* Patch[MDO_CONFIG_DOMAIN_COUNT];
    xvalue* Runtime;
    xvalue* Effective;
    uint64 Revision;
    size_t EffectiveBytes;
    bool RuntimeOverride;
    bool Initialized;
} MdoConfigState;

static MdoConfigState g_MdoConfig;

static const char* const g_MdoConfigDomainName[MDO_CONFIG_DOMAIN_COUNT] = {
    "settings", "models", "permissions"
};

static const char* const g_MdoConfigDomainPath[MDO_CONFIG_DOMAIN_COUNT] = {
    "config/settings.json", "config/models.json", "config/permissions.json"
};

static void MdoConfigErrorSet(xerrkind Kind, MdoConfigError Code,
    cstr Message)
{
    xerror* pError = xrtErrorCreate(Kind, MDO_CONFIG_ERROR_DOMAIN,
        (int32)Code, Message);
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static bool MdoConfigDomainValid(MdoConfigDomain Domain)
{
    return Domain >= MDO_CONFIG_SETTINGS && Domain < MDO_CONFIG_DOMAIN_COUNT;
}

static xstrview MdoConfigKey(cstr Text)
{
    return xrtStrView(Text);
}

static bool MdoConfigViewEqual(xstrview Left, cstr Right)
{
    size_t iSize = strlen(Right);
    return Left.Size == iSize && memcmp(Left.Data, Right, iSize) == 0;
}

static bool MdoConfigString(const xvalue* pValue, xstrview* pText)
{
    return pValue != NULL && xrtValueType(pValue) == XVALUE_STRING &&
        xrtValueGetString(pValue, pText);
}

static bool MdoConfigUnsigned(const xvalue* pValue, uint64* pResult)
{
    int64 iSigned;

    if ( pValue == NULL ) return false;
    if ( xrtValueType(pValue) == XVALUE_UINT )
        return xrtValueGetUInt(pValue, pResult);
    if ( xrtValueType(pValue) != XVALUE_INT ||
         !xrtValueGetInt(pValue, &iSigned) || iSigned < 0 ) return false;
    *pResult = (uint64)iSigned;
    return true;
}

static bool MdoConfigSchemaVersion(const xvalue* pRoot)
{
    uint64 iVersion;
    return pRoot != NULL && xrtValueType(pRoot) == XVALUE_OBJECT &&
        MdoConfigUnsigned(xrtValueObjectGet(pRoot,
            MdoConfigKey("schema_version")), &iVersion) &&
        iVersion == MDO_CONFIG_SCHEMA_VERSION;
}

static xvalue* MdoConfigObject(void)
{
    xvalue* pObject = xrtValueObject();
    if ( pObject == NULL )
        MdoConfigErrorSet(XERR_MEMORY, MDO_CONFIG_ERROR_STATE,
            "cannot allocate configuration object");
    return pObject;
}

static bool MdoConfigMergeObject(xvalue* pTarget, const xvalue* pPatch)
{
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* pValue;
    xvalueiterresult Result;

    if ( pTarget == NULL || pPatch == NULL ||
         xrtValueType(pTarget) != XVALUE_OBJECT ||
         xrtValueType(pPatch) != XVALUE_OBJECT ) {
        MdoConfigErrorSet(XERR_TYPE, MDO_CONFIG_ERROR_SCHEMA,
            "configuration merge requires objects");
        return false;
    }
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(pPatch, &Iterator) ) return false;
    for ( ; ; ) {
        xvalue* pCopy = NULL;
        xvalue* pCurrent;

        Result = xrtValueIterAdvance(&Iterator, &Key, &pValue);
        if ( Result == XVALUE_ITER_END ) break;
        if ( Result == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
        pCurrent = xrtValueObjectGet(pTarget, Key.String);
        if ( pCurrent != NULL && xrtValueType(pCurrent) == XVALUE_OBJECT &&
             xrtValueType(pValue) == XVALUE_OBJECT ) {
            pCopy = xrtValueDeepClone(pCurrent);
            if ( pCopy == NULL || !MdoConfigMergeObject(pCopy, pValue) ) {
                xrtValueRelease(pCopy);
                xrtValueIterEnd(&Iterator);
                return false;
            }
        } else {
            pCopy = xrtValueDeepClone(pValue);
            if ( pCopy == NULL ) {
                xrtValueIterEnd(&Iterator);
                return false;
            }
        }
        if ( !xrtValueObjectSetTake(pTarget, Key.String, &pCopy) ) {
            xrtValueRelease(pCopy);
            xrtValueIterEnd(&Iterator);
            return false;
        }
    }
    xrtValueIterEnd(&Iterator);
    return true;
}

static bool MdoConfigDiff(const xvalue* pBase, const xvalue* pValue,
    xvalue** ppDiff, bool* pEqual)
{
    xvalue* pResult = NULL;
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* pItem;
    xvalueiterresult IterResult;

    *ppDiff = NULL;
    *pEqual = false;
    if ( xrtValueEqual(pBase, pValue) ) {
        *pEqual = true;
        return true;
    }
    if ( xrtValueType(pBase) != XVALUE_OBJECT ||
         xrtValueType(pValue) != XVALUE_OBJECT ) {
        pResult = xrtValueDeepClone(pValue);
        if ( pResult == NULL ) return false;
        *ppDiff = pResult;
        return true;
    }
    pResult = MdoConfigObject();
    if ( pResult == NULL ) return false;
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(pValue, &Iterator) ) goto fail;
    for ( ; ; ) {
        xvalue* pChild = NULL;
        bool bEqual = false;
        xvalue* pBaseItem;

        IterResult = xrtValueIterAdvance(&Iterator, &Key, &pItem);
        if ( IterResult == XVALUE_ITER_END ) break;
        if ( IterResult == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            goto fail;
        }
        pBaseItem = xrtValueObjectGet(pBase, Key.String);
        if ( pBaseItem != NULL ) {
            if ( !MdoConfigDiff(pBaseItem, pItem, &pChild, &bEqual) ) {
                xrtValueIterEnd(&Iterator);
                goto fail;
            }
            if ( bEqual ) continue;
        } else {
            pChild = xrtValueDeepClone(pItem);
            if ( pChild == NULL ) {
                xrtValueIterEnd(&Iterator);
                goto fail;
            }
        }
        if ( !xrtValueObjectSetTake(pResult, Key.String, &pChild) ) {
            xrtValueRelease(pChild);
            xrtValueIterEnd(&Iterator);
            goto fail;
        }
    }
    xrtValueIterEnd(&Iterator);
    *pEqual = xrtValueCount(pResult) == 0u;
    if ( *pEqual ) {
        xrtValueRelease(pResult);
        pResult = NULL;
    }
    *ppDiff = pResult;
    return true;

fail:
    xrtValueRelease(pResult);
    return false;
}

static void MdoConfigCanonicalKey(xstrview Key, char* Output,
    size_t iCapacity)
{
    size_t i;
    size_t iOut = 0u;

    for ( i = 0u; i < Key.Size && iOut + 1u < iCapacity; i++ ) {
        unsigned char Ch = (unsigned char)Key.Data[i];
        if ( Ch == '_' || Ch == '-' || Ch == '.' ) continue;
        if ( Ch >= 'A' && Ch <= 'Z' ) Ch = (unsigned char)(Ch + ('a' - 'A'));
        Output[iOut++] = (char)Ch;
    }
    Output[iOut] = '\0';
}

static bool MdoConfigSecretReferenceValid(const xvalue* pValue)
{
    static const char* const Prefixes[] = {
        "env:", "file:", "keychain:", "prompt:"
    };
    xstrview Text;
    size_t i;

    if ( !MdoConfigString(pValue, &Text) || Text.Size == 0u ) return false;
    for ( i = 0u; i < sizeof(Prefixes) / sizeof(Prefixes[0]); i++ ) {
        size_t iPrefix = strlen(Prefixes[i]);
        if ( Text.Size > iPrefix &&
             memcmp(Text.Data, Prefixes[i], iPrefix) == 0 ) return true;
    }
    return false;
}

static bool MdoConfigSecretsValidate(const xvalue* pValue)
{
    xvaluetype Type = xrtValueType(pValue);
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* pItem;
    xvalueiterresult Result;

    if ( Type != XVALUE_OBJECT && Type != XVALUE_ARRAY ) return true;
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(pValue, &Iterator) ) return false;
    for ( ; ; ) {
        Result = xrtValueIterAdvance(&Iterator, &Key, &pItem);
        if ( Result == XVALUE_ITER_END ) break;
        if ( Result == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
        if ( Type == XVALUE_OBJECT ) {
            char sKey[64];
            MdoConfigCanonicalKey(Key.String, sKey, sizeof(sKey));
            if ( strcmp(sKey, "secretref") == 0 ) {
                if ( !MdoConfigSecretReferenceValid(pItem) ) {
                    xrtValueIterEnd(&Iterator);
                    MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SECRET,
                        "secret_ref must use env:, file:, keychain:, or prompt:");
                    return false;
                }
            } else if ( strcmp(sKey, "apikey") == 0 ||
                        strcmp(sKey, "key") == 0 ||
                        strcmp(sKey, "accesstoken") == 0 ||
                        strcmp(sKey, "refreshtoken") == 0 ||
                        strcmp(sKey, "token") == 0 ||
                        strcmp(sKey, "secret") == 0 ||
                        strcmp(sKey, "clientsecret") == 0 ||
                        strcmp(sKey, "privatekey") == 0 ||
                        strcmp(sKey, "bearer") == 0 ||
                        strcmp(sKey, "password") == 0 ||
                        strcmp(sKey, "proxypass") == 0 ||
                        strcmp(sKey, "authorization") == 0 ) {
                xrtValueIterEnd(&Iterator);
                MdoConfigErrorSet(XERR_PERMISSION, MDO_CONFIG_ERROR_SECRET,
                    "sensitive values must be stored as secret_ref");
                return false;
            }
        }
        if ( !MdoConfigSecretsValidate(pItem) ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
    }
    xrtValueIterEnd(&Iterator);
    return true;
}

static bool MdoConfigStringOneOf(const xvalue* pValue,
    const char* const* ppChoices, size_t iChoices)
{
    xstrview Text;
    size_t i;

    if ( !MdoConfigString(pValue, &Text) ) return false;
    for ( i = 0u; i < iChoices; i++ )
        if ( MdoConfigViewEqual(Text, ppChoices[i]) ) return true;
    return false;
}

static bool MdoConfigBool(const xvalue* pObject, cstr Name)
{
    bool bValue;
    const xvalue* pValue = xrtValueObjectGet(pObject, MdoConfigKey(Name));
    return pValue != NULL && xrtValueType(pValue) == XVALUE_BOOL &&
        xrtValueGetBool(pValue, &bValue);
}

static bool MdoConfigHomeRelativePath(xstrview Path)
{
    size_t Segment = 0u;
    size_t i;
    if ( Path.Size == 0u || Path.Data[0] == '/' ||
         !xrtUtf8Valid(Path, NULL) ) return false;
    for ( i = 0u; i <= Path.Size; ++i ) {
        unsigned char Ch = i == Path.Size ? 0u : (unsigned char)Path.Data[i];
        if ( Ch == '\\' || Ch == ':' || (i != Path.Size && Ch < 0x20u) )
            return false;
        if ( Ch == '/' || i == Path.Size ) {
            size_t Length = i - Segment;
            if ( Length == 0u ||
                 (Length == 1u && Path.Data[Segment] == '.') ||
                 (Length == 2u && Path.Data[Segment] == '.' &&
                  Path.Data[Segment + 1u] == '.') ) return false;
            Segment = i + 1u;
        }
    }
    return true;
}

static bool MdoConfigProxyText(xstrview Text, size_t Maximum,
    bool HostOrBypass)
{
    size_t i;
    if ( Text.Size >= Maximum || !xrtUtf8Valid(Text, NULL) ) return false;
    for ( i = 0u; i < Text.Size; ++i ) {
        unsigned char Ch = (unsigned char)Text.Data[i];
        if ( Ch == 0u || Ch == '\r' || Ch == '\n' ) return false;
        if ( HostOrBypass && (Ch <= 0x20u || Ch > 0x7eu ||
                Ch == '/' || Ch == '@') ) return false;
    }
    return true;
}

static bool MdoConfigProxyValidate(const xvalue* Proxy)
{
    static const char* const Kinds[] = {
        "none", "http-connect", "socks5"
    };
    const xvalue* Credential;
    xstrview Kind;
    xstrview Host;
    xstrview User;
    xstrview Bypass;
    xstrview SecretRef;
    uint64 Port;
    if ( xrtValueType(Proxy) != XVALUE_OBJECT ||
         !MdoConfigStringOneOf(xrtValueObjectGet(Proxy,
            MdoConfigKey("kind")), Kinds, 3u) ||
         !MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("kind")), &Kind) ||
         !MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("host")), &Host) ||
         !MdoConfigProxyText(Host, 256u, true) ||
         !MdoConfigUnsigned(xrtValueObjectGet(Proxy,
            MdoConfigKey("port")), &Port) || Port > 65535u ||
         !MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("user")), &User) ||
         !MdoConfigProxyText(User, 256u, false) ||
         !MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("bypass")), &Bypass) ||
         !MdoConfigProxyText(Bypass, 1024u, true) ||
         (!MdoConfigViewEqual(Kind, "none") &&
          (Host.Size == 0u || Port == 0u)) ) return false;
    Credential = xrtValueObjectGet(Proxy, MdoConfigKey("credential"));
    if ( Credential != NULL && xrtValueType(Credential) != XVALUE_NULL &&
         (xrtValueType(Credential) != XVALUE_OBJECT ||
          xrtValueCount(Credential) != 1u || User.Size == 0u ||
          !MdoConfigSecretReferenceValid(xrtValueObjectGet(Credential,
            MdoConfigKey("secret_ref"))) ||
          !MdoConfigString(xrtValueObjectGet(Credential,
            MdoConfigKey("secret_ref")), &SecretRef) ||
          SecretRef.Size >= 2049u) ) return false;
    return true;
}

static bool MdoConfigSettingsValidate(const xvalue* pSettings)
{
    static const char* const Themes[] = { "system", "light", "dark" };
    static const char* const FontSizes[] = { "small", "normal", "large" };
    static const char* const Densities[] = { "compact", "comfortable" };
    static const char* const SubmitModes[] = { "queue", "guide" };
    static const char* const InteractionModes[] = { "ask", "agent", "plan" };
    static const char* const Efforts[] = {
        "none", "minimal", "low", "medium", "high", "xhigh", "max"
    };
    static const char* const SearchProviders[] = { "brave" };
    static const char* const OpenModes[] = { "last", "new", "ask" };
    const xvalue* pAppearance;
    const xvalue* pComposer;
    const xvalue* pNotifications;
    const xvalue* pPower;
    const xvalue* pAgent;
    const xvalue* pWeb;
    const xvalue* pSearch;
    const xvalue* pTransport;
    const xvalue* pProxy;
    const xvalue* pWorkspace;
    xstrview Locale;
    xstrview Text;
    uint64 iValue;
    uint64 WebTimeout;
    uint64 WebIdleTimeout;
    uint64 WebMaxResponse;
    uint64 WebMaxText;
    uint64 WebMaxDocuments;
    uint64 WebMaxResults;

    if ( xrtValueType(pSettings) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pSettings,
            MdoConfigKey("locale")), &Locale) ||
         Locale.Size < 2u || Locale.Size > 32u ) goto invalid;
    pAppearance = xrtValueObjectGet(pSettings, MdoConfigKey("appearance"));
    pComposer = xrtValueObjectGet(pSettings, MdoConfigKey("composer"));
    pNotifications = xrtValueObjectGet(pSettings,
        MdoConfigKey("notifications"));
    pPower = xrtValueObjectGet(pSettings, MdoConfigKey("power"));
    pAgent = xrtValueObjectGet(pSettings, MdoConfigKey("agent"));
    pWeb = xrtValueObjectGet(pSettings, MdoConfigKey("web"));
    pSearch = pWeb != NULL ?
        xrtValueObjectGet(pWeb, MdoConfigKey("search")) : NULL;
    pTransport = xrtValueObjectGet(pSettings, MdoConfigKey("transport"));
    pProxy = pTransport != NULL ?
        xrtValueObjectGet(pTransport, MdoConfigKey("proxy")) : NULL;
    pWorkspace = xrtValueObjectGet(pSettings, MdoConfigKey("workspace"));
    if ( xrtValueType(pAppearance) != XVALUE_OBJECT ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pAppearance,
            MdoConfigKey("theme")), Themes,
            sizeof(Themes) / sizeof(Themes[0])) ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pAppearance,
            MdoConfigKey("font_size")), FontSizes,
            sizeof(FontSizes) / sizeof(FontSizes[0])) ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pAppearance,
            MdoConfigKey("density")), Densities,
            sizeof(Densities) / sizeof(Densities[0])) ||
         xrtValueType(pComposer) != XVALUE_OBJECT ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pComposer,
            MdoConfigKey("submit_mode")), SubmitModes,
            sizeof(SubmitModes) / sizeof(SubmitModes[0])) ||
         (pNotifications != NULL &&
          (xrtValueType(pNotifications) != XVALUE_OBJECT ||
           !MdoConfigBool(pNotifications, "sound"))) ||
         xrtValueType(pPower) != XVALUE_OBJECT ||
         !MdoConfigBool(pPower, "prevent_sleep") ||
         xrtValueType(pAgent) != XVALUE_OBJECT ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pAgent,
            MdoConfigKey("interaction_mode")), InteractionModes,
            sizeof(InteractionModes) / sizeof(InteractionModes[0])) ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pAgent,
            MdoConfigKey("reasoning_effort")), Efforts,
            sizeof(Efforts) / sizeof(Efforts[0])) ||
         !MdoConfigString(xrtValueObjectGet(pAgent,
            MdoConfigKey("user_instructions")), &Text) ||
         Text.Size > 8192u ||
         (Text.Size != 0u &&
          memchr(Text.Data, '\0', Text.Size) != NULL) ||
         !xrtUtf8Valid(Text, NULL) ||
         !MdoConfigBool(pAgent, "web_search") ||
         !MdoConfigBool(pAgent, "memory") ||
         !MdoConfigBool(pAgent, "schedules") ||
         !MdoConfigUnsigned(xrtValueObjectGet(pAgent,
            MdoConfigKey("max_parallel_tools")), &iValue) ||
         iValue == 0u || iValue > 64u ||
         !MdoConfigUnsigned(xrtValueObjectGet(pAgent,
            MdoConfigKey("max_parallel_subagents")), &iValue) ||
         iValue > 16u ||
         xrtValueType(pWeb) != XVALUE_OBJECT ||
         !MdoConfigBool(pWeb, "enabled") ||
         !MdoConfigBool(pWeb, "allow_http") ||
         !MdoConfigBool(pWeb, "allow_private_networks") ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("timeout_ms")), &WebTimeout) ||
         WebTimeout < 1000u || WebTimeout > 300000u ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("idle_timeout_ms")), &WebIdleTimeout) ||
         WebIdleTimeout < 1000u || WebIdleTimeout > WebTimeout ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("max_response_bytes")), &WebMaxResponse) ||
         WebMaxResponse < 16384u || WebMaxResponse > 16777216u ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("max_text_bytes")), &WebMaxText) ||
         WebMaxText < 4096u || WebMaxText > 1048576u ||
         WebMaxText > WebMaxResponse ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("max_documents")), &WebMaxDocuments) ||
         WebMaxDocuments == 0u || WebMaxDocuments > 128u ||
         xrtValueType(pSearch) != XVALUE_OBJECT ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pSearch,
            MdoConfigKey("provider")), SearchProviders,
            sizeof(SearchProviders) / sizeof(SearchProviders[0])) ||
         !MdoConfigString(xrtValueObjectGet(pSearch,
            MdoConfigKey("endpoint")), &Text) ||
         Text.Size == 0u || Text.Size >= 2048u ||
         !MdoConfigString(xrtValueObjectGet(pSearch,
            MdoConfigKey("secret_ref")), &Text) ||
         Text.Size == 0u || Text.Size >= 2049u ||
         !MdoConfigUnsigned(xrtValueObjectGet(pSearch,
            MdoConfigKey("max_results")), &WebMaxResults) ||
         WebMaxResults == 0u || WebMaxResults > 20u ||
         xrtValueType(pTransport) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pTransport,
            MdoConfigKey("ca_pem_path")), &Text) ||
         Text.Size >= 512u ||
         (Text.Size != 0u && !MdoConfigHomeRelativePath(Text)) ||
         !MdoConfigProxyValidate(pProxy) ||
         xrtValueType(pWorkspace) != XVALUE_OBJECT ||
         !MdoConfigStringOneOf(xrtValueObjectGet(pWorkspace,
            MdoConfigKey("open_mode")), OpenModes,
            sizeof(OpenModes) / sizeof(OpenModes[0])) ||
         !MdoConfigBool(pWorkspace, "confirm_external_write") ) goto invalid;
    return true;

invalid:
    MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SCHEMA,
        "settings do not satisfy schema version 1");
    return false;
}

static bool MdoConfigModelProtocol(xstrview Protocol, cstr* pEndpoint)
{
    if ( MdoConfigViewEqual(Protocol, "openai-chat-completions") ) {
        *pEndpoint = "chat_completions";
        return true;
    }
    if ( MdoConfigViewEqual(Protocol, "openai-responses") ) {
        *pEndpoint = "responses";
        return true;
    }
    if ( MdoConfigViewEqual(Protocol, "anthropic-messages") ) {
        *pEndpoint = "anthropic_messages";
        return true;
    }
    return false;
}

static bool MdoConfigArrayContainsString(const xvalue* pArray,
    xstrview Expected)
{
    size_t i;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ) return false;
    for ( i = 0u; i < xrtValueCount(pArray); ++i ) {
        xstrview Text;
        if ( MdoConfigString(xrtValueArrayGet(pArray, i), &Text) &&
             Text.Size == Expected.Size &&
             memcmp(Text.Data, Expected.Data, Text.Size) == 0 ) return true;
    }
    return false;
}

static bool MdoConfigStringListValidate(const xvalue* pArray,
    const char* const* ppChoices, size_t iChoices, size_t iMinimum,
    size_t iMaximum, uint64* pMask)
{
    uint64 Mask = 0u;
    size_t i;
    size_t j;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ||
         xrtValueCount(pArray) < iMinimum ||
         xrtValueCount(pArray) > iMaximum || iChoices > 64u ) return false;
    for ( i = 0u; i < xrtValueCount(pArray); ++i ) {
        xstrview Text;
        bool Found = false;
        if ( !MdoConfigString(xrtValueArrayGet(pArray, i), &Text) )
            return false;
        for ( j = 0u; j < iChoices; ++j ) {
            uint64 Bit = (uint64)1u << j;
            if ( !MdoConfigViewEqual(Text, ppChoices[j]) ) continue;
            if ( (Mask & Bit) != 0u ) return false;
            Mask |= Bit;
            Found = true;
            break;
        }
        if ( !Found ) return false;
    }
    if ( pMask != NULL ) *pMask = Mask;
    return true;
}

static const xvalue* MdoConfigFindById(const xvalue* pArray,
    xstrview Expected)
{
    size_t i;
    if ( xrtValueType(pArray) != XVALUE_ARRAY ) return NULL;
    for ( i = 0u; i < xrtValueCount(pArray); ++i ) {
        const xvalue* pItem = xrtValueArrayGet(pArray, i);
        xstrview Id;
        if ( xrtValueType(pItem) == XVALUE_OBJECT &&
             MdoConfigString(xrtValueObjectGet(pItem, MdoConfigKey("id")),
                &Id) && Id.Size == Expected.Size &&
             memcmp(Id.Data, Expected.Data, Id.Size) == 0 ) return pItem;
    }
    return NULL;
}

static bool MdoConfigProviderValidate(const xvalue* pProvider,
    xstrview* pId)
{
    static const char* const EndpointNames[] = {
        "chat_completions", "responses", "anthropic_messages"
    };
    const xvalue* pEndpoints;
    const xvalue* pCredential;
    xstrview Text;
    uint64 Timeout;
    size_t i;
    size_t iEndpoints = 0u;

    if ( xrtValueType(pProvider) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pProvider, MdoConfigKey("id")),
            pId) || pId->Size == 0u || pId->Size > 128u ||
         !MdoConfigString(xrtValueObjectGet(pProvider, MdoConfigKey("name")),
            &Text) || Text.Size == 0u || Text.Size > 256u ||
         !MdoConfigBool(pProvider, "builtin") ||
         !MdoConfigBool(pProvider, "editable") ||
         !MdoConfigBool(pProvider, "removable") ||
         !MdoConfigBool(pProvider, "verify_peer") ||
         !MdoConfigUnsigned(xrtValueObjectGet(pProvider,
            MdoConfigKey("timeout_ms")), &Timeout) ||
         Timeout == 0u || Timeout > 600000u ) return false;
    pEndpoints = xrtValueObjectGet(pProvider, MdoConfigKey("endpoints"));
    if ( xrtValueType(pEndpoints) != XVALUE_OBJECT ) return false;
    for ( i = 0u; i < sizeof(EndpointNames) / sizeof(EndpointNames[0]); ++i ) {
        const xvalue* pEndpoint = xrtValueObjectGet(pEndpoints,
            MdoConfigKey(EndpointNames[i]));
        if ( pEndpoint == NULL ) continue;
        if ( !MdoConfigString(pEndpoint, &Text) || Text.Size == 0u ||
             Text.Size > 2048u ) return false;
        ++iEndpoints;
    }
    if ( iEndpoints == 0u || xrtValueCount(pEndpoints) != iEndpoints )
        return false;
    pCredential = xrtValueObjectGet(pProvider, MdoConfigKey("credential"));
    if ( pCredential != NULL &&
         (xrtValueType(pCredential) != XVALUE_OBJECT ||
          !MdoConfigSecretReferenceValid(xrtValueObjectGet(pCredential,
            MdoConfigKey("secret_ref")))) ) return false;
    return true;
}

static bool MdoConfigModelItemValidate(const xvalue* pItem,
    const xvalue* pProviders, xstrview* pId)
{
    static const char* const Capabilities[] = {
        "text-input", "tool-result-input", "text-output", "json-output",
        "tool-call-output", "reasoning-output", "streaming",
        "reasoning-control", "parallel-tool-calls",
        "max-completion-tokens", "developer-role", "media-input"
    };
    static const char* const Efforts[] = {
        "none", "minimal", "low", "medium", "high", "xhigh", "max"
    };
    static const char* const Attachments[] = { "image", "audio", "file" };
    const xvalue* pProtocols;
    const xvalue* pCapabilities;
    const xvalue* pWindow;
    const xvalue* pEfforts;
    const xvalue* pAttachments;
    const xvalue* pProvider;
    const xvalue* pEndpoints;
    xstrview ProviderId;
    xstrview DefaultProtocol;
    xstrview DefaultEffort;
    xstrview Text;
    uint64 CapabilityMask;
    uint64 Context;
    uint64 MaxInput;
    uint64 MaxOutput;
    uint64 Reserve;
    uint64 Summary;
    size_t i;
    size_t j;

    if ( xrtValueType(pItem) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pItem, MdoConfigKey("id")),
            pId) || pId->Size == 0u || pId->Size > 128u ||
         !MdoConfigString(xrtValueObjectGet(pItem, MdoConfigKey("name")),
            &Text) || Text.Size == 0u || Text.Size > 256u ||
         !MdoConfigString(xrtValueObjectGet(pItem, MdoConfigKey("provider")),
            &ProviderId) || ProviderId.Size == 0u || ProviderId.Size > 128u ||
         !MdoConfigString(xrtValueObjectGet(pItem,
            MdoConfigKey("wire_model")), &Text) ||
         Text.Size == 0u || Text.Size > 256u ||
         !MdoConfigBool(pItem, "builtin") ||
         !MdoConfigBool(pItem, "free") ||
         !MdoConfigBool(pItem, "editable") ||
         !MdoConfigBool(pItem, "removable") ) return false;
    pProvider = MdoConfigFindById(pProviders, ProviderId);
    if ( pProvider == NULL ) return false;
    pEndpoints = xrtValueObjectGet(pProvider, MdoConfigKey("endpoints"));
    pProtocols = xrtValueObjectGet(pItem, MdoConfigKey("protocols"));
    if ( xrtValueType(pProtocols) != XVALUE_ARRAY ||
         xrtValueCount(pProtocols) == 0u || xrtValueCount(pProtocols) > 3u ||
         !MdoConfigString(xrtValueObjectGet(pItem,
            MdoConfigKey("default_protocol")), &DefaultProtocol) ||
         !MdoConfigArrayContainsString(pProtocols, DefaultProtocol) ) return false;
    for ( i = 0u; i < xrtValueCount(pProtocols); ++i ) {
        const xvalue* pProtocol = xrtValueArrayGet(pProtocols, i);
        cstr sEndpoint;
        if ( !MdoConfigString(pProtocol, &Text) ||
             !MdoConfigModelProtocol(Text, &sEndpoint) ||
             !MdoConfigString(xrtValueObjectGet(pEndpoints,
                MdoConfigKey(sEndpoint)), &Text) || Text.Size == 0u ) return false;
        for ( j = 0u; j < i; ++j )
            if ( xrtValueEqual(pProtocol, xrtValueArrayGet(pProtocols, j)) )
                return false;
    }
    pCapabilities = xrtValueObjectGet(pItem, MdoConfigKey("capabilities"));
    if ( !MdoConfigStringListValidate(pCapabilities, Capabilities,
            sizeof(Capabilities) / sizeof(Capabilities[0]), 3u,
            sizeof(Capabilities) / sizeof(Capabilities[0]),
            &CapabilityMask) ||
         (CapabilityMask & ((uint64)1u << 0)) == 0u ||
         (CapabilityMask & ((uint64)1u << 2)) == 0u ||
         (CapabilityMask & ((uint64)1u << 6)) == 0u ||
         ((CapabilityMask & ((uint64)1u << 8)) != 0u &&
          (CapabilityMask & ((uint64)1u << 4)) == 0u) ) return false;
    pWindow = xrtValueObjectGet(pItem, MdoConfigKey("window"));
    if ( xrtValueType(pWindow) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pWindow, MdoConfigKey("mode")),
            &Text) ||
         (!MdoConfigViewEqual(Text, "shared-context") &&
          !MdoConfigViewEqual(Text, "split-input-output")) ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWindow,
            MdoConfigKey("context_tokens")), &Context) || Context == 0u ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWindow,
            MdoConfigKey("max_input_tokens")), &MaxInput) ||
         MaxInput == 0u || MaxInput > Context ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWindow,
            MdoConfigKey("max_output_tokens")), &MaxOutput) ||
         MaxOutput == 0u || MaxOutput > Context || MaxOutput > UINT32_MAX ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWindow,
            MdoConfigKey("output_reserve_tokens")), &Reserve) ||
         Reserve > MaxOutput || Reserve > UINT32_MAX ||
         !MdoConfigUnsigned(xrtValueObjectGet(pWindow,
            MdoConfigKey("summary_tokens")), &Summary) ||
         Summary > MaxOutput || Summary > UINT32_MAX ) return false;
    pEfforts = xrtValueObjectGet(pItem, MdoConfigKey("reasoning_efforts"));
    if ( !MdoConfigStringListValidate(pEfforts, Efforts,
            sizeof(Efforts) / sizeof(Efforts[0]), 1u,
            sizeof(Efforts) / sizeof(Efforts[0]), NULL) ||
         !MdoConfigString(xrtValueObjectGet(pItem,
            MdoConfigKey("default_reasoning_effort")), &DefaultEffort) ||
         !MdoConfigArrayContainsString(pEfforts, DefaultEffort) ) return false;
    pAttachments = xrtValueObjectGet(pItem, MdoConfigKey("attachments"));
    if ( !MdoConfigStringListValidate(pAttachments, Attachments,
            sizeof(Attachments) / sizeof(Attachments[0]), 0u,
            sizeof(Attachments) / sizeof(Attachments[0]), NULL) ) return false;
    return true;
}

static bool MdoConfigModelsValidate(const xvalue* pModels,
    const xvalue* pDefaultModels)
{
    const xvalue* pProviders;
    const xvalue* pItems;
    const xvalue* pDefaultProviders;
    const xvalue* pDefaultItems;
    const xvalue* pProtectedProvider;
    const xvalue* pProtectedModel;
    xstrview DefaultId;
    size_t i;
    size_t j;
    bool bDefaultFound = false;
    bool bProtectedProviderFound = false;
    bool bProtectedModelFound = false;

    if ( xrtValueType(pModels) != XVALUE_OBJECT ||
         xrtValueType(pDefaultModels) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pModels,
            MdoConfigKey("default_model")), &DefaultId) ||
         DefaultId.Size == 0u || DefaultId.Size > 128u ) goto invalid;
    pProviders = xrtValueObjectGet(pModels, MdoConfigKey("providers"));
    pItems = xrtValueObjectGet(pModels, MdoConfigKey("items"));
    pDefaultProviders = xrtValueObjectGet(pDefaultModels,
        MdoConfigKey("providers"));
    pDefaultItems = xrtValueObjectGet(pDefaultModels, MdoConfigKey("items"));
    if ( xrtValueType(pProviders) != XVALUE_ARRAY ||
         xrtValueCount(pProviders) > 128u ||
         xrtValueType(pItems) != XVALUE_ARRAY ||
         xrtValueCount(pItems) > 512u ||
         xrtValueType(pDefaultProviders) != XVALUE_ARRAY ||
         xrtValueType(pDefaultItems) != XVALUE_ARRAY ) goto invalid;
    pProtectedProvider = MdoConfigFindById(pDefaultProviders,
        xrtStrView("ling"));
    pProtectedModel = MdoConfigFindById(pDefaultItems,
        xrtStrView("ling-3.0-tiny"));
    if ( pProtectedProvider == NULL || pProtectedModel == NULL ) goto invalid;
    for ( i = 0u; i < xrtValueCount(pProviders); ++i ) {
        const xvalue* pProvider = xrtValueArrayGet(pProviders, i);
        xstrview Id;
        if ( !MdoConfigProviderValidate(pProvider, &Id) ) goto invalid;
        if ( MdoConfigViewEqual(Id, "ling") ) {
            if ( bProtectedProviderFound ||
                 !xrtValueEqual(pProvider, pProtectedProvider) ) goto protected;
            bProtectedProviderFound = true;
        }
        for ( j = 0u; j < i; ++j ) {
            const xvalue* pEarlier = xrtValueArrayGet(pProviders, j);
            xstrview EarlierId;
            if ( MdoConfigString(xrtValueObjectGet(pEarlier,
                    MdoConfigKey("id")), &EarlierId) &&
                 EarlierId.Size == Id.Size &&
                 memcmp(EarlierId.Data, Id.Data, Id.Size) == 0 ) goto invalid;
        }
    }
    for ( i = 0u; i < xrtValueCount(pItems); ++i ) {
        const xvalue* pItem = xrtValueArrayGet(pItems, i);
        xstrview Id;
        if ( !MdoConfigModelItemValidate(pItem, pProviders, &Id) ) goto invalid;
        if ( Id.Size == DefaultId.Size &&
             memcmp(Id.Data, DefaultId.Data, Id.Size) == 0 ) bDefaultFound = true;
        if ( MdoConfigViewEqual(Id, "ling-3.0-tiny") ) {
            if ( bProtectedModelFound ||
                 !xrtValueEqual(pItem, pProtectedModel) ) goto protected;
            bProtectedModelFound = true;
        }
        for ( j = 0u; j < i; ++j ) {
            const xvalue* pEarlier = xrtValueArrayGet(pItems, j);
            xstrview EarlierId;
            if ( MdoConfigString(xrtValueObjectGet(pEarlier,
                    MdoConfigKey("id")), &EarlierId) &&
                 EarlierId.Size == Id.Size &&
                 memcmp(EarlierId.Data, Id.Data, Id.Size) == 0 ) goto invalid;
        }
    }
    if ( !bProtectedProviderFound || !bProtectedModelFound ) goto protected;
    if ( !bDefaultFound ) goto invalid;
    return true;

protected:
    MdoConfigErrorSet(XERR_PERMISSION, MDO_CONFIG_ERROR_PROTECTED,
        "Ling 3.0 Tiny is built-in; its provider and model cannot be edited or removed");
    return false;

invalid:
    MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SCHEMA,
        "models do not satisfy schema version 1");
    return false;
}

static bool MdoConfigPermissionsValidate(const xvalue* pPermissions)
{
    const xvalue* pProfiles;
    xstrview DefaultProfile;

    if ( xrtValueType(pPermissions) != XVALUE_OBJECT ||
         !MdoConfigString(xrtValueObjectGet(pPermissions,
            MdoConfigKey("default_profile")), &DefaultProfile) ) goto invalid;
    pProfiles = xrtValueObjectGet(pPermissions, MdoConfigKey("profiles"));
    if ( xrtValueType(pProfiles) != XVALUE_OBJECT ||
         !xrtValueObjectHas(pProfiles, DefaultProfile) ) goto invalid;
    return true;

invalid:
    MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SCHEMA,
        "permissions do not satisfy schema version 1");
    return false;
}

static bool MdoConfigEffectiveValidate(const xvalue* pRoot,
    const xvalue* pDefaults)
{
    const xvalue* pSettings;
    const xvalue* pModels;
    const xvalue* pPermissions;
    const xvalue* pDefaultModels;

    if ( !MdoConfigSchemaVersion(pRoot) ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SCHEMA,
            "configuration schema_version must be 1");
        return false;
    }
    pSettings = xrtValueObjectGet(pRoot, MdoConfigKey("settings"));
    pModels = xrtValueObjectGet(pRoot, MdoConfigKey("models"));
    pPermissions = xrtValueObjectGet(pRoot, MdoConfigKey("permissions"));
    pDefaultModels = xrtValueObjectGet(pDefaults, MdoConfigKey("models"));
    return MdoConfigSecretsValidate(pRoot) &&
        MdoConfigSettingsValidate(pSettings) &&
        MdoConfigModelsValidate(pModels, pDefaultModels) &&
        MdoConfigPermissionsValidate(pPermissions);
}

static xvalue* MdoConfigParseObject(xstrview Text, cstr What)
{
    xjsonreadconfig Config;
    xvalue* pValue;

    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_CONFIG_MAX_FILE;
    Config.MaxDepth = 64u;
    Config.MaxValues = 16384u;
    Config.MaxContainerItems = 8192u;
    pValue = xrtJsonRead(Text, &Config);
    if ( pValue == NULL ) return NULL;
    if ( xrtValueType(pValue) != XVALUE_OBJECT ) {
        xrtValueRelease(pValue);
        MdoConfigErrorSet(XERR_TYPE, MDO_CONFIG_ERROR_PARSE, What);
        return NULL;
    }
    return pValue;
}

static bool MdoConfigEnvelopeValidate(const xvalue* pDocument)
{
    xvalueiter Iterator;
    xvaluekey Key;
    xvalue* pValue;
    xvalueiterresult Result;

    if ( !MdoConfigSchemaVersion(pDocument) ||
         xrtValueType(xrtValueObjectGet(pDocument,
            MdoConfigKey("patch"))) != XVALUE_OBJECT ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SCHEMA,
            "user configuration must contain schema_version 1 and patch object");
        return false;
    }
    memset(&Iterator, 0, sizeof(Iterator));
    if ( !xrtValueIterBegin(pDocument, &Iterator) ) return false;
    for ( ; ; ) {
        Result = xrtValueIterAdvance(&Iterator, &Key, &pValue);
        if ( Result == XVALUE_ITER_END ) break;
        if ( Result == XVALUE_ITER_ERROR ) {
            xrtValueIterEnd(&Iterator);
            return false;
        }
        if ( !MdoConfigViewEqual(Key.String, "schema_version") &&
             !MdoConfigViewEqual(Key.String, "patch") ) {
            xrtValueIterEnd(&Iterator);
            MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_SCHEMA,
                "unknown user configuration envelope key");
            return false;
        }
    }
    xrtValueIterEnd(&Iterator);
    return true;
}

static bool MdoConfigReadAll(xfile File, char** ppText, size_t* pSize)
{
    xfileinfo Info;
    char* sText;
    size_t iSize;

    if ( !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_CONFIG_MAX_FILE || Info.Size > SIZE_MAX - 1u ) {
        MdoConfigErrorSet(XERR_RANGE, MDO_CONFIG_ERROR_STORAGE,
            "configuration file exceeds its size limit");
        return false;
    }
    iSize = (size_t)Info.Size;
    sText = (char*)xrtMalloc(iSize + 1u);
    if ( sText == NULL ) return false;
    if ( iSize != 0u && !xrtReadFull(File, sText, iSize, NULL) ) {
        xrtFree(sText);
        return false;
    }
    sText[iSize] = '\0';
    *ppText = sText;
    *pSize = iSize;
    return true;
}

static xvalue* MdoConfigNormalizePatch(MdoConfigDomain Domain,
    const xvalue* pPatch)
{
    const xvalue* pBase = xrtValueObjectGet(g_MdoConfig.Defaults,
        MdoConfigKey(g_MdoConfigDomainName[Domain]));
    xvalue* pCandidate = xrtValueDeepClone(pBase);
    xvalue* pNormalized = NULL;
    bool bEqual = false;

    if ( pCandidate == NULL || !MdoConfigMergeObject(pCandidate, pPatch) ||
         !MdoConfigDiff(pBase, pCandidate, &pNormalized, &bEqual) ) {
        xrtValueRelease(pCandidate);
        xrtValueRelease(pNormalized);
        return NULL;
    }
    xrtValueRelease(pCandidate);
    if ( bEqual ) return MdoConfigObject();
    return pNormalized;
}

static xvalue* MdoConfigBuildEffective(xvalue* const* ppPatches)
{
    xvalue* pRoot = xrtValueDeepClone(g_MdoConfig.Defaults);
    size_t i;

    if ( pRoot == NULL ) return NULL;
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ ) {
        xvalue* pSection = xrtValueDeepClone(xrtValueObjectGet(
            g_MdoConfig.Defaults, MdoConfigKey(g_MdoConfigDomainName[i])));
        if ( pSection == NULL ||
             !MdoConfigMergeObject(pSection, ppPatches[i]) ||
             !xrtValueObjectSetTake(pRoot,
                MdoConfigKey(g_MdoConfigDomainName[i]), &pSection) ) {
            xrtValueRelease(pSection);
            xrtValueRelease(pRoot);
            return NULL;
        }
    }
    if ( g_MdoConfig.Runtime != NULL &&
         !MdoConfigMergeObject(pRoot, g_MdoConfig.Runtime) ) {
        xrtValueRelease(pRoot);
        return NULL;
    }
    if ( !MdoConfigEffectiveValidate(pRoot, g_MdoConfig.Defaults) ) {
        xrtValueRelease(pRoot);
        return NULL;
    }
    return pRoot;
}

static bool MdoConfigEffectiveSize(const xvalue* pEffective, size_t* pSize)
{
    str sJson = xrtJsonStringify(pEffective, false, pSize);
    if ( sJson == NULL ) return false;
    xrtFree(sJson);
    return true;
}

static xvalue* MdoConfigLoadPatch(MdoConfigDomain Domain)
{
    xfile File = MdoHomeOpenRead(g_MdoConfigDomainPath[Domain]);
    char* sText = NULL;
    size_t iSize = 0u;
    xvalue* pDocument = NULL;
    xvalue* pPatch = NULL;
    bool bOk = false;

    if ( File == NULL ) {
        if ( xrtGetError() != NULL &&
             xrtErrorKind(xrtGetError()) == XERR_NOT_FOUND ) {
            xrtClearError();
            return MdoConfigObject();
        }
        return NULL;
    }
    if ( !MdoConfigReadAll(File, &sText, &iSize) ) goto done;
    if ( !xrtClose(File) ) {
        File = NULL;
        goto done;
    }
    File = NULL;
    pDocument = MdoConfigParseObject(
        (xstrview){ sText, iSize }, "user configuration must be a JSON object");
    if ( pDocument == NULL || !MdoConfigEnvelopeValidate(pDocument) ) goto done;
    pPatch = MdoConfigNormalizePatch(Domain,
        xrtValueObjectGet(pDocument, MdoConfigKey("patch")));
    if ( pPatch == NULL ) goto done;
    bOk = true;

done:
    if ( File != NULL ) (void)xrtClose(File);
    xrtFree(sText);
    xrtValueRelease(pDocument);
    if ( !bOk ) {
        xrtValueRelease(pPatch);
        pPatch = NULL;
    }
    return pPatch;
}

static bool MdoConfigRuntimeMergeText(xvalue* pRuntime, cstr Text)
{
    xvalue* pPatch = MdoConfigParseObject(xrtStrView(Text),
        "runtime override must be a JSON object");
    bool bOk;

    if ( pPatch == NULL ) return false;
    bOk = MdoConfigMergeObject(pRuntime, pPatch);
    xrtValueRelease(pPatch);
    return bOk;
}

static xvalue* MdoConfigLoadRuntime(bool* pPresent)
{
    xvalue* pRuntime = MdoConfigObject();
    char* sEnvironment = NULL;
    uint32 i;

    *pPresent = false;
    if ( pRuntime == NULL ) return NULL;
    if ( !xrtEnvLookup("MDO_CONFIG_OVERRIDE", &sEnvironment) ) goto fail;
    if ( sEnvironment != NULL && sEnvironment[0] != '\0' ) {
        if ( !MdoConfigRuntimeMergeText(pRuntime, sEnvironment) ) goto fail;
        *pPresent = true;
    }
    xrtFree(sEnvironment);
    sEnvironment = NULL;
    for ( i = 0u; i < xsAppArgumentCount(); i++ ) {
        cstr sArgument = xsAppArgument(i);
        cstr sValue = NULL;
        if ( strcmp(sArgument, "--config-override") == 0 ) {
            if ( ++i >= xsAppArgumentCount() ) {
                MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
                    "--config-override requires a JSON object");
                goto fail;
            }
            sValue = xsAppArgument(i);
        } else if ( strncmp(sArgument, "--config-override=", 18u) == 0 ) {
            sValue = sArgument + 18u;
        }
        if ( sValue != NULL ) {
            if ( sValue[0] == '\0' ||
                 !MdoConfigRuntimeMergeText(pRuntime, sValue) ) goto fail;
            *pPresent = true;
        }
    }
    return pRuntime;

fail:
    xrtFree(sEnvironment);
    xrtValueRelease(pRuntime);
    return NULL;
}

static xvalue* MdoConfigDocumentCreate(const xvalue* pPatch)
{
    xvalue* pDocument = MdoConfigObject();
    xvalue* pVersion = xrtValueUInt(MDO_CONFIG_SCHEMA_VERSION);
    xvalue* pCopy = xrtValueDeepClone(pPatch);

    if ( pDocument == NULL || pVersion == NULL || pCopy == NULL ||
         !xrtValueObjectSetTake(pDocument, MdoConfigKey("schema_version"),
            &pVersion) ||
         !xrtValueObjectSetTake(pDocument, MdoConfigKey("patch"), &pCopy) ) {
        xrtValueRelease(pVersion);
        xrtValueRelease(pCopy);
        xrtValueRelease(pDocument);
        return NULL;
    }
    return pDocument;
}

static str MdoConfigDocumentString(const xvalue* pPatch, size_t* pSize)
{
    xvalue* pDocument = MdoConfigDocumentCreate(pPatch);
    str sJson;

    if ( pDocument == NULL ) return NULL;
    sJson = xrtJsonStringify(pDocument, true, pSize);
    xrtValueRelease(pDocument);
    return sJson;
}

static void MdoConfigPreviewError(MdoConfigPreview* pPreview)
{
    const xerror* pError = xrtGetError();
    cstr sMessage = pError != NULL ? xrtErrorMessage(pError) : NULL;

    if ( pPreview == NULL || pPreview->Size < sizeof(*pPreview) ) return;
    pPreview->Valid = false;
    pPreview->Changes = false;
    pPreview->PatchBytes = 0u;
    snprintf(pPreview->Message, sizeof(pPreview->Message), "%s",
        sMessage != NULL ? sMessage : "configuration validation failed");
}

static bool MdoConfigPrepareImportLocked(MdoConfigDomain Domain,
    xstrview Document, xvalue** ppPatch, xvalue** ppEffective,
    size_t* pPatchBytes)
{
    xvalue* pDocument = NULL;
    xvalue* pPatch = NULL;
    xvalue* pEffective = NULL;
    xvalue* arrPatches[MDO_CONFIG_DOMAIN_COUNT];
    str sSerialized = NULL;
    size_t i;

    pDocument = MdoConfigParseObject(Document,
        "user configuration must be a JSON object");
    if ( pDocument == NULL || !MdoConfigEnvelopeValidate(pDocument) ) goto fail;
    pPatch = MdoConfigNormalizePatch(Domain,
        xrtValueObjectGet(pDocument, MdoConfigKey("patch")));
    if ( pPatch == NULL ) goto fail;
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ )
        arrPatches[i] = i == (size_t)Domain ? pPatch : g_MdoConfig.Patch[i];
    pEffective = MdoConfigBuildEffective(arrPatches);
    if ( pEffective == NULL ) goto fail;
    sSerialized = MdoConfigDocumentString(pPatch, pPatchBytes);
    if ( sSerialized == NULL ) goto fail;
    xrtFree(sSerialized);
    xrtValueRelease(pDocument);
    *ppPatch = pPatch;
    *ppEffective = pEffective;
    return true;

fail:
    xrtFree(sSerialized);
    xrtValueRelease(pEffective);
    xrtValueRelease(pPatch);
    xrtValueRelease(pDocument);
    return false;
}

static void MdoConfigPublishLocked(MdoConfigDomain Domain, xvalue* pPatch,
    xvalue* pEffective, size_t iEffectiveBytes)
{
    xvalue* pOldPatch;
    xvalue* pOldEffective;

    pOldPatch = g_MdoConfig.Patch[Domain];
    pOldEffective = g_MdoConfig.Effective;
    g_MdoConfig.Patch[Domain] = pPatch;
    g_MdoConfig.Effective = pEffective;
    g_MdoConfig.EffectiveBytes = iEffectiveBytes;
    g_MdoConfig.Revision++;
    xrtValueRelease(pOldPatch);
    xrtValueRelease(pOldEffective);
}

bool MdoConfigInit(void)
{
    xstrview DefaultsText;
    size_t i;

    if ( g_MdoConfig.Initialized ) return true;
    memset(&g_MdoConfig, 0, sizeof(g_MdoConfig));
    g_MdoConfig.Lock = xrtMutexCreate();
    if ( g_MdoConfig.Lock == NULL || !MdoBuiltinDefaults(&DefaultsText) )
        goto fail;
    g_MdoConfig.Defaults = MdoConfigParseObject(DefaultsText,
        "built-in defaults must be a JSON object");
    if ( g_MdoConfig.Defaults == NULL ||
         !MdoConfigEffectiveValidate(g_MdoConfig.Defaults,
            g_MdoConfig.Defaults) ) goto fail;
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ ) {
        g_MdoConfig.Patch[i] = MdoConfigLoadPatch((MdoConfigDomain)i);
        if ( g_MdoConfig.Patch[i] == NULL ) goto fail;
    }
    g_MdoConfig.Runtime = MdoConfigLoadRuntime(
        &g_MdoConfig.RuntimeOverride);
    if ( g_MdoConfig.Runtime == NULL ) goto fail;
    g_MdoConfig.Effective = MdoConfigBuildEffective(g_MdoConfig.Patch);
    if ( g_MdoConfig.Effective == NULL ||
         !MdoConfigEffectiveSize(g_MdoConfig.Effective,
            &g_MdoConfig.EffectiveBytes) ) goto fail;
    g_MdoConfig.Revision = 1u;
    g_MdoConfig.Initialized = true;
    return true;

fail:
    {
        xerror* pSaved = xrtTakeError();
        MdoConfigUnit();
        xrtClearError();
        if ( pSaved != NULL ) xrtSetErrorTake(pSaved);
        else MdoConfigErrorSet(XERR_STATE, MDO_CONFIG_ERROR_STATE,
            "configuration initialization failed");
    }
    return false;
}

void MdoConfigUnit(void)
{
    size_t i;

    xrtValueRelease(g_MdoConfig.Effective);
    xrtValueRelease(g_MdoConfig.Runtime);
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ )
        xrtValueRelease(g_MdoConfig.Patch[i]);
    xrtValueRelease(g_MdoConfig.Defaults);
    if ( g_MdoConfig.Lock != NULL ) xrtMutexDestroy(g_MdoConfig.Lock);
    memset(&g_MdoConfig, 0, sizeof(g_MdoConfig));
}

bool MdoConfigGetSnapshot(MdoConfigSnapshot* pSnapshot)
{
    size_t i;

    if ( pSnapshot == NULL || pSnapshot->Size < sizeof(*pSnapshot) ||
         !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid configuration snapshot request");
        return false;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    pSnapshot->SchemaVersion = MDO_CONFIG_SCHEMA_VERSION;
    pSnapshot->Revision = g_MdoConfig.Revision;
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ )
        pSnapshot->UserPatch[i] = xrtValueCount(g_MdoConfig.Patch[i]) != 0u;
    pSnapshot->RuntimeOverride = g_MdoConfig.RuntimeOverride;
    pSnapshot->EffectiveBytes = g_MdoConfig.EffectiveBytes;
    xrtMutexUnlock(g_MdoConfig.Lock);
    return true;
}

bool MdoConfigGetPowerSettings(MdoConfigPowerSettings* pSettings)
{
    const xvalue* Settings;
    const xvalue* Power;
    bool PreventSleep;
    uint32 Size;
    bool Ok = false;
    if ( pSettings == NULL || pSettings->Size < sizeof(*pSettings) ||
         !g_MdoConfig.Initialized ) return false;
    Size = pSettings->Size;
    xrtMutexLock(g_MdoConfig.Lock);
    Settings = xrtValueObjectGet(g_MdoConfig.Effective,
        MdoConfigKey("settings"));
    Power = Settings != NULL ? xrtValueObjectGet(Settings,
        MdoConfigKey("power")) : NULL;
    if ( Power != NULL && MdoConfigBool(Power, "prevent_sleep") &&
         xrtValueGetBool(xrtValueObjectGet(Power,
            MdoConfigKey("prevent_sleep")), &PreventSleep) ) {
        memset(pSettings, 0, sizeof(*pSettings));
        pSettings->Size = Size;
        pSettings->Revision = g_MdoConfig.Revision;
        pSettings->PreventSleep = PreventSleep;
        Ok = true;
    }
    xrtMutexUnlock(g_MdoConfig.Lock);
    return Ok;
}

bool MdoConfigGetAgentSettings(MdoConfigAgentSettings* pSettings)
{
    const xvalue* pSettingsValue;
    const xvalue* pAgent;
    const xvalue* pPermissions;
    xstrview Reasoning;
    xstrview Permission;
    uint64 MaxTools;
    uint64 MaxSubagents;
    bool MemoryEnabled;
    bool SchedulesEnabled;
    uint32 Size;
    bool Ok = false;

    if ( pSettings == NULL || pSettings->Size < sizeof(*pSettings) ||
         !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid Agent settings request");
        return false;
    }
    Size = pSettings->Size;
    xrtMutexLock(g_MdoConfig.Lock);
    pSettingsValue = xrtValueObjectGet(g_MdoConfig.Effective,
        MdoConfigKey("settings"));
    pAgent = pSettingsValue != NULL ?
        xrtValueObjectGet(pSettingsValue, MdoConfigKey("agent")) : NULL;
    pPermissions = xrtValueObjectGet(g_MdoConfig.Effective,
        MdoConfigKey("permissions"));
    if ( pAgent != NULL && pPermissions != NULL &&
         MdoConfigString(xrtValueObjectGet(pAgent,
            MdoConfigKey("reasoning_effort")), &Reasoning) &&
         MdoConfigString(xrtValueObjectGet(pPermissions,
            MdoConfigKey("default_profile")), &Permission) &&
         xrtValueGetBool(xrtValueObjectGet(pAgent,
            MdoConfigKey("memory")), &MemoryEnabled) &&
         xrtValueGetBool(xrtValueObjectGet(pAgent,
            MdoConfigKey("schedules")), &SchedulesEnabled) &&
         MdoConfigUnsigned(xrtValueObjectGet(pAgent,
            MdoConfigKey("max_parallel_tools")), &MaxTools) &&
         MdoConfigUnsigned(xrtValueObjectGet(pAgent,
            MdoConfigKey("max_parallel_subagents")), &MaxSubagents) &&
         Reasoning.Size < sizeof(pSettings->ReasoningEffort) &&
         Permission.Size < sizeof(pSettings->PermissionProfile) &&
         MaxTools <= UINT32_MAX && MaxSubagents <= UINT32_MAX ) {
        memset(pSettings, 0, sizeof(*pSettings));
        pSettings->Size = Size;
        pSettings->Revision = g_MdoConfig.Revision;
        pSettings->MemoryEnabled = MemoryEnabled;
        pSettings->SchedulesEnabled = SchedulesEnabled;
        pSettings->MaxParallelTools = (uint32)MaxTools;
        pSettings->MaxParallelSubagents = (uint32)MaxSubagents;
        memcpy(pSettings->ReasoningEffort, Reasoning.Data, Reasoning.Size);
        memcpy(pSettings->PermissionProfile, Permission.Data, Permission.Size);
        Ok = true;
    }
    xrtMutexUnlock(g_MdoConfig.Lock);
    if ( !Ok ) MdoConfigErrorSet(XERR_STATE, MDO_CONFIG_ERROR_STATE,
        "effective Agent settings are unavailable");
    return Ok;
}

char* MdoConfigAgentInstructions(void)
{
    const xvalue* pSettings;
    const xvalue* pAgent;
    xstrview Text;
    char* Result = NULL;
    if ( !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_STATE, MDO_CONFIG_ERROR_STATE,
            "Agent instructions are unavailable");
        return NULL;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    pSettings = xrtValueObjectGet(g_MdoConfig.Effective,
        MdoConfigKey("settings"));
    pAgent = pSettings != NULL ? xrtValueObjectGet(pSettings,
        MdoConfigKey("agent")) : NULL;
    if ( pAgent != NULL && MdoConfigString(xrtValueObjectGet(pAgent,
            MdoConfigKey("user_instructions")), &Text) &&
         Text.Size <= 8192u ) {
        Result = (char*)xrtMalloc(Text.Size + 1u);
        if ( Result != NULL ) {
            if ( Text.Size != 0u ) memcpy(Result, Text.Data, Text.Size);
            Result[Text.Size] = '\0';
        }
    }
    xrtMutexUnlock(g_MdoConfig.Lock);
    if ( Result == NULL ) MdoConfigErrorSet(XERR_STATE,
        MDO_CONFIG_ERROR_STATE, "cannot read Agent instructions");
    return Result;
}

bool MdoConfigGetWebSettings(MdoConfigWebSettings* pSettings)
{
    const xvalue* pSettingsValue;
    const xvalue* pAgent;
    const xvalue* pWeb;
    const xvalue* pSearch;
    xstrview Provider;
    xstrview Endpoint;
    xstrview SecretRef;
    uint64 Timeout;
    uint64 IdleTimeout;
    uint64 MaxResponse;
    uint64 MaxText;
    uint64 MaxDocuments;
    uint64 MaxResults;
    uint32 Size;
    bool AgentEnabled;
    bool Enabled;
    bool AllowHttp;
    bool AllowPrivate;
    bool Ok = false;

    if ( pSettings == NULL || pSettings->Size < sizeof(*pSettings) ||
         !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid Web settings request");
        return false;
    }
    Size = pSettings->Size;
    xrtMutexLock(g_MdoConfig.Lock);
    pSettingsValue = xrtValueObjectGet(g_MdoConfig.Effective,
        MdoConfigKey("settings"));
    pAgent = pSettingsValue != NULL ?
        xrtValueObjectGet(pSettingsValue, MdoConfigKey("agent")) : NULL;
    pWeb = pSettingsValue != NULL ?
        xrtValueObjectGet(pSettingsValue, MdoConfigKey("web")) : NULL;
    pSearch = pWeb != NULL ?
        xrtValueObjectGet(pWeb, MdoConfigKey("search")) : NULL;
    if ( pAgent != NULL && pWeb != NULL && pSearch != NULL &&
         xrtValueGetBool(xrtValueObjectGet(pAgent,
            MdoConfigKey("web_search")), &AgentEnabled) &&
         xrtValueGetBool(xrtValueObjectGet(pWeb,
            MdoConfigKey("enabled")), &Enabled) &&
         xrtValueGetBool(xrtValueObjectGet(pWeb,
            MdoConfigKey("allow_http")), &AllowHttp) &&
         xrtValueGetBool(xrtValueObjectGet(pWeb,
            MdoConfigKey("allow_private_networks")), &AllowPrivate) &&
         MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("timeout_ms")), &Timeout) &&
         MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("idle_timeout_ms")), &IdleTimeout) &&
         MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("max_response_bytes")), &MaxResponse) &&
         MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("max_text_bytes")), &MaxText) &&
         MdoConfigUnsigned(xrtValueObjectGet(pWeb,
            MdoConfigKey("max_documents")), &MaxDocuments) &&
         MdoConfigUnsigned(xrtValueObjectGet(pSearch,
            MdoConfigKey("max_results")), &MaxResults) &&
         MdoConfigString(xrtValueObjectGet(pSearch,
            MdoConfigKey("provider")), &Provider) &&
         MdoConfigString(xrtValueObjectGet(pSearch,
            MdoConfigKey("endpoint")), &Endpoint) &&
         MdoConfigString(xrtValueObjectGet(pSearch,
            MdoConfigKey("secret_ref")), &SecretRef) &&
         Provider.Size < sizeof(pSettings->Provider) &&
         Endpoint.Size < sizeof(pSettings->Endpoint) &&
         SecretRef.Size < sizeof(pSettings->SecretRef) &&
         Timeout <= UINT32_MAX && IdleTimeout <= UINT32_MAX &&
         MaxResponse <= SIZE_MAX && MaxText <= SIZE_MAX &&
         MaxDocuments <= SIZE_MAX && MaxResults <= UINT32_MAX ) {
        memset(pSettings, 0, sizeof(*pSettings));
        pSettings->Size = Size;
        pSettings->Revision = g_MdoConfig.Revision;
        pSettings->Enabled = AgentEnabled && Enabled;
        pSettings->AllowHttp = AllowHttp;
        pSettings->AllowPrivateNetworks = AllowPrivate;
        pSettings->TimeoutMilliseconds = (uint32)Timeout;
        pSettings->IdleTimeoutMilliseconds = (uint32)IdleTimeout;
        pSettings->MaxResponseBytes = (size_t)MaxResponse;
        pSettings->MaxTextBytes = (size_t)MaxText;
        pSettings->MaxDocuments = (size_t)MaxDocuments;
        pSettings->MaxResults = (uint32)MaxResults;
        memcpy(pSettings->Provider, Provider.Data, Provider.Size);
        memcpy(pSettings->Endpoint, Endpoint.Data, Endpoint.Size);
        memcpy(pSettings->SecretRef, SecretRef.Data, SecretRef.Size);
        Ok = true;
    }
    xrtMutexUnlock(g_MdoConfig.Lock);
    if ( !Ok ) MdoConfigErrorSet(XERR_STATE, MDO_CONFIG_ERROR_STATE,
        "effective Web settings are unavailable");
    return Ok;
}

bool MdoConfigGetTransportSettings(MdoConfigTransportSettings* pSettings)
{
    const xvalue* Settings;
    const xvalue* Transport;
    const xvalue* Proxy;
    const xvalue* Credential;
    xstrview Path;
    xstrview Kind;
    xstrview Host;
    xstrview User;
    xstrview Bypass;
    xstrview SecretRef = xrtStrView("");
    uint64 Port;
    uint32 Size;
    bool Ok = false;

    if ( pSettings == NULL || pSettings->Size < sizeof(*pSettings) ||
         !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid transport settings request");
        return false;
    }
    Size = pSettings->Size;
    xrtMutexLock(g_MdoConfig.Lock);
    Settings = xrtValueObjectGet(g_MdoConfig.Effective,
        MdoConfigKey("settings"));
    Transport = Settings != NULL ? xrtValueObjectGet(Settings,
        MdoConfigKey("transport")) : NULL;
    Proxy = Transport != NULL ? xrtValueObjectGet(Transport,
        MdoConfigKey("proxy")) : NULL;
    Credential = Proxy != NULL ? xrtValueObjectGet(Proxy,
        MdoConfigKey("credential")) : NULL;
    if ( Transport != NULL && MdoConfigString(xrtValueObjectGet(Transport,
            MdoConfigKey("ca_pem_path")), &Path) &&
         Path.Size < sizeof(pSettings->CaPemPath) &&
         Proxy != NULL && MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("kind")), &Kind) &&
         MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("host")), &Host) &&
         MdoConfigUnsigned(xrtValueObjectGet(Proxy,
            MdoConfigKey("port")), &Port) &&
         MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("user")), &User) &&
         MdoConfigString(xrtValueObjectGet(Proxy,
            MdoConfigKey("bypass")), &Bypass) &&
         (Credential == NULL || xrtValueType(Credential) == XVALUE_NULL ||
          MdoConfigString(xrtValueObjectGet(Credential,
            MdoConfigKey("secret_ref")), &SecretRef)) &&
         Kind.Size < sizeof(pSettings->ProxyKind) &&
         Host.Size < sizeof(pSettings->ProxyHost) &&
         Port <= UINT16_MAX &&
         User.Size < sizeof(pSettings->ProxyUser) &&
         Bypass.Size < sizeof(pSettings->ProxyBypass) &&
         SecretRef.Size < sizeof(pSettings->ProxySecretRef) ) {
        memset(pSettings, 0, sizeof(*pSettings));
        pSettings->Size = Size;
        pSettings->Revision = g_MdoConfig.Revision;
        memcpy(pSettings->CaPemPath, Path.Data, Path.Size);
        memcpy(pSettings->ProxyKind, Kind.Data, Kind.Size);
        memcpy(pSettings->ProxyHost, Host.Data, Host.Size);
        pSettings->ProxyPort = (uint16)Port;
        memcpy(pSettings->ProxyUser, User.Data, User.Size);
        memcpy(pSettings->ProxyBypass, Bypass.Data, Bypass.Size);
        memcpy(pSettings->ProxySecretRef, SecretRef.Data, SecretRef.Size);
        Ok = true;
    }
    xrtMutexUnlock(g_MdoConfig.Lock);
    if ( !Ok ) MdoConfigErrorSet(XERR_STATE, MDO_CONFIG_ERROR_STATE,
        "effective transport settings are unavailable");
    return Ok;
}

str MdoConfigEffectiveJson(size_t* pSize)
{
    str sJson;

    if ( !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_STATE, MDO_CONFIG_ERROR_STATE,
            "configuration is not initialized");
        return NULL;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    sJson = xrtJsonStringify(g_MdoConfig.Effective, true, pSize);
    xrtMutexUnlock(g_MdoConfig.Lock);
    return sJson;
}

str MdoConfigExport(MdoConfigDomain Domain, bool Effective, size_t* pSize)
{
    xvalue* pPatch = NULL;
    bool bEqual = false;
    str sJson = NULL;

    if ( !MdoConfigDomainValid(Domain) || !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid configuration export request");
        return NULL;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    if ( Effective ) {
        const xvalue* pBase = xrtValueObjectGet(g_MdoConfig.Defaults,
            MdoConfigKey(g_MdoConfigDomainName[Domain]));
        const xvalue* pValue = xrtValueObjectGet(g_MdoConfig.Effective,
            MdoConfigKey(g_MdoConfigDomainName[Domain]));
        if ( !MdoConfigDiff(pBase, pValue, &pPatch, &bEqual) ) goto done;
        if ( bEqual ) pPatch = MdoConfigObject();
    } else {
        pPatch = xrtValueDeepClone(g_MdoConfig.Patch[Domain]);
    }
    if ( pPatch != NULL ) sJson = MdoConfigDocumentString(pPatch, pSize);

done:
    xrtValueRelease(pPatch);
    xrtMutexUnlock(g_MdoConfig.Lock);
    return sJson;
}

bool MdoConfigPreviewImport(MdoConfigDomain Domain, xstrview Document,
    MdoConfigPreview* pPreview)
{
    xvalue* pPatch = NULL;
    xvalue* pEffective = NULL;
    size_t iBytes = 0u;
    bool bOk;

    if ( !MdoConfigDomainValid(Domain) || pPreview == NULL ||
         pPreview->Size < sizeof(*pPreview) || !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid configuration preview request");
        MdoConfigPreviewError(pPreview);
        return false;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    bOk = MdoConfigPrepareImportLocked(Domain, Document, &pPatch,
        &pEffective, &iBytes);
    if ( bOk ) {
        pPreview->Valid = true;
        pPreview->Changes = !xrtValueEqual(g_MdoConfig.Patch[Domain], pPatch);
        pPreview->PatchBytes = iBytes;
        snprintf(pPreview->Message, sizeof(pPreview->Message), "%s",
            pPreview->Changes ? "validated change" : "no effective change");
    }
    xrtMutexUnlock(g_MdoConfig.Lock);
    if ( !bOk ) MdoConfigPreviewError(pPreview);
    xrtValueRelease(pPatch);
    xrtValueRelease(pEffective);
    return bOk;
}

bool MdoConfigImport(MdoConfigDomain Domain, xstrview Document)
{
    xvalue* pPatch = NULL;
    xvalue* pEffective = NULL;
    str sSerialized = NULL;
    size_t iBytes = 0u;
    size_t iEffectiveBytes = 0u;
    bool bOk = false;

    if ( !MdoConfigDomainValid(Domain) || !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid configuration import request");
        return false;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    if ( !MdoConfigPrepareImportLocked(Domain, Document, &pPatch,
            &pEffective, &iBytes) ) goto done;
    if ( xrtValueEqual(g_MdoConfig.Patch[Domain], pPatch) ) {
        bOk = true;
        goto done;
    }
    if ( !MdoConfigEffectiveSize(pEffective, &iEffectiveBytes) ) goto done;
    if ( xrtValueCount(pPatch) == 0u ) {
        if ( !MdoHomeRemove(g_MdoConfigDomainPath[Domain], true) ) goto done;
    } else {
        sSerialized = MdoConfigDocumentString(pPatch, &iBytes);
        if ( sSerialized == NULL ||
             !MdoHomeAtomicWrite(g_MdoConfigDomainPath[Domain], sSerialized,
                iBytes, true) ) goto done;
    }
    MdoConfigPublishLocked(Domain, pPatch, pEffective, iEffectiveBytes);
    pPatch = NULL;
    pEffective = NULL;
    bOk = true;

done:
    xrtFree(sSerialized);
    xrtValueRelease(pPatch);
    xrtValueRelease(pEffective);
    xrtMutexUnlock(g_MdoConfig.Lock);
    return bOk;
}

bool MdoConfigPreviewRestore(MdoConfigDomain Domain,
    MdoConfigPreview* pPreview)
{
    xvalue* pEmpty = NULL;
    xvalue* pEffective = NULL;
    xvalue* arrPatches[MDO_CONFIG_DOMAIN_COUNT];
    size_t i;
    bool bOk = false;

    if ( !MdoConfigDomainValid(Domain) || pPreview == NULL ||
         pPreview->Size < sizeof(*pPreview) || !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid configuration restore preview request");
        MdoConfigPreviewError(pPreview);
        return false;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    pEmpty = MdoConfigObject();
    if ( pEmpty == NULL ) goto done;
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ )
        arrPatches[i] = i == (size_t)Domain ? pEmpty : g_MdoConfig.Patch[i];
    pEffective = MdoConfigBuildEffective(arrPatches);
    if ( pEffective == NULL ) goto done;
    pPreview->Valid = true;
    pPreview->Changes = xrtValueCount(g_MdoConfig.Patch[Domain]) != 0u;
    pPreview->PatchBytes = 0u;
    snprintf(pPreview->Message, sizeof(pPreview->Message), "%s",
        pPreview->Changes ? "restore user patch" : "already at defaults");
    bOk = true;

done:
    xrtMutexUnlock(g_MdoConfig.Lock);
    if ( !bOk ) MdoConfigPreviewError(pPreview);
    xrtValueRelease(pEmpty);
    xrtValueRelease(pEffective);
    return bOk;
}

bool MdoConfigRestore(MdoConfigDomain Domain)
{
    xvalue* pEmpty = NULL;
    xvalue* pEffective = NULL;
    xvalue* arrPatches[MDO_CONFIG_DOMAIN_COUNT];
    size_t i;
    size_t iEffectiveBytes = 0u;
    bool bOk = false;

    if ( !MdoConfigDomainValid(Domain) || !g_MdoConfig.Initialized ) {
        MdoConfigErrorSet(XERR_ARGUMENT, MDO_CONFIG_ERROR_ARGUMENT,
            "invalid configuration restore request");
        return false;
    }
    xrtMutexLock(g_MdoConfig.Lock);
    pEmpty = MdoConfigObject();
    if ( pEmpty == NULL ) goto done;
    for ( i = 0u; i < MDO_CONFIG_DOMAIN_COUNT; i++ )
        arrPatches[i] = i == (size_t)Domain ? pEmpty : g_MdoConfig.Patch[i];
    pEffective = MdoConfigBuildEffective(arrPatches);
    if ( pEffective == NULL ) goto done;
    if ( xrtValueCount(g_MdoConfig.Patch[Domain]) == 0u ) {
        bOk = true;
        goto done;
    }
    if ( !MdoConfigEffectiveSize(pEffective, &iEffectiveBytes) ||
         !MdoHomeRemove(g_MdoConfigDomainPath[Domain], true) ) goto done;
    MdoConfigPublishLocked(Domain, pEmpty, pEffective, iEffectiveBytes);
    pEmpty = NULL;
    pEffective = NULL;
    bOk = true;

done:
    xrtValueRelease(pEmpty);
    xrtValueRelease(pEffective);
    xrtMutexUnlock(g_MdoConfig.Lock);
    return bOk;
}
