#include "../../include/mdo/time.h"
/* Filter quota facts before they reach the WebView. Unknown fields are never
 * copied; malformed replies cannot replace previously verified snapshots. */
static xvalue* MdoAccountAllowance(const xvalue* Data,uint64 Id)
{
    uint64 Owner,Expires,Updated;bool Vip=false;
    cstr Plan=MdoAccountText(Data,"plan_id",64),Group=MdoAccountText(Data,"group_id",64);
    const xvalue* Quotas=xrtValueObjectGet(Data,XRT_STR_LITERAL("daily_quotas"));
    if(!Plan||!Group||!MdoAccountGetUInt(xrtValueObjectGet(Data,XRT_STR_LITERAL("member_id")),&Owner)||Owner!=Id||
        !MdoAccountGetUInt(xrtValueObjectGet(Data,XRT_STR_LITERAL("plan_expires_at")),&Expires)||
        !MdoAccountGetUInt(xrtValueObjectGet(Data,XRT_STR_LITERAL("updated_at")),&Updated)||
        !xrtValueGetBool(xrtValueObjectGet(Data,XRT_STR_LITERAL("is_vip")),&Vip)||
        xrtValueType(Quotas)!=XVALUE_ARRAY||xrtValueCount(Quotas)>64)return NULL;
    xvalue* Out=xrtValueObject(),*List=xrtValueArray();
    bool Ok=Out&&List&&MdoAccountSetUInt(Out,"member_id",Owner)&&MdoAccountSetString(Out,"plan_id",Plan)&&
        MdoAccountSetString(Out,"group_id",Group)&&MdoAccountSetBool(Out,"is_vip",Vip)&&
        MdoAccountSetUInt(Out,"plan_expires_at",Expires)&&MdoAccountSetUInt(Out,"updated_at",Updated);
    for(size_t i=0;Ok&&i<xrtValueCount(Quotas);i++){
        const xvalue* Item=xrtValueArrayGet(Quotas,i);cstr Model=MdoAccountText(Item,"model_id",128),Title=MdoAccountText(Item,"title",128);
        uint64 Used,Held,Reset,Limit=0,Remaining=0;bool Unlimited=false;
        if(!Model||!Title||!MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("used_tokens")),&Used)||
            !MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("reserved_tokens")),&Held)||
            !MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("resets_at")),&Reset)||
            Used>9007199254740991ULL||Held>9007199254740991ULL||Reset>9007199254740991ULL||
            !xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("unlimited")),&Unlimited)){Ok=false;break;}
        if(!Unlimited&&(!MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("limit_tokens")),&Limit)||!Limit||Limit>1000000000000ULL||
            !MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("remaining_tokens")),&Remaining)||Remaining>Limit)){Ok=false;break;}
        xvalue* Q=xrtValueObject();Ok=Q&&MdoAccountSetString(Q,"model_id",Model)&&MdoAccountSetString(Q,"title",Title)&&
            MdoAccountSetUInt(Q,"used_tokens",Used)&&MdoAccountSetUInt(Q,"reserved_tokens",Held)&&MdoAccountSetUInt(Q,"resets_at",Reset)&&MdoAccountSetBool(Q,"unlimited",Unlimited);
        if(Ok)Ok=xrtValueObjectSetNew(Q,XRT_STR_LITERAL("limit_tokens"),Unlimited?xrtValueNull():xrtValueUInt(Limit))&&
            xrtValueObjectSetNew(Q,XRT_STR_LITERAL("remaining_tokens"),Unlimited?xrtValueNull():xrtValueUInt(Remaining));
        const xvalue* Pool=xrtValueObjectGet(Item,XRT_STR_LITERAL("token_pool"));
        if(Ok&&Pool){cstr Name=MdoAccountText(Item,"token_pool",64);uint64 Weight,Base,Peak,Available=0;bool Active=false;
            Ok=Name&&*Name&&MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("token_weight_bps")),&Weight)&&Weight>0&&Weight<=10000000&&
                MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("base_token_weight_bps")),&Base)&&Base>0&&Base<=1000000&&
                MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("token_peak_multiplier_bps")),&Peak)&&Peak>=10000&&Peak<=100000&&
                xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("peak_active")),&Active)&&
                (Unlimited||(MdoAccountGetUInt(xrtValueObjectGet(Item,XRT_STR_LITERAL("available_model_tokens")),&Available)&&Available<=9007199254740991ULL));
            if(Ok)Ok=MdoAccountSetString(Q,"token_pool",Name)&&MdoAccountSetUInt(Q,"token_weight_bps",Weight)&&
                MdoAccountSetUInt(Q,"base_token_weight_bps",Base)&&MdoAccountSetUInt(Q,"token_peak_multiplier_bps",Peak)&&MdoAccountSetBool(Q,"peak_active",Active)&&
                xrtValueObjectSetNew(Q,XRT_STR_LITERAL("available_model_tokens"),Unlimited?xrtValueNull():xrtValueUInt(Available));
        }
        if(Ok)Ok=xrtValueArrayAppendNew(List,Q);else xrtValueRelease(Q);
    }
    if(Ok)Ok=xrtValueObjectSetNew(Out,XRT_STR_LITERAL("daily_quotas"),List);else xrtValueRelease(List);
    if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}

