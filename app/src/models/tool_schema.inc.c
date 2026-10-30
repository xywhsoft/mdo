/* Provider strict mode has a narrower contract than a tool's JSON Schema.
 * Keep optional fields optional: do not invent required/null arguments or
 * rewrite schemas that the local tool validator and executor already use. */
static bool MdoModelsStrictSchema(const xvalue* Schema, unsigned Depth)
{
    if (!Schema || xrtValueType(Schema) != XVALUE_OBJECT || Depth > 48u) return false;
    const xvalue* Type = xrtValueObjectGet(Schema, XRT_STR_LITERAL("type"));
    xstrview Name = {0}; bool Object = false, Array = false;
    if (xrtValueGetString(Type, &Name)) {
        Object = Name.Size == 6u && !memcmp(Name.Data, "object", 6u);
        Array = Name.Size == 5u && !memcmp(Name.Data, "array", 5u);
    } else if (xrtValueType(Type) == XVALUE_ARRAY) {
        for (size_t i = 0u; i < xrtValueCount(Type); ++i) {
            if (!xrtValueGetString(xrtValueArrayGet(Type, i), &Name)) return false;
            Object |= Name.Size == 6u && !memcmp(Name.Data, "object", 6u);
            Array |= Name.Size == 5u && !memcmp(Name.Data, "array", 5u);
        }
    } else if (!xrtValueObjectGet(Schema, XRT_STR_LITERAL("$ref")) &&
        !xrtValueObjectGet(Schema, XRT_STR_LITERAL("anyOf"))) return false;
    if (Object) {
        const xvalue* Properties = xrtValueObjectGet(Schema, XRT_STR_LITERAL("properties"));
        const xvalue* Required = xrtValueObjectGet(Schema, XRT_STR_LITERAL("required"));
        bool Additional = true;
        if (xrtValueType(Properties) != XVALUE_OBJECT || xrtValueType(Required) != XVALUE_ARRAY ||
            xrtValueCount(Properties) != xrtValueCount(Required) ||
            !xrtValueGetBool(xrtValueObjectGet(Schema, XRT_STR_LITERAL("additionalProperties")), &Additional) || Additional) return false;
        for (size_t i = 0u; i < xrtValueCount(Properties); ++i) {
            xstrview Key = {0}; const xvalue* Property = xrtValueObjectAt(Properties, i, &Key); size_t Matches = 0u;
            for (size_t j = 0u; j < xrtValueCount(Required); ++j) {
                xstrview Field = {0};
                if (!xrtValueGetString(xrtValueArrayGet(Required, j), &Field)) return false;
                if (Field.Size == Key.Size && !memcmp(Field.Data, Key.Data, Key.Size)) ++Matches;
            }
            if (Matches != 1u || !MdoModelsStrictSchema(Property, Depth + 1u)) return false;
        }
    }
    if (Array && !MdoModelsStrictSchema(xrtValueObjectGet(Schema, XRT_STR_LITERAL("items")), Depth + 1u)) return false;
    /* A conservative subset. An unsupported strict keyword still keeps its
     * original best-effort schema and local validation; only strict is false. */
    static const char* const Unsupported[] = {"allOf", "oneOf", "not", "if", "then", "else",
        "dependentRequired", "dependentSchemas", "patternProperties", "prefixItems", "default"};
    for (size_t i = 0u; i < sizeof(Unsupported) / sizeof(Unsupported[0]); ++i)
        if (xrtValueObjectGet(Schema, xrtStrView(Unsupported[i]))) return false;
    const xvalue* Any = xrtValueObjectGet(Schema, XRT_STR_LITERAL("anyOf"));
    if (Any) {
        if (xrtValueType(Any) != XVALUE_ARRAY || !xrtValueCount(Any)) return false;
        for (size_t i = 0u; i < xrtValueCount(Any); ++i)
            if (!MdoModelsStrictSchema(xrtValueArrayGet(Any, i), Depth + 1u)) return false;
    }
    const xvalue* Defs = xrtValueObjectGet(Schema, XRT_STR_LITERAL("$defs"));
    if (Defs) {
        if (xrtValueType(Defs) != XVALUE_OBJECT) return false;
        for (size_t i = 0u; i < xrtValueCount(Defs); ++i)
            if (!MdoModelsStrictSchema(xrtValueObjectAt(Defs, i, NULL), Depth + 1u)) return false;
    }
    return true;
}

