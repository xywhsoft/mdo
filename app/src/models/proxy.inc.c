/* Proxy policies replace the whole proxy tuple, never its individual fields.
 * CA trust remains global. Missing policy and explicit "global" both inherit
 * global settings; only a model's explicit "provider" uses its provider. */
static const xvalue* MdoModelsSelectedProxy(const MdoProviderEntry* Provider,
    const MdoModelEntry* Model)
{
    const xvalue* Proxy = Model ? Model->Proxy : NULL;
    xstrview Kind;
    if (Proxy && MdoModelsString(xrtValueObjectGet(Proxy, MdoModelsKey("kind")), &Kind) &&
        MdoModelsViewEqual(Kind, "provider")) return Provider ? Provider->Proxy : NULL;
    return Proxy;
}

static void MdoModelsProxyCopy(const xvalue* Proxy, cstr Key, char* Target)
{
    xstrview Text;
    if (MdoModelsString(xrtValueObjectGet(Proxy, MdoModelsKey(Key)), &Text)) {
        memcpy(Target, Text.Data, Text.Size); Target[Text.Size] = 0;
    }
}

bool MdoModelProxySettings(const xvalue* Proxy, MdoConfigTransportSettings* Settings,
    xllm_error* Error)
{
    if (!Settings || Settings->Size < sizeof(*Settings) || !MdoConfigGetTransportSettings(Settings)) {
        MdoModelsProfileError(Error, "model transport settings are unavailable");
        return false;
    }
    if (!MdoConfigModelProxyValid(Proxy, false)) {
        MdoModelProfileErrorInit(Error, "profile_proxy_invalid", "the selected proxy policy is invalid");
        return false;
    }
    if (!Proxy) return true;
    xstrview Kind;
    if (!MdoModelsString(xrtValueObjectGet(Proxy, MdoModelsKey("kind")), &Kind)) return false;
    if (MdoModelsViewEqual(Kind, "global")) return true;
    /* Validation above bounds every copied field, including secret_ref. */
    memset(Settings->ProxyKind, 0, sizeof(Settings->ProxyKind));
    memset(Settings->ProxyHost, 0, sizeof(Settings->ProxyHost));
    memset(Settings->ProxyUser, 0, sizeof(Settings->ProxyUser));
    memset(Settings->ProxyBypass, 0, sizeof(Settings->ProxyBypass));
    memset(Settings->ProxySecretRef, 0, sizeof(Settings->ProxySecretRef));
    Settings->ProxyPort = 0;
    MdoModelsProxyCopy(Proxy, "kind", Settings->ProxyKind);
    MdoModelsProxyCopy(Proxy, "host", Settings->ProxyHost);
    MdoModelsProxyCopy(Proxy, "user", Settings->ProxyUser);
    MdoModelsProxyCopy(Proxy, "bypass", Settings->ProxyBypass);
    uint64 Port = 0;
    (void)MdoModelsUnsigned(xrtValueObjectGet(Proxy, MdoModelsKey("port")), &Port);
    Settings->ProxyPort = (uint16)Port;
    MdoModelsProxyCopy(xrtValueObjectGet(Proxy, MdoModelsKey("credential")), "secret_ref", Settings->ProxySecretRef);
    return true;
}

bool MdoModelUsesSystemProxy(const MdoModelCatalog* Catalog, const char* ModelId)
{
    const MdoModelEntry* Model = NULL;
    if (Catalog) Model = ModelId && ModelId[0] ? MdoModelsLookup(Catalog, ModelId) :
        (Catalog->DefaultModelIndex < Catalog->ModelCount ? &Catalog->Models[Catalog->DefaultModelIndex] : NULL);
    const MdoProviderEntry* Provider = Model ? MdoModelsFindProvider(Catalog, Model->ProviderId) : NULL;
    MdoConfigTransportSettings Settings = {0}; Settings.Size = sizeof(Settings);
    return MdoModelProxySettings(MdoModelsSelectedProxy(Provider, Model), &Settings, NULL) &&
        !strcmp(Settings.ProxyKind, "system");
}