static MdoModelLoginState MdoAccountModelStateLocked(void)
{
    bool Live=g_MdoAccount.Tokens.Access[0]&&!MdoDeadlineExpired(g_MdoAccount.Tokens.Expires);
    bool Pending=g_MdoAccount.Authorization.State[0]||
        g_MdoAccount.Work.Kind==MDO_ACCOUNT_WORK_PASSWORD||g_MdoAccount.Work.Kind==MDO_ACCOUNT_WORK_EXCHANGE||
        g_MdoAccount.Work.Kind==MDO_ACCOUNT_WORK_REFRESH||
        (g_MdoAccount.Busy&&(g_MdoAccount.BusyKind==MDO_ACCOUNT_WORK_PASSWORD||
            g_MdoAccount.BusyKind==MDO_ACCOUNT_WORK_EXCHANGE||g_MdoAccount.BusyKind==MDO_ACCOUNT_WORK_REFRESH));
    if(Pending||(!Live&&g_MdoAccount.Tokens.Refresh[0]))return MDO_MODEL_LOGIN_PENDING;
    if(!Live)return MDO_MODEL_LOGIN_REQUIRED;
    if(g_MdoAccount.OnlineVersion[0])return MDO_MODEL_LOGIN_READY;
    return g_MdoAccount.CatalogStatus?MDO_MODEL_LOGIN_UNAVAILABLE:MDO_MODEL_LOGIN_PENDING;
}
static MdoModelLoginState MdoAccountModelState(void)
{
    if(!g_MdoAccount.Initialized)return MDO_MODEL_LOGIN_REQUIRED;
    xrtMutexLock(g_MdoAccount.Lock);
    MdoModelLoginState State=MdoAccountModelStateLocked();
    xrtMutexUnlock(g_MdoAccount.Lock);return State;
}

