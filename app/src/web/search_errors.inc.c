/* Only the account service's bounded, known rejection envelope can authorize
 * replay of a billed POST. Unknown/contradictory reasons, transport failure and
 * any provider dispatch remain single attempts. Never echo upstream prose. */
#define MDO_WEB_SEARCH_MAX_ATTEMPTS 6u
#define MDO_WEB_SEARCH_RECOVERY_US UINT64_C(60000000)
typedef struct MdoWebSearchError {
    const char* Code;
    uint64 Status;
    const char* Message;
    bool RetrySafe;
} MdoWebSearchError;
static const MdoWebSearchError g_MdoWebSearchErrors[] = {
    {"login_required",401,"Search requires a valid account login. Sign in again.",false},
    {"contact_required",403,"Search requires the phone/email verification configured by the account service.",false},
    {"provider_unconfigured",503,"Search service has no enabled provider with valid configuration. Its administrator must configure the provider.",false},
    {"member_busy",429,"Another search for this account is still running. Wait for it to finish.",true},
    {"server_busy",429,"Search service is temporarily busy. Try again later.",true},
    {"minute_limit",429,"Account search minute limit reached. Wait for the next minute window.",true},
    {"daily_limit",429,"Your daily search quota is exhausted. Wait for the daily reset; switching queries will not restore the quota.",false},
    {"global_daily_limit",429,"The service's daily search quota is exhausted. Wait for the daily reset or contact its administrator.",false},
    {"budget_busy",503,"Search budget database is temporarily busy. Try again later.",true},
    {"budget_unavailable",503,"Search budget database is unavailable. Contact the service administrator.",false},
    {"workers_busy",503,"Search service has no available worker. Try again later.",true},
    {"request_build_failed",503,"Search service could not prepare this request. Contact its administrator.",false},
    {"provider_auth_failed",502,"Search provider rejected the service credentials. The service administrator must repair them; signing in again will not help.",false},
    {"provider_rate_limited",502,"Search provider rate limit reached. Wait before searching again.",false},
    {"provider_unavailable",502,"Search provider is temporarily unavailable. Try again later.",false},
    {"provider_timeout",504,"Search provider timed out. Submission may have consumed an attempt; try again later.",false},
    {"provider_invalid_response",502,"Search provider returned an invalid response. Contact the service administrator.",false}
};

static xvalue* MdoWebSearchEnvelope(MdoWebState* State, const XS_FetchResponse* Response)
{
    xjsonreadconfig Config;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = State->Settings.MaxResponseBytes;
    Config.MaxDepth = 8u; Config.MaxValues = 512u; Config.MaxContainerItems = 64u;
    if ( Response->Body == NULL || !xrtUtf8Valid(
            (xstrview){(const char*)Response->Body, Response->BodySize}, NULL) ) return NULL;
    return xrtJsonRead((xstrview){(const char*)Response->Body, Response->BodySize}, &Config);
}

static const MdoWebSearchError* MdoWebSearchReason(const xvalue* Envelope,
    uint64 Status, uint64 Code, bool* RetrySafe, uint64* Minimum)
{
    *RetrySafe = false; *Minimum = 0u;
    if ( Code < 400u || (Status != 200u && Code != Status) ) return NULL;
    const xvalue* Data = xrtValueObjectGet(Envelope, xrtStrView("data"));
    const xvalue* Error = xrtValueObjectGet(Data, xrtStrView("error"));
    xstrview Name;
    bool Safe, Dispatched;
    uint64 Milliseconds;
    if ( !MdoWebString(xrtValueObjectGet(Error, xrtStrView("code")),1u,64u,&Name) ||
         !xrtValueGetBool(xrtValueObjectGet(Error, xrtStrView("retry_safe")),&Safe) ||
         !xrtValueGetBool(xrtValueObjectGet(Error, xrtStrView("dispatched")),&Dispatched) ||
         !MdoWebUnsigned(xrtValueObjectGet(Error, xrtStrView("retry_after_ms")),&Milliseconds) ||
         Milliseconds > UINT64_C(86401000) ) return NULL;
    for (size_t i = 0; i < sizeof(g_MdoWebSearchErrors)/sizeof(g_MdoWebSearchErrors[0]); ++i) {
        const MdoWebSearchError* Reason = &g_MdoWebSearchErrors[i];
        if ( Reason->Status != Code || strlen(Reason->Code) != Name.Size ||
             memcmp(Reason->Code, Name.Data, Name.Size) != 0 ) continue;
        *RetrySafe = Reason->RetrySafe && Safe && !Dispatched;
        *Minimum = Milliseconds * 1000u;
        return Reason;
    }
    return NULL;
}
