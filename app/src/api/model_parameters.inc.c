/* Field-level discovery. The bundled registry is advisory, never a claim that
 * a proxy implements every upstream feature. Unknown and false stay distinct. */
static bool MdoParameterTextEqual(const xvalue* Value, cstr Text)
{
    xstrview S = {0}; return Text && xrtValueGetString(Value, &S) &&
        S.Size == strlen(Text) && !memcmp(S.Data, Text, S.Size);
}

static bool MdoParameterContains(const xvalue* List, cstr Text)
{
    for (size_t i = 0; i < xrtValueCount(List); ++i)
        if (MdoParameterTextEqual(xrtValueArrayGet(List, i), Text)) return true;
    return false;
}

static bool MdoParameterSet(xvalue* Fields, cstr Name, const xvalue* Value,
    cstr Source, cstr Url, const xvalue* Date)
{
    if (!Value || xrtValueObjectHas(Fields, xrtStrView(Name))) return true;
    xvalue* Field = xrtValueObject();
    bool Ok = Field && xrtValueObjectSet(Field, XRT_STR_LITERAL("value"), (xvalue*)Value) &&
        MdoApiValueSetString(Field, "source", Source) && MdoApiValueSetString(Field, "url", Url ? Url : "") &&
        (!Date || xrtValueObjectSet(Field, XRT_STR_LITERAL("checked_at"), (xvalue*)Date)) &&
        xrtValueObjectSet(Fields, xrtStrView(Name), Field);
    xrtValueRelease(Field); return Ok;
}

static bool MdoParameterPositive(const xvalue* Value)
{
    uint64 U; int64 I;
    return (xrtValueGetUInt(Value, &U) && U > 0 && U <= UINT32_MAX) ||
        (xrtValueGetInt(Value, &I) && I > 0 && I <= UINT32_MAX);
}

static xvalue* MdoParameterApiFields(const xvalue* Row)
{
    xvalue* Fields = xrtValueObject(); xvalue* Reasoning = NULL;
    const xvalue* Top = xrtValueObjectGet(Row, XRT_STR_LITERAL("top_provider"));
    const xvalue* Context = xrtValueObjectGet(Top, XRT_STR_LITERAL("context_length"));
    if (!MdoParameterPositive(Context)) Context = xrtValueObjectGet(Row, XRT_STR_LITERAL("context_length"));
    const xvalue* Input = xrtValueObjectGet(Row, XRT_STR_LITERAL("max_input_tokens"));
    const xvalue* Output = xrtValueObjectGet(Top, XRT_STR_LITERAL("max_completion_tokens"));
    if (!MdoParameterPositive(Output)) Output = xrtValueObjectGet(Row, XRT_STR_LITERAL("max_tokens"));
    if (!MdoParameterPositive(Output)) Output = xrtValueObjectGet(Row, XRT_STR_LITERAL("max_output_tokens"));
    /* Anthropic publishes an input limit, not a shared input+output window. */
    if (MdoParameterPositive(Input)) {
        xrtValueObjectSet(Fields, XRT_STR_LITERAL("max_input_tokens"), (xvalue*)Input);
        if (!MdoParameterPositive(Context)) {
            Context = Input;
            MdoApiValueSetString(Fields, "window_mode", "split-input-output");
        }
    }
    if (MdoParameterPositive(Context)) xrtValueObjectSet(Fields, XRT_STR_LITERAL("context_tokens"), (xvalue*)Context);
    if (MdoParameterPositive(Output)) xrtValueObjectSet(Fields, XRT_STR_LITERAL("max_output_tokens"), (xvalue*)Output);
    const xvalue* Caps = xrtValueObjectGet(Row, XRT_STR_LITERAL("capabilities"));
    const xvalue* Thinking = xrtValueObjectGet(Caps, XRT_STR_LITERAL("thinking"));
    const xvalue* Effort = xrtValueObjectGet(Caps, XRT_STR_LITERAL("effort"));
    bool Supported;
    if (xrtValueGetBool(xrtValueObjectGet(Thinking, XRT_STR_LITERAL("supported")), &Supported)) {
        Reasoning = xrtValueObject(); MdoApiValueSetString(Reasoning, "kind", Supported ? "adaptive" : "none");
        xrtValueObjectSet(Reasoning, XRT_STR_LITERAL("declared"), (xvalue*)Thinking);
    }
    if (xrtValueGetBool(xrtValueObjectGet(Effort, XRT_STR_LITERAL("supported")), &Supported) && Supported) {
        if (!Reasoning) Reasoning = xrtValueObject();
        MdoApiValueSetString(Reasoning, "kind", "effort"); xvalue* Levels = xrtValueArray();
        static const char* const Names[] = {"low", "medium", "high", "xhigh", "max"};
        for (size_t i = 0; i < 5; ++i) {
            bool Enabled = false;
            if (xrtValueGetBool(xrtValueObjectGet(Effort, xrtStrView(Names[i])), &Enabled) && Enabled)
                MdoApiValueAppendString(Levels, Names[i]);
        }
        if (!xrtValueCount(Levels)) {
            xrtValueRelease(Reasoning); Reasoning = NULL;
        } else MdoApiValueSetTake(Reasoning, "efforts", &Levels);
        xrtValueRelease(Levels);
    }
    if (Reasoning) { xrtValueObjectSet(Fields, XRT_STR_LITERAL("reasoning"), Reasoning); xrtValueRelease(Reasoning); }
    const xvalue* Tools = xrtValueObjectGet(Caps, XRT_STR_LITERAL("tool_calling"));
    if (xrtValueType(Tools) == XVALUE_OBJECT) Tools = xrtValueObjectGet(Tools, XRT_STR_LITERAL("supported"));
    if (xrtValueGetBool(Tools, &Supported)) xrtValueObjectSet(Fields, XRT_STR_LITERAL("tools"), (xvalue*)Tools);
    /* A parameter name alone does not enumerate supported effort values. */
    return Fields;
}