static uint64 MdoAccountModelRecoveryRevision(void)
{
    if(!g_MdoAccount.Initialized)return 0;
    uint64 Hash=UINT64_C(1469598103934665603),Value=0;
    xrtMutexLock(g_MdoAccount.Lock);
    Hash^=(uint64)MdoAccountModelStateLocked();Hash*=UINT64_C(1099511628211);
    Hash^=g_MdoAccount.ModelCredentialRevision;Hash*=UINT64_C(1099511628211);
    const char* Labels[]={g_MdoAccount.OnlineVersion,MdoAccountText(g_MdoAccount.Allowance,"plan_id",64),MdoAccountText(g_MdoAccount.Allowance,"group_id",64)};
    for(size_t I=0;I<3;I++)for(const unsigned char* P=(const unsigned char*)(Labels[I]?Labels[I]:"");*P;P++){Hash^=*P;Hash*=UINT64_C(1099511628211);}
    Value=0;(void)MdoAccountGetUInt(xrtValueObjectGet(g_MdoAccount.Allowance,XRT_STR_LITERAL("plan_expires_at")),&Value);Hash^=Value;Hash*=UINT64_C(1099511628211);
    bool Vip=false;(void)xrtValueGetBool(xrtValueObjectGet(g_MdoAccount.Allowance,XRT_STR_LITERAL("is_vip")),&Vip);Hash^=(uint64)Vip;Hash*=UINT64_C(1099511628211);
    (void)MdoAccountGetUInt(xrtValueObjectGet(g_MdoAccount.Balance,XRT_STR_LITERAL("available_micros")),&Value);Hash^=Value;Hash*=UINT64_C(1099511628211);
    const xvalue* Quotas=xrtValueObjectGet(g_MdoAccount.Allowance,XRT_STR_LITERAL("daily_quotas"));
    const char* Fields[]={"remaining_tokens","reserved_tokens","resets_at","limit_tokens"};
    for(size_t I=0;I<xrtValueCount(Quotas);I++){
      const xvalue* Q=xrtValueArrayGet(Quotas,I);cstr Id=MdoAccountText(Q,"model_id",128);
      for(const unsigned char* P=(const unsigned char*)(Id?Id:"");*P;P++){Hash^=*P;Hash*=UINT64_C(1099511628211);}
      bool Unlimited=false;(void)xrtValueGetBool(xrtValueObjectGet(Q,XRT_STR_LITERAL("unlimited")),&Unlimited);Hash^=(uint64)Unlimited;Hash*=UINT64_C(1099511628211);
      for(size_t J=0;J<4;J++){
        Value=0;(void)MdoAccountGetUInt(xrtValueObjectGet(xrtValueArrayGet(Quotas,I),xrtStrView(Fields[J])),&Value);
        Hash^=Value;Hash*=UINT64_C(1099511628211);
      }
    }
    xrtMutexUnlock(g_MdoAccount.Lock);return Hash;
}

static bool MdoAccountModelPin(xcancel* Cancel,MdoModelOnlineAccess* Access)
{
    MdoAccountLease* Lease=xrtCalloc(1,sizeof(*Lease));
    if(!Lease||!g_MdoAccount.Initialized){xrtFree(Lease);return false;}
    bool Ok=false;
    xrtMutexLock(g_MdoAccount.Lock);
    uint64 Member=g_MdoAccount.Tokens.MemberId;
    if(!Member)(void)MdoAccountGetUInt(xrtValueObjectGet(g_MdoAccount.Profile,XRT_STR_LITERAL("id")),&Member);
    if(!g_MdoAccount.Stopping&&g_MdoAccount.SessionCancel&&Member&&
        (!Cancel||!xrtCancelRequested(Cancel))){
        Lease->Cancel=xrtCancelChild(Cancel);
        if(Lease->Cancel)Lease->Watch=xrtCancelWatch(g_MdoAccount.SessionCancel,MdoAccountCancelLease,Lease->Cancel);
        if(Lease->Watch){Lease->Generation=g_MdoAccount.Epoch;Lease->MemberId=Member;Lease->Managed=true;Ok=true;}
    }
    xrtMutexUnlock(g_MdoAccount.Lock);
    if(!Ok){MdoAccountRelease(Lease);xrtFree(Lease);return false;}
    Access->Cancel=Lease->Cancel;Access->Handle=Lease;return true;
}

