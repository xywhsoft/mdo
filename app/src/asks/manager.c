#include <stdio.h>
#include <string.h>

#include "../../include/mdo/asks.h"

#define MDO_ASK_TIMEOUT_US (4u * 60u * 60u * 1000000u)
#define MDO_ASK_POLL_US 250000u

struct MdoAskBinding {
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
};

typedef struct MdoAskEntry {
    bool Used;
    bool Answered;
    xcancel* Cancel; /* borrowed only while the tool callback is active */
    uint64 ExpiresAt;
    uint64 Deadline;
    MdoAskInfo Info;
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Answer[MDO_ASK_ANSWER_CAPACITY];
} MdoAskEntry;

typedef struct MdoAskManager {
    xmutex* Lock;
    xcond* Changed;
    uint64 NextId;
    size_t ActiveCallbacks;
    bool Stopping;
    bool Initialized;
    MdoAskEntry Entries[MDO_ASK_PENDING_MAX];
} MdoAskManager;

static MdoAskManager g_MdoAsks;

static void MdoAskError(xwork_error* Error, xwork_error_code Code,
    const char* Message)
{
    if ( Error == NULL ) return;
    xworkErrorInit(Error);
    Error->eCode = Code;
    snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message);
}

static bool MdoAskCopy(char* Target, size_t Capacity, xstrview Source)
{
    if ( Source.Size == 0u || Source.Size >= Capacity ||
         memchr(Source.Data, 0, Source.Size) != NULL ||
         !xrtUtf8Valid(Source, NULL) ) return false;
    memcpy(Target, Source.Data, Source.Size);
    Target[Source.Size] = '\0';
    return true;
}

static bool MdoAskParse(const char* Json, MdoAskInfo* Info)
{
    xjsonreadconfig Config;
    xvalue* Root;
    const xvalue* Question;
    const xvalue* Options;
    xstrview View;
    size_t Index;
    size_t Size = 0u;
    bool Ok = false;
    if ( Json == NULL || Info == NULL ) return false;
    while ( Size <= 8192u && Json[Size] != '\0' ) ++Size;
    if ( Size > 8192u ) return false;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = 8192u;
    Config.MaxDepth = 3u;
    Config.MaxValues = 12u;
    Config.MaxContainerItems = MDO_ASK_OPTIONS_MAX;
    Root = xrtJsonRead(xrtStrViewN(Json, Size), &Config);
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) < 1u || xrtValueCount(Root) > 2u ) goto done;
    Question = xrtValueObjectGet(Root, XRT_STR_LITERAL("question"));
    Options = xrtValueObjectGet(Root, XRT_STR_LITERAL("options"));
    if ( xrtValueType(Question) != XVALUE_STRING ||
         !xrtValueGetString(Question, &View) ||
         !MdoAskCopy(Info->Question, sizeof(Info->Question), View) ||
         (Options == NULL && xrtValueCount(Root) != 1u) ||
         (Options != NULL && xrtValueCount(Root) != 2u) ) goto done;
    if ( Options != NULL ) {
        if ( xrtValueType(Options) != XVALUE_ARRAY ||
             xrtValueCount(Options) > MDO_ASK_OPTIONS_MAX ) goto done;
        for ( Index = 0u; Index < xrtValueCount(Options); ++Index ) {
            const xvalue* Item = xrtValueArrayGet(Options, Index);
            if ( xrtValueType(Item) != XVALUE_STRING ||
                 !xrtValueGetString(Item, &View) ||
                 !MdoAskCopy(Info->Options[Index],
                    sizeof(Info->Options[Index]), View) ) goto done;
        }
        Info->OptionCount = xrtValueCount(Options);
    }
    Ok = true;
done:
    xrtValueRelease(Root);
    return Ok;
}