static xvalue* MdoParameterResolve(const xvalue* Registry, cstr Endpoint, cstr Protocol,
    cstr Id, const xvalue* Row, cstr LookupState)
{
    xvalue* Result = xrtValueObject(); xvalue* Fields = xrtValueObject();
    xvalue* Api = MdoParameterApiFields(Row); xvalue* Now = xrtValueInt(MdoUtcUs() / 1000);
    const xvalue* Entries = xrtValueObjectGet(Registry, XRT_STR_LITERAL("entries"));
    const xvalue* Match = NULL; const xvalue* Upstream = NULL; bool KnownHost = false, Ambiguous = false;
    for (size_t i = 0; i < xrtValueCount(Entries); ++i) {
        const xvalue* Entry = xrtValueArrayGet(Entries, i);
        const xvalue* Bases = xrtValueObjectGet(Entry, XRT_STR_LITERAL("bases"));
        bool BaseMatch = false;
        for (size_t j = 0; j < xrtValueCount(Bases); ++j) {
            xstrview Base = {0}; xrtValueGetString(xrtValueArrayGet(Bases, j), &Base);
            const char* Host = strstr(Endpoint, "://");
            size_t HostSize = Host ? (size_t)(Host + 3 - Endpoint) + strcspn(Host + 3, "/") : 0;
            if (HostSize && Base.Size >= HostSize && !memcmp(Base.Data, Endpoint, HostSize) &&
                (Base.Size == HostSize || Base.Data[HostSize] == '/')) KnownHost = true;
            if (strlen(Endpoint) > Base.Size && !memcmp(Endpoint, Base.Data, Base.Size) && Endpoint[Base.Size] == '/') {
                cstr Tail = Endpoint + Base.Size;
                BaseMatch |= !strcmp(Tail, "/responses") || !strcmp(Tail, "/chat/completions") || !strcmp(Tail, "/messages");
            }
        }
        if (!MdoParameterContains(xrtValueObjectGet(Entry, XRT_STR_LITERAL("ids")), Id) ||
            !MdoParameterContains(xrtValueObjectGet(Entry, XRT_STR_LITERAL("protocols")), Protocol)) continue;
        bool Recommend = false; xrtValueGetBool(xrtValueObjectGet(Entry, XRT_STR_LITERAL("upstream")), &Recommend);
        if (BaseMatch) { Match = Entry; break; }
        if (Recommend) {
            if (Upstream && !xrtValueEqual(xrtValueObjectGet(Upstream, XRT_STR_LITERAL("fields")),
                xrtValueObjectGet(Entry, XRT_STR_LITERAL("fields")))) Ambiguous = true;
            Upstream = Entry;
        }
    }
    bool Recommendation = !Match && !KnownHost && !Ambiguous; if (Recommendation) Match = Upstream;
    bool Ok = Result && Fields && Api && Now;
    for (size_t i = 0; Ok && i < xrtValueCount(Api); ++i) {
        xstrview K; const xvalue* V = xrtValueObjectAt(Api, i, &K); char Name[64];
        if (K.Size >= sizeof(Name)) { Ok = false; break; }
        memcpy(Name, K.Data, K.Size); Name[K.Size] = 0;
        Ok = MdoParameterSet(Fields, Name, V, "api", Endpoint, Now);
    }
    const xvalue* Defaults = xrtValueObjectGet(Match, XRT_STR_LITERAL("fields"));
    cstr Url = MdoApiModelText(Match, "source_url", 2048);
    for (size_t i = 0; Ok && i < xrtValueCount(Defaults); ++i) {
        xstrview K; const xvalue* V = xrtValueObjectAt(Defaults, i, &K); char Name[64];
        if (K.Size >= sizeof(Name)) { Ok = false; break; }
        memcpy(Name, K.Data, K.Size); Name[K.Size] = 0;
        Ok = MdoParameterSet(Fields, Name, V, Recommendation ? "upstream-recommendation" : "server", Url,
            xrtValueObjectGet(Match, XRT_STR_LITERAL("checked_at")));
    }
    /* A boolean effort support without a list is still unknown. A declared
     * non-effort mode remains visible even if this adapter cannot control it. */
    Ok = Ok && MdoApiValueSetString(Result, "id", Id) && MdoApiValueSetString(Result, "protocol", Protocol) &&
        MdoApiValueSetString(Result, "lookup_state", LookupState) &&
        MdoApiValueSetString(Result, "registry_version", MdoApiModelText(Registry, "version", 64) ? MdoApiModelText(Registry, "version", 64) : "unavailable") &&
        MdoApiValueSetTake(Result, "fields", &Fields);
    xrtValueRelease(Api); xrtValueRelease(Now); xrtValueRelease(Fields);
    if (!Ok) { xrtValueRelease(Result); return NULL; } return Result;
}