static bool MdoAccountModelAccess(xcancel* Cancel,uint64 Deadline,MdoModelOnlineAccess* Access)
{
    MdoAccountLease* Lease=xrtCalloc(1,sizeof(*Lease));if(!Lease||!g_MdoAccount.Initialized){xrtFree(Lease);return false;}
    uint64 Until=MdoDeadlineAfterUs(30000000);bool Ok=false;uint32 RetryLimit=MdoRecoveryRetryLimit();
    if(Deadline!=MDO_DEADLINE_NEVER&&Deadline<Until)Until=Deadline;
    xrtMutexLock(g_MdoAccount.Lock);++g_MdoAccount.Acquirers;
    uint64 Epoch=g_MdoAccount.Epoch,Serial=g_MdoAccount.RefreshSerial;
    while(!g_MdoAccount.Stopping&&!MdoDeadlineExpired(Until)&&(!Cancel||!xrtCancelRequested(Cancel))){
        if(g_MdoAccount.Epoch!=Epoch)break;
        if(MdoAccountLeaseLocked(Cancel,Lease)){Ok=true;break;}
        /* Unwatch may wait for a cancellation callback. A partially created
         * lease is cleaned up after unlocking, never inside the account lock. */
        if(Lease->AccessToken||Lease->Cancel||Lease->Watch)break;
        /* One failed renewal is one product recovery attempt. Do not hide
         * repeated refreshes in a second, uncounted 30-second retry loop. */
        if(g_MdoAccount.RefreshSerial!=Serial)break;
        if(g_MdoAccount.RefreshFailures>RetryLimit&&!MdoDeadlineExpired(g_MdoAccount.NextRefresh))break;
        /* Switching during rotation discards the old refresh. Wait for the
         * pending login to settle so its session cancellation reaches the
         * pinned caller, rather than reporting a spurious login failure. */
        bool LoginPending=g_MdoAccount.Work.Kind==MDO_ACCOUNT_WORK_PASSWORD||
            g_MdoAccount.Work.Kind==MDO_ACCOUNT_WORK_EXCHANGE||
            (g_MdoAccount.Busy&&(g_MdoAccount.BusyKind==MDO_ACCOUNT_WORK_PASSWORD||
                g_MdoAccount.BusyKind==MDO_ACCOUNT_WORK_EXCHANGE));
        if(!LoginPending&&(!g_MdoAccount.Tokens.Refresh[0]||
            !MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH)))break;
        uint64 Now=MdoMonotonicUs();
        if(Now>=Until)break;
        uint64 Wait=Until-Now;
        xrtCondWaitFor(g_MdoAccount.Changed,g_MdoAccount.Lock, MdoDurationMs(Wait<100000u?Wait:100000u));
    }
    --g_MdoAccount.Acquirers;xrtCondBroadcast(g_MdoAccount.Changed);xrtMutexUnlock(g_MdoAccount.Lock);
    if(!Ok){MdoAccountRelease(Lease);xrtFree(Lease);return false;}
    Access->Token=Lease->AccessToken;Access->Cancel=Lease->Cancel;Access->Handle=Lease;return true;
}
static void MdoAccountModelRelease(MdoModelOnlineAccess* Access,int Status)
{
    MdoAccountLease* Lease=Access->Handle;if(Status==401)(void)MdoAccountRejectAccess(Lease);
    MdoAccountRelease(Lease);xrtFree(Lease);memset(Access,0,sizeof(*Access));
    if(!g_MdoAccount.Initialized)return;
    xrtMutexLock(g_MdoAccount.Lock);if(g_MdoAccount.Tokens.Access[0])g_MdoAccount.NextProfile=0;
    (void)MdoAccountQueueLocked(MDO_ACCOUNT_WORK_PROFILE);xrtMutexUnlock(g_MdoAccount.Lock);
}
static void MdoAccountClearModelsLocked(void)
{
    xrtValueRelease(g_MdoAccount.Allowance);g_MdoAccount.Allowance=NULL;
    g_MdoAccount.OnlineVersion[0]=0;g_MdoAccount.ModelsStatus=0;g_MdoAccount.CatalogStatus=0;g_MdoAccount.NextProfile=0;
    (void)MdoModelManagerSetOnlineCatalog(NULL);
}