bool MdoAskManagerInit(void)
{
    if ( g_MdoAsks.Initialized ) return true;
    memset(&g_MdoAsks, 0, sizeof(g_MdoAsks));
    g_MdoAsks.Lock = xrtMutexCreate();
    g_MdoAsks.Changed = xrtCondCreate();
    if ( g_MdoAsks.Lock == NULL || g_MdoAsks.Changed == NULL ) {
        if ( g_MdoAsks.Changed != NULL )
            (void)xrtCondDestroy(g_MdoAsks.Changed);
        if ( g_MdoAsks.Lock != NULL )
            (void)xrtMutexDestroy(g_MdoAsks.Lock);
        memset(&g_MdoAsks, 0, sizeof(g_MdoAsks));
        return false;
    }
    g_MdoAsks.Initialized = true;
    return true;
}

void MdoAskManagerUnit(void)
{
    if ( !g_MdoAsks.Initialized || g_MdoAsks.Lock == NULL ||
         !xrtMutexLock(g_MdoAsks.Lock) ) return;
    g_MdoAsks.Stopping = true;
    (void)xrtCondBroadcast(g_MdoAsks.Changed);
    while ( g_MdoAsks.ActiveCallbacks != 0u )
        (void)xrtCondWait(g_MdoAsks.Changed, g_MdoAsks.Lock);
    (void)xrtMutexUnlock(g_MdoAsks.Lock);
    (void)xrtCondDestroy(g_MdoAsks.Changed);
    (void)xrtMutexDestroy(g_MdoAsks.Lock);
    memset(&g_MdoAsks, 0, sizeof(g_MdoAsks));
}

bool MdoAskList(const char* ProjectId, const char* SessionId,
    MdoAskInfo* Items, size_t Capacity, size_t* Count)
{
    size_t Index;
    size_t Total = 0u;
    if ( Count == NULL || ProjectId == NULL || SessionId == NULL ||
         (Capacity != 0u && Items == NULL) || !g_MdoAsks.Initialized ||
         !xrtMutexLock(g_MdoAsks.Lock) ) return false;
    for ( Index = 0u; Index < MDO_ASK_PENDING_MAX; ++Index ) {
        const MdoAskEntry* Entry = &g_MdoAsks.Entries[Index];
        if ( !Entry->Used || Entry->Answered ||
             strcmp(Entry->ProjectId, ProjectId) != 0 ||
             strcmp(Entry->SessionId, SessionId) != 0 ) continue;
        if ( Total < Capacity ) Items[Total] = Entry->Info;
        ++Total;
    }
    (void)xrtMutexUnlock(g_MdoAsks.Lock);
    *Count = Total;
    return true;
}

bool MdoAskAnswer(const char* ProjectId, const char* SessionId,
    uint64 Id, const char* Answer, xwork_error* Error)
{
    size_t Index;
    size_t Length = 0u;
    xworkErrorInit(Error);
    if ( Answer != NULL )
        while ( Length < MDO_ASK_ANSWER_CAPACITY && Answer[Length] != '\0' )
            ++Length;
    if ( ProjectId == NULL || SessionId == NULL || Id == 0u ||
         Answer == NULL || Length == 0u ||
         Length >= MDO_ASK_ANSWER_CAPACITY ||
         !xrtUtf8Valid(xrtStrViewN(Answer, Length), NULL) ) {
        MdoAskError(Error, XWORK_ERROR_INVALID_ARGUMENT,
            "the answer must be 1 to 1024 UTF-8 bytes");
        return false;
    }
    if ( !g_MdoAsks.Initialized || !xrtMutexLock(g_MdoAsks.Lock) ) {
        MdoAskError(Error, XWORK_ERROR_CONTEXT, "ask manager is unavailable");
        return false;
    }
    for ( Index = 0u; Index < MDO_ASK_PENDING_MAX; ++Index ) {
        MdoAskEntry* Entry = &g_MdoAsks.Entries[Index];
        if ( !Entry->Used || Entry->Answered || Entry->Info.Id != Id ||
             strcmp(Entry->ProjectId, ProjectId) != 0 ||
             strcmp(Entry->SessionId, SessionId) != 0 ) continue;
        if ( g_MdoAsks.Stopping ||
             (Entry->Cancel != NULL && xrtCancelRequested(Entry->Cancel)) ||
             xrtDeadlineExpired(Entry->ExpiresAt) ||
             (Entry->Deadline != 0u &&
              Entry->Deadline != XRT_DEADLINE_NEVER &&
              xrtDeadlineExpired(Entry->Deadline)) ) break;
        memcpy(Entry->Answer, Answer, Length + 1u);
        Entry->Answered = true;
        (void)xrtCondBroadcast(g_MdoAsks.Changed);
        (void)xrtMutexUnlock(g_MdoAsks.Lock);
        return true;
    }
    (void)xrtMutexUnlock(g_MdoAsks.Lock);
    MdoAskError(Error, XWORK_ERROR_POLICY, "ask is no longer pending");
    return false;
}

