static bool SdkMailboxIdentity(const xvalue* P,char Key[128]){
    cstr Box=SdkField(P,"mailbox"),Id=SdkField(P,"message_id");
    if(!SdkStateKey(Box)||strlen(Box)>96||!SdkStateKey(Id))return false;
    char* Text=xrtFormat("%s\n%s",Box,Id);char Hash[65];bool Ok=Text&&MdoExtensionHash(Text,Hash);xrtFree(Text);
    if(Ok)snprintf(Key,128,"sys.mail.%s",Hash);return Ok;
}
static bool SdkMailboxHash(const xvalue* P,char Hash[65]){
    xvalue* Canonical=SdkCanonicalValue(P,0);char* Text=Canonical?SdkJson(Canonical):NULL;
    bool Ok=Text&&MdoExtensionHash(Text,Hash);xrtValueRelease(Canonical);xrtFree(Text);return Ok;
}
static xvalue* SdkMailboxInfo(const xvalue* Message,bool Body){
    xvalue* R=xrtValueObject();const cstr Names[]={"mailbox","message_id","sender","state"};
    for(size_t I=0;I<4;I++)SdkPut(R,Names[I],SdkField(Message,Names[I]));
    uint64 Sequence=0,Ack=0;SdkUInt(SdkGet(Message,"sequence"),&Sequence);SdkUInt(SdkGet(Message,"ack_revision"),&Ack);
    SdkPutUInt(R,"sequence",Sequence);SdkPutUInt(R,"ack_revision",Ack);
    if(Body&&SdkGet(Message,"body"))xrtValueObjectSetNew(R,XRT_STR_LITERAL("body"),xrtValueClone(SdkGet(Message,"body")));return R;
}
static xvalue* SdkMailboxAccepted(const xvalue* Message){
    xvalue* R=xrtValueObject();SdkPut(R,"mailbox",SdkField(Message,"mailbox"));SdkPut(R,"message_id",SdkField(Message,"message_id"));
    uint64 Sequence=0;SdkUInt(SdkGet(Message,"sequence"),&Sequence);SdkPutUInt(R,"sequence",Sequence);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("accepted"),xrtValueBool(true));return R;
}
static bool SdkMailboxStore(xvalue* Root,cstr Key,xvalue* Message,uint64 Revision,mdo_error_v1* E){
    xvalue* Entry=xrtValueObject();bool Ok=Entry&&SdkPutUInt(Entry,"revision",Revision)&&
        xrtValueObjectSetNew(Entry,XRT_STR_LITERAL("deleted"),xrtValueBool(false))&&
        xrtValueObjectSet(Entry,XRT_STR_LITERAL("value"),Message)&&
        xrtValueObjectSet((xvalue*)SdkGet(Root,"records"),xrtStrView(Key),Entry)&&SdkPutUInt(Root,"revision",Revision);
    xrtValueRelease(Entry);return Ok||MdoApplicationError(E,"sdk_state_allocation","Cannot construct message publication","mailbox",500,false);
}
static bool SdkMailboxApply(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue* Root,xvalue** R,mdo_error_v1* E,bool* Changed){
    *Changed=false;cstr Box=SdkField(P,"mailbox");
    if(!SdkStateKey(Box)||strlen(Box)>96)return MdoApplicationError(E,"sdk_mailbox_arguments","A portable mailbox identity is required","mailbox",400,false);
    const xvalue* Records=SdkGet(Root,"records");bool Read=!strcmp(Method,"mailbox.read");
    if(Read){
        uint64 After=0,Limit=32;if((SdkGet(P,"after")&&!SdkUInt(SdkGet(P,"after"),&After))||
            (SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>64)
            return MdoApplicationError(E,"sdk_mailbox_arguments","Invalid message cursor/limit","mailbox",400,false);
        *R=xrtValueObject();xvalue* Items=xrtValueArray();size_t Bytes=0;uint64 Cursor=After>SdkStateRevision(Root)?SdkStateRevision(Root):After;bool More=false;
        for(size_t I=0;I<xrtValueCount(Records);I++){
            cstr Key=SdkKey(Records,I);if(strncmp(Key,"sys.mail.",9))continue;
            const xvalue* M=SdkGet(SdkGet(Records,Key),"value");uint64 Sequence=0;
            if(!SdkField(M,"mailbox")||strcmp(SdkField(M,"mailbox"),Box)||!SdkUInt(SdkGet(M,"sequence"),&Sequence)||Sequence<=After)continue;
            if(SdkField(M,"state")&&!strcmp(SdkField(M,"state"),"pending")){
                xvalue* Item=SdkMailboxInfo(M,true);char* Json=Item?SdkJson(Item):NULL;size_t Size=Json?strlen(Json):32769;xrtFree(Json);
                if(xrtValueCount(Items)>=Limit||Size>32768-Bytes){xrtValueRelease(Item);More=true;break;}
                xrtValueArrayAppendNew(Items,Item);Bytes+=Size;
            }Cursor=Sequence;
        }
        if(!More&&SdkStateRevision(Root)>Cursor)Cursor=SdkStateRevision(Root);
        SdkPutUInt(*R,"cursor",Cursor);SdkPutUInt(*R,"latest",SdkStateRevision(Root));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("resync_required"),xrtValueBool(After>SdkStateRevision(Root)));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("has_more"),xrtValueBool(More));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);return *R!=NULL;
    }
    char Key[128];if(!SdkMailboxIdentity(P,Key))return MdoApplicationError(E,"sdk_mailbox_arguments","A portable message_id is required","mailbox",400,false);
    const xvalue* Existing=SdkGet(Records,Key),*Old=SdkGet(Existing,"value");
    if(!strcmp(Method,"mailbox.get")){
        if(!Old)return MdoApplicationError(E,"sdk_message_missing","Message is unavailable","mailbox",404,false);
        *R=SdkMailboxInfo(Old,true);return *R!=NULL;
    }
    bool Send=!strcmp(Method,"mailbox.send");uint64 Revision=SdkStateRevision(Root);
    if(Revision==UINT64_MAX||(!Existing&&xrtValueCount(Records)>=SDK_STATE_RECORDS))return MdoApplicationError(E,"sdk_mailbox_limit","Execution namespace has exhausted its retained message/state budget","mailbox",413,false);
    if(Send){
        const xvalue* Body=SdkGet(P,"body");char* Encoded=Body?SdkJson(Body):NULL;size_t Size=Encoded?strlen(Encoded):0;xrtFree(Encoded);
        if(!Body)return MdoApplicationError(E,"sdk_mailbox_arguments","Message body is required","mailbox",400,false);
        if(Size>8192)return MdoApplicationError(E,"sdk_mailbox_limit","Message body exceeds 8 KiB; send an artifact reference","mailbox",413,false);
        cstr Sender=C->MemberId&&C->MemberId[0]?C->MemberId:C->SessionId&&C->SessionId[0]?C->SessionId:C->ExtensionId;
        xvalue* Identity=xrtValueObject();SdkPut(Identity,"mailbox",Box);SdkPut(Identity,"message_id",SdkField(P,"message_id"));SdkPut(Identity,"sender",Sender);xrtValueObjectSet(Identity,XRT_STR_LITERAL("body"),Body);
        char Hash[65];bool Hashed=SdkMailboxHash(Identity,Hash);xrtValueRelease(Identity);if(!Hashed)return false;
        if(Old){
            if(!SdkField(Old,"hash")||strcmp(SdkField(Old,"hash"),Hash))return MdoApplicationError(E,"sdk_identity_conflict","message_id belongs to different content or sender","mailbox",409,false);
            *R=SdkMailboxAccepted(Old);return *R!=NULL;
        }
        size_t Pending=0,Bytes=0;
        for(size_t I=0;I<xrtValueCount(Records);I++){
            cstr K=SdkKey(Records,I);if(strncmp(K,"sys.mail.",9))continue;
            const xvalue* M=SdkGet(SdkGet(Records,K),"value");uint64 N=0;
            if(SdkField(M,"mailbox")&&!strcmp(SdkField(M,"mailbox"),Box)&&SdkField(M,"state")&&!strcmp(SdkField(M,"state"),"pending")){
                Pending++;SdkUInt(SdkGet(M,"body_bytes"),&N);Bytes+=(size_t)N;
            }
        }
        if(Pending>=128||Bytes>262144||Size>262144-Bytes)return MdoApplicationError(E,"sdk_mailbox_full","Recipient unread budget reached; acknowledge or drain messages","mailbox",429,false);
        xvalue* M=xrtValueObject();bool Ok=SdkPut(M,"mailbox",Box)&&SdkPut(M,"message_id",SdkField(P,"message_id"))&&SdkPut(M,"sender",Sender)&&SdkPut(M,"state","pending")&&
            SdkPut(M,"hash",Hash)&&SdkPutUInt(M,"sequence",Revision+1)&&SdkPutUInt(M,"body_bytes",Size)&&xrtValueObjectSet(M,XRT_STR_LITERAL("body"),Body)&&SdkMailboxStore(Root,Key,M,Revision+1,E);
        if(Ok)*R=SdkMailboxAccepted(M);xrtValueRelease(M);*Changed=Ok;return Ok&&*R;
    }
    if(!Old)return MdoApplicationError(E,"sdk_message_missing","Message is unavailable","mailbox",404,false);
    char Hash[65];if(!SdkMailboxHash(P,Hash))return false;
    if(SdkField(Old,"state")&&!strcmp(SdkField(Old,"state"),"acked")){
        if(!SdkField(Old,"ack_hash")||strcmp(SdkField(Old,"ack_hash"),Hash))return MdoApplicationError(E,"sdk_identity_conflict","Acknowledgement belongs to different state changes","mailbox",409,false);
        *R=xrtValueClone(SdkGet(Old,"ack_result"));return *R!=NULL;
    }
    if(SdkGet(P,"lease_guard")&&!SdkLeaseGuard(Root,SdkGet(P,"lease_guard"),E))return false;
    xvalue* Message=xrtValueClone(Old);bool Ok=Message!=NULL;const xvalue* Writes=SdkGet(P,"writes"),*Checks=SdkGet(P,"checks");
    if(Ok&&Writes){xvalue* Changes=NULL;Ok=SdkStateApply(Root,Writes,Checks,&Changes,E);xrtValueRelease(Changes);Revision=SdkStateRevision(Root);}
    else if(Ok){
        if(Checks&&(xrtValueType(Checks)!=XVALUE_ARRAY||xrtValueCount(Checks)>64))Ok=MdoApplicationError(E,"sdk_mailbox_arguments","Invalid acknowledgement checks","mailbox",400,false);
        for(size_t I=0;Ok&&I<xrtValueCount(Checks);I++)Ok=SdkStateCheck(Records,xrtValueArrayGet(Checks,I),E);
        Revision++;
    }
    if(Ok){
        *R=xrtValueObject();SdkPut(*R,"mailbox",Box);SdkPut(*R,"message_id",SdkField(P,"message_id"));SdkPutUInt(*R,"ack_revision",Revision);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("acknowledged"),xrtValueBool(true));
        Ok=SdkPut(Message,"state","acked")&&SdkPut(Message,"ack_hash",Hash)&&SdkPutUInt(Message,"ack_revision",Revision)&&
            xrtValueObjectSetNew(Message,XRT_STR_LITERAL("body"),xrtValueNull())&&xrtValueObjectSet(Message,XRT_STR_LITERAL("ack_result"),*R)&&SdkMailboxStore(Root,Key,Message,Revision,E);
    }xrtValueRelease(Message);*Changed=Ok;return Ok&&*R;
}
static bool SdkMailboxWait(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    uint64 Timeout=0;if((SdkGet(P,"timeout_ms")&&!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout))||Timeout>60000)return MdoApplicationError(E,"sdk_mailbox_arguments","timeout_ms must be 0..60000","mailbox",400,false);
    uint64 End=MdoDeadlineAfterUs(Timeout*1000);
    for(;;){
        xvalue* Events=MdoApplicationEvents(NULL,E);if(!Events)return false;
        xvalue* Wait=xrtValueObject();xrtValueObjectSet(Wait,XRT_STR_LITERAL("after"),SdkGet(Events,"latest"));xrtValueObjectSet(Wait,XRT_STR_LITERAL("epoch"),SdkGet(Events,"epoch"));SdkPut(Wait,"topic","mailbox.changed");xrtValueRelease(Events);
        bool Found=false;bool Ok=MdoApplicationStateCall(C,"mailbox.read",P,R,E,&Found);
        bool Resync=false;xrtValueGetBool(SdkGet(*R,"resync_required"),&Resync);
        if(!Ok||Resync||xrtValueCount(SdkGet(*R,"items"))||!Timeout||MdoDeadlineExpired(End)){xrtValueRelease(Wait);return Ok;}
        xrtValueRelease(*R);*R=NULL;uint64 Remaining=MdoDeadlineRemainingUs(End);uint64 Ms=(Remaining+999)/1000;if(Ms>1000)Ms=1000;
        SdkPutUInt(Wait,"timeout_ms",Ms);Events=MdoApplicationEventsWait(C,Wait,E);xrtValueRelease(Wait);if(!Events)return false;xrtValueRelease(Events);
    }
}
