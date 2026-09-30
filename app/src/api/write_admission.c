#include <stdio.h>
#include <string.h>

#include "write_admission.h"

static xmutex* g_MdoWriteLock;
static char g_MdoWriteNonce[33];
static uint64 g_MdoWriteGeneration;
static size_t g_MdoWriteReaders;
static bool g_MdoWriteExclusive;

bool MdoApiWriteInit(void)
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Bytes[16];
    size_t i;
    if ( g_MdoWriteLock != NULL ) return true;
    if ( !xrtSecureRandom(Bytes, sizeof(Bytes)) ) return false;
    g_MdoWriteLock = xrtMutexCreate();
    if ( g_MdoWriteLock == NULL ) return false;
    for ( i = 0u; i < sizeof(Bytes); ++i ) {
        g_MdoWriteNonce[2u * i] = Hex[Bytes[i] >> 4u];
        g_MdoWriteNonce[2u * i + 1u] = Hex[Bytes[i] & 15u];
    }
    g_MdoWriteNonce[32] = '\0';
    g_MdoWriteGeneration = 0u; g_MdoWriteReaders = 0u; g_MdoWriteExclusive = false;
    return true;
}

void MdoApiWriteUnit(void)
{
    /* The host drains requests before API teardown, as for its other locks. */
    if ( g_MdoWriteLock != NULL ) xrtMutexDestroy(g_MdoWriteLock);
    g_MdoWriteLock = NULL;
    memset(g_MdoWriteNonce, 0, sizeof(g_MdoWriteNonce));
}

static void MdoWriteTokenLocked(char Token[MDO_API_WRITE_TOKEN_CAPACITY])
{
    snprintf(Token, MDO_API_WRITE_TOKEN_CAPACITY, "%s-%llu", g_MdoWriteNonce,
        (unsigned long long)g_MdoWriteGeneration);
}

bool MdoApiWriteToken(char Token[MDO_API_WRITE_TOKEN_CAPACITY])
{
    if ( g_MdoWriteLock == NULL ) return false;
    xrtMutexLock(g_MdoWriteLock); MdoWriteTokenLocked(Token); xrtMutexUnlock(g_MdoWriteLock);
    return true;
}

/* Called under the short admission mutex. It performs no reply or I/O. */
static int MdoWriteExpected(const MdoApiContext* Context)
{
    const xhttpfield* Field = NULL;
    xhttpnext Next = xrtHttpFieldGetUnique(Context->Request->head->Fields,
        Context->Request->head->FieldCount, XRT_STR_LITERAL("X-Mdo-Write-Token"), &Field);
    char Current[MDO_API_WRITE_TOKEN_CAPACITY];
    xstrview Expected;
    if ( Next == XHTTP_NEXT_END ) return 428;
    if ( Next != XHTTP_NEXT_ITEM || Field == NULL ) return 400;
    Expected = xrtStrTrim(Field->Value); MdoWriteTokenLocked(Current);
    if ( Expected.Size != strlen(Current) || memcmp(Expected.Data, Current, Expected.Size) != 0 ) return 412;
    return 200;
}

static bool MdoWriteReject(MdoApiContext* Context, int Status)
{
    cstr Code = Status == 428 ? "write_token_required" : Status == 400 ? "write_token_invalid" :
        Status == 412 ? "write_token_conflict" : Status == 409 ? "write_admission_busy" : "write_admission_unavailable";
    (void)MdoApiReplyError(Context, (uint16)Status, Code,
        Status == 409 ? "Wait for accepted writes before removing a project" :
        Status == 412 ? "This page predates project removal or a host restart; reload before writing" :
        "Use the current write token before changing data", NULL);
    return false;
}

bool MdoApiWriteEnter(MdoApiContext* Context, bool Exclusive, bool Stop)
{
    int Status = 200;
    if ( g_MdoWriteLock == NULL ) return MdoWriteReject(Context, 503);
    xrtMutexLock(g_MdoWriteLock);
    if ( g_MdoWriteExclusive || (Exclusive && g_MdoWriteReaders != 0u) ) Status = 409;
    else if ( !Exclusive && !Stop ) Status = MdoWriteExpected(Context);
    if ( Status == 200 ) {
        if ( Exclusive ) { g_MdoWriteExclusive = true; Context->WriteExclusive = true; }
        else if ( g_MdoWriteReaders == SIZE_MAX ) Status = 503;
        else { ++g_MdoWriteReaders; Context->WriteShared = true; }
    }
    xrtMutexUnlock(g_MdoWriteLock);
    return Status == 200 || MdoWriteReject(Context, Status);
}

void MdoApiWriteLeave(MdoApiContext* Context)
{
    if ( !Context->WriteShared && !Context->WriteExclusive ) return;
    xrtMutexLock(g_MdoWriteLock);
    if ( Context->WriteExclusive ) g_MdoWriteExclusive = false;
    else --g_MdoWriteReaders;
    Context->WriteExclusive = false; Context->WriteShared = false;
    xrtMutexUnlock(g_MdoWriteLock);
}

bool MdoApiWriteInvalidate(MdoApiContext* Context)
{
    int Status = 503;
    if ( g_MdoWriteLock == NULL || !Context->WriteExclusive ) return MdoWriteReject(Context, Status);
    xrtMutexLock(g_MdoWriteLock);
    if ( g_MdoWriteExclusive && g_MdoWriteReaders == 0u ) {
        Status = MdoWriteExpected(Context);
        if ( Status == 200 ) {
            if ( g_MdoWriteGeneration == UINT64_MAX ) Status = 503;
            else ++g_MdoWriteGeneration;
        }
    }
    xrtMutexUnlock(g_MdoWriteLock);
    return Status == 200 || MdoWriteReject(Context, Status);
}