static xwork_result MdoAskExecute(void* UserData,
    const xwork_tool_context* Context, const char* ArgumentsJson,
    xwork_tool_output* Output, xwork_error* Error)
{
    const MdoAskBinding* Binding = (const MdoAskBinding*)UserData;
    MdoAskInfo Request;
    MdoAskEntry* Entry = NULL;
    char Answer[MDO_ASK_ANSWER_CAPACITY];
    uint64 ExpiresAt;
    bool Cancelled = false;
    size_t Index;
    if ( Binding == NULL || Context == NULL || Output == NULL )
        return XWORK_RESULT_ERROR;
    memset(&Request, 0, sizeof(Request));
    if ( !MdoAskParse(ArgumentsJson, &Request) ) {
        if ( !xworkToolOutputSet(Output, false,
                "Invalid question or options; revise the ask_user call.") )
            return XWORK_RESULT_ERROR;
        return XWORK_RESULT_OK;
    }
    if ( !g_MdoAsks.Initialized || !xrtMutexLock(g_MdoAsks.Lock) ) {
        MdoAskError(Error, XWORK_ERROR_CONTEXT, "ask manager is unavailable");
        return XWORK_RESULT_ERROR;
    }
    if ( g_MdoAsks.Stopping || g_MdoAsks.NextId == UINT64_MAX )
        goto unavailable;
    for ( Index = 0u; Index < MDO_ASK_PENDING_MAX; ++Index ) {
        if ( !g_MdoAsks.Entries[Index].Used ) {
            Entry = &g_MdoAsks.Entries[Index];
            break;
        }
    }
    if ( Entry == NULL ) goto unavailable;
    memset(Entry, 0, sizeof(*Entry));
    Entry->Used = true;
    Entry->Info = Request;
    Entry->Info.Id = ++g_MdoAsks.NextId;
    Entry->Info.RunId = Context->uRunId;
    Entry->Info.CreatedAt = xrtNow();
    Entry->Cancel = Context->pCancel;
    Entry->Deadline = Context->uDeadline;
    memcpy(Entry->ProjectId, Binding->ProjectId,
        strlen(Binding->ProjectId) + 1u);
    memcpy(Entry->SessionId, Binding->SessionId,
        strlen(Binding->SessionId) + 1u);
    ++g_MdoAsks.ActiveCallbacks;
    ExpiresAt = xrtClock();
    ExpiresAt = ExpiresAt > UINT64_MAX - MDO_ASK_TIMEOUT_US ?
        UINT64_MAX : ExpiresAt + MDO_ASK_TIMEOUT_US;
    Entry->ExpiresAt = ExpiresAt;
    (void)xrtCondBroadcast(g_MdoAsks.Changed);
    while ( !Entry->Answered ) {
        uint64 Now = xrtClock();
        uint64 Wake = Now + MDO_ASK_POLL_US;
        if ( g_MdoAsks.Stopping ||
             (Context->pCancel != NULL &&
              xrtCancelRequested(Context->pCancel)) ) {
            Cancelled = true;
            break;
        }
        if ( Now >= ExpiresAt ||
             (Context->uDeadline != XRT_DEADLINE_NEVER &&
              Context->uDeadline != 0u &&
              xrtDeadlineExpired(Context->uDeadline)) ) break;
        if ( Wake > ExpiresAt ) Wake = ExpiresAt;
        if ( Context->uDeadline != XRT_DEADLINE_NEVER &&
             Context->uDeadline != 0u && Context->uDeadline < Wake )
            Wake = Context->uDeadline;
        if ( xrtCondWaitUntil(g_MdoAsks.Changed, g_MdoAsks.Lock,
                Wake) == XWAIT_ERROR ) break;
    }
    snprintf(Answer, sizeof(Answer), "%s", Entry->Answer);
    memset(Entry, 0, sizeof(*Entry));
    --g_MdoAsks.ActiveCallbacks;
    (void)xrtCondBroadcast(g_MdoAsks.Changed);
    (void)xrtMutexUnlock(g_MdoAsks.Lock);
    if ( Cancelled ) {
        MdoAskError(Error, XWORK_ERROR_CANCELLED, "user question was cancelled");
        return XWORK_RESULT_CANCELLED;
    }
    if ( !xworkToolOutputSet(Output, true, Answer[0] != '\0' ? Answer :
            "[user did not answer; continue without asking again]") ) {
        MdoAskError(Error, XWORK_ERROR_OUT_OF_MEMORY,
            "cannot encode the user's answer");
        return XWORK_RESULT_ERROR;
    }
    return XWORK_RESULT_OK;
unavailable:
    (void)xrtMutexUnlock(g_MdoAsks.Lock);
    MdoAskError(Error, XWORK_ERROR_LIMIT, "too many pending user questions");
    return XWORK_RESULT_LIMIT;
}

