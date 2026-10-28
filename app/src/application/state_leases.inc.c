static bool SdkLeaseKey(cstr Id,char Key[128]){
    if(!SdkStateKey(Id)||strlen(Id)>96)return false;
    snprintf(Key,128,"sys.lease.%s",Id);return true;
}
static bool SdkLeaseLive(const xvalue* Value){
    bool Held=false;uint64 Expires=0;cstr Epoch=SdkField(Value,"epoch");
    return xrtValueGetBool(SdkGet(Value,"held"),&Held)&&Held&&Epoch&&!strcmp(Epoch,SdkState.Epoch)&&
        SdkUInt(SdkGet(Value,"expires_us"),&Expires)&&!MdoDeadlineExpired(Expires);
}
static bool SdkLeaseGuard(const xvalue* Root,const xvalue* Guard,mdo_error_v1* E){
    char Key[128];uint64 Fence=0;cstr Token=SdkField(Guard,"token");
    if(!SdkLeaseKey(SdkField(Guard,"id"),Key)||!SdkUInt(SdkGet(Guard,"fence"),&Fence)||!Token)
        return MdoApplicationError(E,"sdk_lease_arguments","Lease guard requires id, fence and token","lease",400,false);
    const xvalue* Value=SdkGet(SdkGet(SdkGet(Root,"records"),Key),"value");uint64 Current=0;
    return SdkLeaseLive(Value)&&SdkUInt(SdkGet(Value,"fence"),&Current)&&Current==Fence&&SdkField(Value,"token")&&!strcmp(Token,SdkField(Value,"token"))
        ?true:MdoApplicationError(E,"sdk_lease_stale","Lease expired, restarted or belongs to another execution","lease",409,false);
}
static xvalue* SdkLeaseInfo(cstr Id,const xvalue* Value,bool Token){
    xvalue* R=xrtValueObject();SdkPut(R,"id",Id);
    uint64 Fence=0,Expires=0;SdkUInt(SdkGet(Value,"fence"),&Fence);SdkUInt(SdkGet(Value,"expires_us"),&Expires);
    SdkPutUInt(R,"fence",Fence);SdkPutUInt(R,"expires_us",Expires);SdkPut(R,"epoch",SdkField(Value,"epoch"));SdkPut(R,"owner",SdkField(Value,"owner"));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("valid"),xrtValueBool(SdkLeaseLive(Value)));
    if(Token)SdkPut(R,"token",SdkField(Value,"token"));
    return R;
}
static bool SdkLeaseApply(cstr Method,const xvalue* P,xvalue* Root,xvalue** R,mdo_error_v1* E,bool* Changed){
    cstr Id=SdkField(P,"id");char Key[128];*Changed=false;
    if(!SdkLeaseKey(Id,Key))return MdoApplicationError(E,"sdk_lease_arguments","A portable lease id of at most 96 characters is required","lease",400,false);
    xvalue* Records=(xvalue*)SdkGet(Root,"records");const xvalue* Entry=SdkGet(Records,Key),*Value=SdkGet(Entry,"value");
    bool Inspect=!strcmp(Method,"lease.inspect"),Validate=!strcmp(Method,"lease.validate"),Acquire=!strcmp(Method,"lease.acquire"),Release=!strcmp(Method,"lease.release");
    if(Inspect||Validate){
        if(Validate&&!SdkLeaseGuard(Root,P,E))return false;
        *R=SdkLeaseInfo(Id,Value,false);return *R!=NULL;
    }
    if(!SdkStateKey(SdkField(P,"request_id")))return MdoApplicationError(E,"sdk_lease_arguments","Lease mutations require request_id","lease",400,false);
    char Hash[65]={0};int Replayed=SdkStateRequest(Root,Method,P,R,Hash,E);if(Replayed)return Replayed>0;
    uint64 Ttl=30000;
    if((SdkGet(P,"ttl_ms")&&!SdkUInt(SdkGet(P,"ttl_ms"),&Ttl))||!Ttl||Ttl>300000)return MdoApplicationError(E,"sdk_lease_arguments","ttl_ms must be 1..300000","lease",400,false);
    if(Acquire){
        if(!SdkStateKey(SdkField(P,"owner")))return MdoApplicationError(E,"sdk_lease_arguments","A portable owner identity is required","lease",400,false);
        if(SdkLeaseLive(Value))return MdoApplicationError(E,"sdk_lease_busy","Another execution holds this lease","lease",409,false);
    }else if(!SdkLeaseGuard(Root,P,E))return false;
    uint64 Revision=SdkStateRevision(Root);
    if(Revision==UINT64_MAX||(!Entry&&xrtValueCount(Records)>=SDK_STATE_RECORDS))return MdoApplicationError(E,"sdk_state_limit","Lease namespace budget exhausted","lease",413,false);
    xvalue* New=Acquire?xrtValueObject():xrtValueClone(Value);bool Ok=New!=NULL;
    if(Acquire){char Token[33];Ok=Ok&&SdkCallIdentity(Token)&&SdkPut(New,"token",Token)&&SdkPut(New,"owner",SdkField(P,"owner"))&&SdkPut(New,"epoch",SdkState.Epoch)&&SdkPutUInt(New,"fence",Revision+1);}
    Ok=Ok&&xrtValueObjectSetNew(New,XRT_STR_LITERAL("held"),xrtValueBool(!Release))&&SdkPutUInt(New,"expires_us",Release?0:MdoDeadlineAfterUs(Ttl*1000));
    if(Ok){*R=SdkLeaseInfo(Id,New,!Release);xvalue* Next=xrtValueObject();
        Ok=*R&&Next&&SdkPutUInt(Next,"revision",Revision+1)&&xrtValueObjectSetNew(Next,XRT_STR_LITERAL("deleted"),xrtValueBool(false))&&
            xrtValueObjectSet(Next,XRT_STR_LITERAL("value"),New)&&xrtValueObjectSet(Records,xrtStrView(Key),Next)&&SdkPutUInt(Root,"revision",Revision+1)&&SdkStateRemember(Root,P,Hash,*R);
        xrtValueRelease(Next);
    }
    xrtValueRelease(New);
    if(!Ok){xrtValueRelease(*R);*R=NULL;return MdoApplicationError(E,"sdk_state_allocation","Cannot construct lease mutation; no changes published","lease",500,false);}
    *Changed=true;return true;
}