static bool MdoModelsToolStrictCompatible(cstr Json)
{
    xvalue* Schema = Json ? xrtJsonParse(xrtStrView(Json)) : NULL; xstrview RootType = {0};
    bool Ok = Schema && !xrtValueObjectGet(Schema, XRT_STR_LITERAL("anyOf")) &&
        xrtValueGetString(xrtValueObjectGet(Schema, XRT_STR_LITERAL("type")), &RootType) &&
        RootType.Size == 6u && !memcmp(RootType.Data, "object", 6u) && MdoModelsStrictSchema(Schema, 0u);
    xrtValueRelease(Schema); xrtClearError(); return Ok;
}

/* The SDK omits strict:false. Responses may then normalize optional arguments
 * into strict mode. Use its supported wire seam to make false explicit. The
 * replacement belongs to the SDK's libc allocator, not the XRT allocator. */
static bool MdoModelsStrictBody(xllm_client* Client, xllm_wire* Wire, void* Data)
{
    xllm_model_profile Profile;
    if (!xllmClientGetModelProfile(Client, &Profile) ||
        (Profile.eProvider != XLLM_PROVIDER_OPENAI_COMPAT && Profile.eProvider != XLLM_PROVIDER_OPENAI_RESPONSES)) return true;
    xvalue* Root = xrtJsonParse(xrtStrViewN(Wire->sBody, Wire->iBodySize));
    xvalue* Tools = xrtValueObjectGet(Root, XRT_STR_LITERAL("tools")); bool Ok = Root != NULL, Changed = false;
    for (size_t i = 0u; Ok && i < xrtValueCount(Tools); ++i) {
        xvalue* Tool = xrtValueArrayGet(Tools, i); xstrview Type = {0};
        if (!xrtValueGetString(xrtValueObjectGet(Tool, XRT_STR_LITERAL("type")), &Type) ||
            Type.Size != 8u || memcmp(Type.Data, "function", 8u)) continue;
        if (Profile.eProvider == XLLM_PROVIDER_OPENAI_COMPAT) Tool = xrtValueObjectGet(Tool, XRT_STR_LITERAL("function"));
        xvalue* Strict = xrtValueObjectGet(Tool, XRT_STR_LITERAL("strict")); bool Enabled = false;
        xrtValueGetBool(Strict, &Enabled);
        if (!Enabled && Strict) continue;
        /* Chat Completions already defaults to best effort. Only Responses
         * needs an explicit false when the SDK omitted the caller's flag. */
        if (!Enabled && !Strict && Profile.eProvider == XLLM_PROVIDER_OPENAI_COMPAT) continue;
        const xvalue* Schema = xrtValueObjectGet(Tool, XRT_STR_LITERAL("parameters")); xstrview RootType = {0};
        bool Compatible = !xrtValueObjectGet(Schema, XRT_STR_LITERAL("anyOf")) &&
            xrtValueGetString(xrtValueObjectGet(Schema, XRT_STR_LITERAL("type")), &RootType) &&
            RootType.Size == 6u && !memcmp(RootType.Data, "object", 6u) && MdoModelsStrictSchema(Schema, 0u);
        if (Enabled && Compatible) continue;
        Ok = xrtValueObjectSetNew(Tool, XRT_STR_LITERAL("strict"), xrtValueBool(false)); Changed = true;
    }
    if (Ok && Data) {
        if (xrtValueObjectHas(Root, XRT_STR_LITERAL("reasoning_effort"))) {
            Ok = xrtValueObjectRemove(Root, XRT_STR_LITERAL("reasoning_effort")); Changed = true;
        }
        xvalue* Reasoning = xrtValueObjectGet(Root, XRT_STR_LITERAL("reasoning"));
        if (xrtValueObjectHas(Reasoning, XRT_STR_LITERAL("effort"))) {
            Ok = Ok && xrtValueObjectRemove(Reasoning, XRT_STR_LITERAL("effort")); Changed = true;
        }
    }
    if (Ok && Changed) {
        size_t Size = 0u; char* Json = xrtJsonStringify(Root, false, &Size);
        char* Replacement = Json ? malloc(Size + 1u) : NULL;
        if (Replacement) {
            memcpy(Replacement, Json, Size + 1u); Wire->sBody = Replacement;
            Wire->iBodySize = Size; Wire->iBodyCapacity = Size + 1u;
        } else Ok = false;
        xrtFree(Json);
    }
    xrtValueRelease(Root); return Ok;
}