bool MdoAskRegisterTool(xwork_agent* Agent, const char* ProjectId,
    const char* SessionId, MdoAskBinding** Binding, xwork_error* Error)
{
    static const char* Schema =
        "{\"type\":\"object\",\"properties\":{\"question\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":1000},\"options\":{\"type\":\"array\",\"maxItems\":8,\"items\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":240}}},\"required\":[\"question\"],\"additionalProperties\":false}";
    xwork_tool_definition Tool;
    MdoAskBinding* Value;
    if ( Binding == NULL || Agent == NULL ) return false;
    *Binding = NULL;
    if ( ProjectId == NULL || ProjectId[0] == '\0' ||
         SessionId == NULL || SessionId[0] == '\0' ) return true;
    if ( strlen(ProjectId) >= MDO_PROJECT_ID_CAPACITY ||
         strlen(SessionId) >= MDO_SESSION_ID_CAPACITY ) return false;
    Value = (MdoAskBinding*)xrtCalloc(1u, sizeof(*Value));
    if ( Value == NULL ) return false;
    snprintf(Value->ProjectId, sizeof(Value->ProjectId), "%s", ProjectId);
    snprintf(Value->SessionId, sizeof(Value->SessionId), "%s", SessionId);
    memset(&Tool, 0, sizeof(Tool));
    Tool.sName = "ask_user";
    Tool.sDescription = "Ask the user a concise question and wait for an answer. Provide short options when useful; free text is always available.";
    Tool.sParametersJson = Schema;
    Tool.bStrict = true;
    Tool.uEffects = XWORK_TOOL_EFFECT_READ;
    Tool.OnExecute = MdoAskExecute;
    Tool.pUserData = Value;
    Tool.sSource = "mdo-interaction";
    Tool.iMaxResultBytes = MDO_ASK_ANSWER_CAPACITY + 80u;
    Tool.sSerialGroup = "mdo.ask_user";
    if ( !xworkAgentRegisterTool(Agent, &Tool, Error) ) {
        xrtFree(Value);
        return false;
    }
    *Binding = Value;
    return true;
}

void MdoAskBindingDestroy(MdoAskBinding* Binding)
{
    xrtFree(Binding);
}
