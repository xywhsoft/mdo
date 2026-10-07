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
        if(Ok)Ok=xrtValueArrayAppendNew(List,Q);else xrtValueRelease(Q);
    }
    if(Ok)Ok=xrtValueObjectSetNew(Out,XRT_STR_LITERAL("daily_quotas"),List);else xrtValueRelease(List);
    if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}

static bool MdoAccountModelAccess(xcancel* Cancel,MdoModelOnlineAccess* Access)
{
    MdoAccountLease* Lease=xrtCalloc(1,sizeof(*Lease));if(!Lease||!g_MdoAccount.Initialized){xrtFree(Lease);return false;}
    uint64 Until=xrtDeadlineAfter(30000000);bool Ok=false;
    xrtMutexLock(g_MdoAccount.Lock);++g_MdoAccount.Acquirers;
    while(!g_MdoAccount.Stopping&&!xrtDeadlineExpired(Until)&&(!Cancel||!xrtCancelRequested(Cancel))){
        if(MdoAccountLeaseLocked(Cancel,Lease)){Ok=true;break;}
        MdoAccountRelease(Lease);
        if(!g_MdoAccount.Tokens.Refresh[0]||!MdoAccountQueueLocked(MDO_ACCOUNT_WORK_REFRESH))break;
        xrtCondWaitFor(g_MdoAccount.Changed,g_MdoAccount.Lock,100000);
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
    g_MdoAccount.OnlineVersion[0]=0;g_MdoAccount.ModelsStatus=0;g_MdoAccount.NextProfile=0;
    (void)MdoModelManagerSetOnlineCatalog(NULL);
}
