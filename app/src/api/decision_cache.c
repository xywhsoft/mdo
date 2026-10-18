#include "../../include/mdo/time.h"
#include <string.h>
#include "decision_cache.h"
#include "write_admission.h"

/* Transport acknowledgements only: no disk files or permission history.
 * Retention exceeds the frontend's one-minute recovery budget. Live receipts
 * are never evicted to accept a new decision; capacity fails before applying. */
#define MDO_DECISION_CACHE_MAX 128u
#define MDO_DECISION_PATH_CAPACITY 256u
#define MDO_DECISION_VALUE_CAPACITY 1025u
#define MDO_DECISION_RETAIN_US (UINT64_C(120) * 1000000u)

typedef struct MdoApiDecisionReceipt {
    uint64 ExpiresAt;
    char Token[MDO_API_WRITE_TOKEN_CAPACITY];
    char Path[MDO_DECISION_PATH_CAPACITY];
    char Value[MDO_DECISION_VALUE_CAPACITY];
} MdoApiDecisionReceipt;

static xmutex* g_MdoDecisionLock;
static MdoApiDecisionReceipt g_MdoDecisionReceipts[MDO_DECISION_CACHE_MAX];

bool MdoApiDecisionsInit(void)
{
    if ( g_MdoDecisionLock != NULL ) return true;
    g_MdoDecisionLock = xrtMutexCreate();
    return g_MdoDecisionLock != NULL;
}

void MdoApiDecisionsUnit(void)
{
    /* API requests have drained before teardown, as for write admission. */
    if ( g_MdoDecisionLock != NULL ) xrtMutexDestroy(g_MdoDecisionLock);
    g_MdoDecisionLock = NULL;
    xrtSecureZero(g_MdoDecisionReceipts, sizeof(g_MdoDecisionReceipts));
}

MdoApiDecisionStatus MdoApiDecisionBegin(const MdoApiContext* Context,
    cstr Value, MdoApiDecisionAttempt* Attempt)
{
    char Token[MDO_API_WRITE_TOKEN_CAPACITY];
    size_t i, Length;
    MdoApiDecisionReceipt* Free = NULL;
    if ( Attempt == NULL ) return MDO_API_DECISION_UNAVAILABLE;
    Attempt->Entry = NULL;
    if ( Context == NULL || Value == NULL || g_MdoDecisionLock == NULL ||
         Context->Target.Path.Data == NULL || Context->Target.Path.Size == 0u ||
         Context->Target.Path.Size >= MDO_DECISION_PATH_CAPACITY ||
         memchr(Context->Target.Path.Data, 0, Context->Target.Path.Size) != NULL ||
         !MdoApiWriteToken(Token) ) return MDO_API_DECISION_UNAVAILABLE;
    for ( Length = 0u; Length < MDO_DECISION_VALUE_CAPACITY && Value[Length] != '\0'; ++Length ) {}
    if ( Length == 0u || Length >= MDO_DECISION_VALUE_CAPACITY ||
         !xrtMutexLock(g_MdoDecisionLock) ) return MDO_API_DECISION_UNAVAILABLE;
    for ( i = 0u; i < MDO_DECISION_CACHE_MAX; ++i ) {
        MdoApiDecisionReceipt* Entry = &g_MdoDecisionReceipts[i];
        if ( Entry->ExpiresAt == 0u || MdoDeadlineExpired(Entry->ExpiresAt) ) {
            if ( Free == NULL ) Free = Entry;
            continue;
        }
        if ( strcmp(Entry->Token, Token) == 0 &&
             strlen(Entry->Path) == Context->Target.Path.Size &&
             memcmp(Entry->Path, Context->Target.Path.Data, Context->Target.Path.Size) == 0 ) {
            bool Equal = strcmp(Entry->Value, Value) == 0;
            xrtMutexUnlock(g_MdoDecisionLock);
            return Equal ? MDO_API_DECISION_REPLAY : MDO_API_DECISION_CONFLICT;
        }
    }
    if ( Free == NULL ) {
        xrtMutexUnlock(g_MdoDecisionLock);
        return MDO_API_DECISION_UNAVAILABLE;
    }
    xrtSecureZero(Free, sizeof(*Free));
    memcpy(Free->Token, Token, strlen(Token) + 1u);
    memcpy(Free->Path, Context->Target.Path.Data, Context->Target.Path.Size);
    memcpy(Free->Value, Value, Length + 1u);
    Attempt->Entry = Free;
    return MDO_API_DECISION_NEW;
}

void MdoApiDecisionEnd(MdoApiDecisionAttempt* Attempt, bool Applied)
{
    MdoApiDecisionReceipt* Entry;
    if ( Attempt == NULL ) return;
    Entry = (MdoApiDecisionReceipt*)Attempt->Entry;
    if ( Entry == NULL ) return;
    if ( Applied ) {
        uint64 Now = MdoMonotonicUs();
        Entry->ExpiresAt = Now > UINT64_MAX - MDO_DECISION_RETAIN_US ?
            UINT64_MAX : Now + MDO_DECISION_RETAIN_US;
    } else xrtSecureZero(Entry, sizeof(*Entry));
    Attempt->Entry = NULL;
    xrtMutexUnlock(g_MdoDecisionLock);
}

bool MdoApiDecisionReplyError(MdoApiContext* Context, MdoApiDecisionStatus Status)
{
    return MdoApiReplyError(Context,
        Status == MDO_API_DECISION_CONFLICT ? 409u : 503u,
        Status == MDO_API_DECISION_CONFLICT ? "decision_conflict" : "decisions_unavailable",
        Status == MDO_API_DECISION_CONFLICT ? "A different answer or decision was already accepted" :
            "Decision acknowledgements are temporarily unavailable; no new decision was applied", NULL);
}