static void MdoModelsInstallWireHooks(xllm_client* Client)
{
    xllm_hooks Hooks = {0}; Hooks.pOnRequestBody = MdoModelsStrictBody;
    if (Client) xllmClientSetHooks(Client, &Hooks);
}

typedef struct MdoModelHooks { xllm_hooks Inner; bool ProviderDefault; } MdoModelHooks;
static bool MdoModelsHookRequest(xllm_client* C, xllm_request* R, void* Data)
{
    MdoModelHooks* H = Data;
    bool Ok = !H->Inner.pOnRequest || H->Inner.pOnRequest(C, R, H->Inner.pUserData);
    if (Ok && R->sReasoningEffort) {
        H->ProviderDefault = !strcmp(R->sReasoningEffort, "default");
        if (H->ProviderDefault) R->sReasoningEffort = NULL;
    }
    return Ok;
}
static bool MdoModelsHookBody(xllm_client* C, xllm_wire* W, void* Data)
{
    MdoModelHooks* H = Data; char* Original = W->sBody;
    if (H->Inner.pOnRequestBody && !H->Inner.pOnRequestBody(C, W, H->Inner.pUserData)) return false;
    char* Inner = W->sBody; bool Ok = MdoModelsStrictBody(C, W, H->ProviderDefault ? H : NULL);
    if (Inner != Original && W->sBody != Inner) xllmFree(Inner);
    return Ok;
}
static bool MdoModelsHookRetry(xllm_client* C, const xllm_diagnostics* D, uint32 N, void* Data)
{ MdoModelHooks* H = Data; return !H->Inner.pOnRetry || H->Inner.pOnRetry(C, D, N, H->Inner.pUserData); }
static bool MdoModelsHookResponseBody(xllm_client* C, xllm_wire* W, void* Data)
{ MdoModelHooks* H = Data; return !H->Inner.pOnResponseBody || H->Inner.pOnResponseBody(C, W, H->Inner.pUserData); }
static bool MdoModelsHookTool(xllm_client* C, xllm_tool_call* T, uint32 I, void* Data)
{ MdoModelHooks* H = Data; return !H->Inner.pOnToolCall || H->Inner.pOnToolCall(C, T, I, H->Inner.pUserData); }
static bool MdoModelsHookResponse(xllm_client* C, xllm_response* R, void* Data)
{ MdoModelHooks* H = Data; return !H->Inner.pOnResponse || H->Inner.pOnResponse(C, R, H->Inner.pUserData); }

static void MdoModelsRequestHooks(xllm_request* View, MdoModelHooks* Context, xllm_hooks* Hooks, bool ProviderDefault)
{
    memset(Context, 0, sizeof(*Context)); Context->ProviderDefault = ProviderDefault;
    if (!View->pHooks && !ProviderDefault) return; /* Product clients already own the default seam. */
    if (View->pHooks) Context->Inner = *View->pHooks; memset(Hooks, 0, sizeof(*Hooks));
    Hooks->pOnRequest = Context->Inner.pOnRequest ? MdoModelsHookRequest : NULL;
    Hooks->pOnRequestBody = MdoModelsHookBody;
    Hooks->pOnRetry = Context->Inner.pOnRetry ? MdoModelsHookRetry : NULL;
    Hooks->pOnResponseBody = Context->Inner.pOnResponseBody ? MdoModelsHookResponseBody : NULL;
    Hooks->pOnToolCall = Context->Inner.pOnToolCall ? MdoModelsHookTool : NULL;
    Hooks->pOnResponse = Context->Inner.pOnResponse ? MdoModelsHookResponse : NULL;
    Hooks->pUserData = Context; View->pHooks = Hooks;
}
