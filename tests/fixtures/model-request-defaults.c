#define ServiceInit ModelDefaultsProductInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned DefaultsFailed;
static void CheckDefaults(bool Ok,const char* Name){if(!Ok){++DefaultsFailed;printf("FAIL %s\n",Name);}}
void ServiceInit(XS_HostInfo* Host){
    (void)Host;
    xvalue* Registry=xrtJsonParse(XRT_STR_LITERAL("{\"version\":\"fixture\",\"entries\":[{\"bases\":[\"https://api.deepseek.com/v1\"],\"ids\":[\"deepseek-flash\"],\"protocols\":[\"openai-chat-completions\"],\"fields\":{\"context_tokens\":1048576,\"max_output_tokens\":393216},\"request_defaults\":{\"output_tokens\":65536,\"output_tokens_by_effort\":{\"none\":8192,\"max\":131072},\"basis\":\"official-default\",\"source_url\":\"https://api-docs.deepseek.com/api/create-chat-completion/\",\"checked_at\":\"2026-11-07\"},\"source_url\":\"https://api-docs.deepseek.com/api/list-models/\",\"checked_at\":\"2026-11-07\",\"upstream\":true}]}"));
    xvalue* Api=xrtJsonParse(XRT_STR_LITERAL("{\"max_output_tokens\":32768,\"context_window\":1000000}"));
    xvalue* Record=MdoParameterResolve(Registry,"https://api.deepseek.com/v1/chat/completions","openai-chat-completions","deepseek-flash",Api,"ok");
    const xvalue* Defaults=xrtValueObjectGet(Record,XRT_STR_LITERAL("request_defaults"));
    uint64 Output=0;CheckDefaults(MdoConfigUnsigned(xrtValueObjectGet(Defaults,XRT_STR_LITERAL("output_tokens")),&Output)&&Output==65536,"separate_official_budget");
    const xvalue* Fields=xrtValueObjectGet(Record,XRT_STR_LITERAL("fields"));
    const xvalue* Cap=xrtValueObjectGet(Fields,XRT_STR_LITERAL("max_output_tokens"));
    CheckDefaults(MdoConfigUnsigned(xrtValueObjectGet(Cap,XRT_STR_LITERAL("value")),&Output)&&Output==32768,"api_limit_stays_authoritative");
    MdoApiValueSetUInt(Record,"schema_version",1);
    CheckDefaults(MdoConfigModelParametersValid(Record),"advisory_defaults_save_without_unknown_field_failure");
    xvalue* First=(xvalue*)xrtValueArrayGet(xrtValueObjectGet(Registry,XRT_STR_LITERAL("entries")),0);
    size_t Bytes=0;char* Json=xrtJsonStringify(First,false,&Bytes);
    xvalue* Other=xrtJsonParse(xrtStrViewN(Json,Bytes));xrtFree(Json);
    MdoApiValueSetUInt((xvalue*)xrtValueObjectGet(Other,XRT_STR_LITERAL("request_defaults")),"output_tokens",131072);
    xrtValueArrayAppend((xvalue*)xrtValueObjectGet(Registry,XRT_STR_LITERAL("entries")),Other);
    xvalue* Ambiguous=MdoParameterResolve(Registry,"https://relay.example/v1/chat/completions","openai-chat-completions","deepseek-flash",Api,"ok");
    CheckDefaults(!xrtValueObjectHas(Ambiguous,XRT_STR_LITERAL("request_defaults")),"ambiguous_proxy_budget_is_not_guessed");
    xrtValueRelease(Ambiguous);xrtValueRelease(Other);xrtValueRelease(Record);xrtValueRelease(Api);xrtValueRelease(Registry);
    printf("model_defaults_failed=%u\nmodel_defaults_done=1\n",DefaultsFailed);fflush(stdout);
}
