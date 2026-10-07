#ifndef MDO_ACCOUNT_INTERNAL_H
#define MDO_ACCOUNT_INTERNAL_H

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "../../include/mdo/account.h"
#include "../../include/mdo/config.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/http_url.h"
#include "../../include/mdo/secrets.h"

#define MDO_ACCOUNT_ORIGIN_LIMIT 513u
#define MDO_ACCOUNT_TOKEN_LIMIT 4097u
#define MDO_ACCOUNT_WAIT_MAX 16u
#define MDO_ACCOUNT_SESSION_PATH "data/account/session.bin"
#define MDO_ACCOUNT_PENDING_PATH "data/account/pending.bin"
#define MDO_ACCOUNT_CALLBACK_PATH "/api/v1/account/callback"
#define MDO_ACCOUNT_ANDROID_CALLBACK "https://ai.xywhsoft.com/app/mdo/callback"

typedef struct MdoAccountTokens {
    char Access[MDO_ACCOUNT_TOKEN_LIMIT];
    char Refresh[65];
    uint64 Expires;
    uint64 MemberId;
} MdoAccountTokens;
typedef struct MdoAccountAuthorization {
    char State[65], Verifier[65], Client[65];
    char Redirect[1024], Url[4096], Origin[MDO_ACCOUNT_ORIGIN_LIMIT];
    int64 Expires;
    bool Remember;
} MdoAccountAuthorization;
typedef struct MdoAccountWork {
    unsigned Kind;
    uint64 Epoch;
    char Origin[MDO_ACCOUNT_ORIGIN_LIMIT];
    MdoAccountTokens Tokens;
    MdoAccountAuthorization Authorization;
    char Code[65];
    char Identifier[255], Password[129]; /* Transient worker input; never persisted. */
    bool Remember;
} MdoAccountWork;
typedef struct MdoAccountWaiter {
    uint64 Id, RunId;
    bool Skipped;
    char Query[1001];
} MdoAccountWaiter;
typedef struct MdoAccountManager {
    xmutex* Lock;
    xcond* Changed;
    xthread* Worker;
    xcancel* WorkCancel;
    xcancel* SessionCancel;
    MdoAccountTokens Tokens;
    MdoAccountAuthorization Authorization;
    MdoAccountWork Work;
    xvalue* Profile;
    xvalue* Usage;
    xvalue* Allowance;
    uint64 NextProfile;
    uint16 ModelsStatus;
    char OnlineVersion[65];
    char Origin[MDO_ACCOUNT_ORIGIN_LIMIT];
    char Message[96];
    uint64 Epoch, NextWaiter, Revision, RefreshSerial;
    size_t Acquirers;
    uint16 SearchStatus;
    unsigned BusyKind;
    bool Initialized, Stopping, Busy, Remember, Saved;
    MdoAccountWaiter Waiters[MDO_ACCOUNT_WAIT_MAX];
} MdoAccountManager;

extern MdoAccountManager g_MdoAccount;
enum { MDO_ACCOUNT_WORK_NONE, MDO_ACCOUNT_WORK_EXCHANGE, MDO_ACCOUNT_WORK_REFRESH,
    MDO_ACCOUNT_WORK_PROFILE, MDO_ACCOUNT_WORK_LOGOUT, MDO_ACCOUNT_WORK_PASSWORD };

bool MdoAccountOrigin(cstr Url, char Output[MDO_ACCOUNT_ORIGIN_LIMIT]);
bool MdoAccountRandom(char Output[65]);
bool MdoAccountHex(cstr Text, size_t Length);
bool MdoAccountTokenValid(cstr Text);
bool MdoAccountSetString(xvalue* Object, cstr Key, cstr Value);
bool MdoAccountSetUInt(xvalue* Object, cstr Key, uint64 Value);
bool MdoAccountSetBool(xvalue* Object, cstr Key, bool Value);
bool MdoAccountGetUInt(const xvalue* Value, uint64* Output);
cstr MdoAccountText(const xvalue* Value, cstr Key, size_t Limit);
/* Only private parsed/constructed values: this clears string storage before
 * release. Never pass a public snapshot or a clone sharing secret scalars. */
void MdoAccountSecretValueRelease(xvalue* Value);
xvalue* MdoAccountClient(cstr Origin, cstr Path, cstr Method, const xvalue* Body,
    cstr Access, xcancel* Cancel, uint16* Status);
/* First-party service JSON. Fixed native authority, no redirect/retry; callers
 * constrain their own paths and inspect 200/201, including null-data replies. */
xvalue* MdoAccountServiceJson(cstr Path, cstr Method, const xvalue* Body,
    const MdoAccountLease* Lease, uint16* Status);
bool MdoAccountClientTokens(const xvalue* Data, MdoAccountTokens* Tokens);
bool MdoAccountCredentialWrite(cstr Path, cstr Origin, const xvalue* Value);
xvalue* MdoAccountCredentialRead(cstr Path, char Origin[MDO_ACCOUNT_ORIGIN_LIMIT]);
bool MdoAccountCredentialSaveLocked(void);
bool MdoAccountAuthorizationStart(cstr Origin, cstr LocalOrigin, bool Remember,
    MdoAccountAuthorization* Authorization);
bool MdoAccountAuthorizationSave(const MdoAccountAuthorization* Authorization);
bool MdoAccountAuthorizationLoad(MdoAccountAuthorization* Authorization);

#endif
