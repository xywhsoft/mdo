/* A small shared task list, not a scheduler: callers decide who runs work.
 * The same atomic document owns dependencies, claims and submitted results. */
static xvalue* SdkWorkValue(const xvalue* Root,cstr Id){
    char Key[128];snprintf(Key,sizeof(Key),"sys.work.%s",Id);return (xvalue*)SdkGet(SdkGet(SdkGet(Root,"records"),Key),"value");
}
static bool SdkWorkReady(const xvalue* Root,const xvalue* Work){
    if(!SdkField(Work,"state")||strcmp(SdkField(Work,"state"),"pending"))return false;
    const xvalue* Dependencies=SdkGet(Work,"depends_on");
    for(size_t I=0;I<xrtValueCount(Dependencies);I++){
        const xvalue* Required=SdkWorkValue(Root,SdkText(xrtValueArrayGet(Dependencies,I)));cstr State=SdkField(Required,"state");
        if(!State||strcmp(State,"completed"))return false;
    }return true;
}
static xvalue* SdkWorkInfo(const xvalue* Root,const xvalue* Work,bool Detailed,bool Token){
    xvalue* R=xrtValueObject();const cstr Names[]={"id","title","description","state","worker","claim_id","reason"};
    for(size_t I=0;I<sizeof(Names)/sizeof(*Names);I++)if(SdkGet(Work,Names[I]))SdkPut(R,Names[I],SdkField(Work,Names[I]));
    SdkPutUInt(R,"revision",SdkStateRevision(Work));xrtValueObjectSetNew(R,XRT_STR_LITERAL("ready"),xrtValueBool(SdkWorkReady(Root,Work)));
    xrtValueObjectSet(R,XRT_STR_LITERAL("depends_on"),SdkGet(Work,"depends_on"));
    if(Detailed&&SdkGet(Work,"result"))xrtValueObjectSet(R,XRT_STR_LITERAL("result"),SdkGet(Work,"result"));
    if(Token)SdkPut(R,"claim_token",SdkField(Work,"claim_token"));return R;
}
static bool SdkWorkSave(xvalue* Root,cstr Id,xvalue* Work,mdo_error_v1* E){
    char Key[128];snprintf(Key,sizeof(Key),"sys.work.%s",Id);uint64 Revision=SdkStateRevision(Root);const xvalue* Records=SdkGet(Root,"records");
    if(Revision==UINT64_MAX||(!SdkGet(Records,Key)&&xrtValueCount(Records)>=SDK_STATE_RECORDS))return MdoApplicationError(E,"sdk_work_limit","Shared task storage budget exhausted","work",413,false);
    return SdkPutUInt(Work,"revision",Revision+1)&&SdkMailboxStore(Root,Key,Work,Revision+1,E);
}
static bool SdkWorkApply(cstr Method,const xvalue* P,xvalue* Root,xvalue** R,mdo_error_v1* E,bool* Changed){
    *Changed=false;cstr Id=SdkField(P,"id");const xvalue* Records=SdkGet(Root,"records");
    if(!strcmp(Method,"work.list")){
        uint64 Offset=0,Limit=32;if((SdkGet(P,"offset")&&!SdkUInt(SdkGet(P,"offset"),&Offset))||(SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>64)
            return MdoApplicationError(E,"sdk_work_arguments","offset/limit must be unsigned; limit is 1..64","work",400,false);
        uint64 Expected=0;if(SdkGet(P,"expected_revision")&&(!SdkUInt(SdkGet(P,"expected_revision"),&Expected)||Expected!=SdkStateRevision(Root)))return MdoApplicationError(E,"sdk_work_conflict","Task list changed during pagination","work",409,false);
        xvalue* Items=xrtValueArray();uint64 At=Offset;size_t Bytes=0;const xvalue* Filter=SdkGet(P,"ready_only");bool ReadyOnly=false;
        if(Filter&&!xrtValueGetBool(Filter,&ReadyOnly)){xrtValueRelease(Items);return MdoApplicationError(E,"sdk_work_arguments","ready_only must be boolean","work",400,false);}
        while(At<xrtValueCount(Records)){
            cstr Key=SdkKey(Records,(size_t)At);const xvalue* Work=SdkGet(SdkGet(Records,Key),"value");
            if(strncmp(Key,"sys.work.",9)||(ReadyOnly&&!SdkWorkReady(Root,Work))){At++;continue;}
            xvalue* Item=SdkWorkInfo(Root,Work,false,false);char* Json=SdkJson(Item);size_t Size=Json?strlen(Json):32769;xrtFree(Json);
            if(xrtValueCount(Items)>=Limit||Size>32768-Bytes){xrtValueRelease(Item);break;}
            xrtValueArrayAppendNew(Items,Item);Bytes+=Size;At++;
        }
        *R=xrtValueObject();SdkPutUInt(*R,"revision",SdkStateRevision(Root));SdkPutUInt(*R,"next",At);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("has_more"),xrtValueBool(At<xrtValueCount(Records)));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);return *R!=NULL;
    }
    if(!SdkStateKey(Id)||strlen(Id)>64)return MdoApplicationError(E,"sdk_work_arguments","A portable task id of at most 64 characters is required","work",400,false);
    xvalue* Work=SdkWorkValue(Root,Id);bool Create=!strcmp(Method,"work.create");
    if(Create){
        cstr Title=SdkField(P,"title"),Description=SdkField(P,"description");const xvalue* Dependencies=SdkGet(P,"depends_on");
        if(!Title||!Title[0]||strlen(Title)>512||(Description&&strlen(Description)>4096)||(Dependencies&&(xrtValueType(Dependencies)!=XVALUE_ARRAY||xrtValueCount(Dependencies)>32)))return MdoApplicationError(E,"sdk_work_arguments","Supply a title <=512 bytes, description <=4 KiB and up to 32 existing dependencies","work",400,false);
        xvalue* Intent=xrtValueObject();SdkPut(Intent,"id",Id);SdkPut(Intent,"title",Title);SdkPut(Intent,"description",Description?Description:"");xrtValueObjectSetNew(Intent,XRT_STR_LITERAL("depends_on"),Dependencies?xrtValueClone(Dependencies):xrtValueArray());
        char Hash[65];bool Ok=SdkMailboxHash(Intent,Hash);
        if(Work){bool Match=SdkField(Work,"intent_hash")&&!strcmp(SdkField(Work,"intent_hash"),Hash);xrtValueRelease(Intent);if(Match){*R=SdkWorkInfo(Root,Work,true,false);return *R!=NULL;}return MdoApplicationError(E,"sdk_identity_conflict","Task id belongs to different creation content","work",409,false);}
        for(size_t I=0;Ok&&I<xrtValueCount(Dependencies);I++){
            cstr Required=SdkText(xrtValueArrayGet(Dependencies,I));Ok=Required&&SdkStateKey(Required)&&strlen(Required)<=64&&strcmp(Required,Id)&&SdkWorkValue(Root,Required);
            for(size_t J=0;Ok&&J<I;J++)if(!strcmp(Required,SdkText(xrtValueArrayGet(Dependencies,J))))Ok=false;
        }
        if(!Ok){xrtValueRelease(Intent);return MdoApplicationError(E,"sdk_work_dependencies","Dependencies must be distinct existing tasks; create them first","work",400,false);}
        size_t Count=0;for(size_t I=0;I<xrtValueCount(Records);I++)if(!strncmp(SdkKey(Records,I),"sys.work.",9))Count++;
        if(Count>=128){xrtValueRelease(Intent);return MdoApplicationError(E,"sdk_work_limit","Execution namespace retains at most 128 tasks","work",413,false);}
        Ok=SdkPut(Intent,"intent_hash",Hash)&&SdkPut(Intent,"state","pending")&&SdkWorkSave(Root,Id,Intent,E);
        if(Ok)*R=SdkWorkInfo(Root,Intent,true,false);xrtValueRelease(Intent);*Changed=Ok;return Ok&&*R;
    }
    if(!Work)return MdoApplicationError(E,"sdk_work_missing","Shared task is unavailable","work",404,false);
    if(!strcmp(Method,"work.get")){*R=SdkWorkInfo(Root,Work,true,false);return *R!=NULL;}
    cstr State=SdkField(Work,"state");bool Claim=!strcmp(Method,"work.claim"),Cancel=!strcmp(Method,"work.cancel"),Complete=!strcmp(Method,"work.complete"),Release=!strcmp(Method,"work.release");
    if(Claim){
        cstr Worker=SdkField(P,"worker"),ClaimId=SdkField(P,"claim_id");
        if(!SdkStateKey(Worker)||!SdkStateKey(ClaimId))return MdoApplicationError(E,"sdk_work_arguments","worker and claim_id must be portable identities","work",400,false);
        if(!strcmp(State,"claimed")&&SdkField(Work,"worker")&&!strcmp(SdkField(Work,"worker"),Worker)&&SdkField(Work,"claim_id")&&!strcmp(SdkField(Work,"claim_id"),ClaimId)){
            *R=SdkWorkInfo(Root,Work,true,true);return *R!=NULL;}
        uint64 Revision=0;if(!SdkUInt(SdkGet(P,"expected_revision"),&Revision)||Revision!=SdkStateRevision(Work))return MdoApplicationError(E,"sdk_work_conflict","Task changed before it could be claimed","work",409,false);
        if(!SdkWorkReady(Root,Work))return MdoApplicationError(E,"sdk_work_not_ready","Task is claimed, ended or waiting on dependencies","work",409,false);
        char Token[33];if(!SdkCallIdentity(Token))return false;
        if(!SdkPut(Work,"worker",Worker)||!SdkPut(Work,"claim_id",ClaimId)||!SdkPut(Work,"claim_token",Token)||!SdkPut(Work,"state","claimed"))return false;
    }else if(Cancel){
        uint64 Revision=0;if(!SdkUInt(SdkGet(P,"expected_revision"),&Revision)||Revision!=SdkStateRevision(Work))return MdoApplicationError(E,"sdk_work_conflict","Task changed before cancellation","work",409,false);
        if(!strcmp(State,"completed"))return MdoApplicationError(E,"sdk_work_ended","Completed tasks are immutable","work",409,false);
        cstr Reason=SdkField(P,"reason");if(Reason&&strlen(Reason)>4096)return MdoApplicationError(E,"sdk_work_arguments","Cancellation reason exceeds 4 KiB","work",400,false);
        SdkPut(Work,"state","cancelled");SdkPut(Work,"claim_token","");if(Reason)SdkPut(Work,"reason",Reason);
    }else{
        cstr Token=SdkField(P,"claim_token"),Current=SdkField(Work,"claim_token");
        if(!Token||!Token[0]||!Current||strcmp(Token,Current))return MdoApplicationError(E,"sdk_work_stale_claim","Task claim was released, cancelled or belongs to another worker","work",409,false);
        if(Complete){
            const xvalue* Result=SdkGet(P,"result");char* Json=Result?SdkJson(Result):NULL;size_t Bytes=Json?strlen(Json):0;xrtFree(Json);
            if(!Result||Bytes>16384)return MdoApplicationError(E,"sdk_work_arguments","Completion needs a JSON result of at most 16 KiB","work",Bytes>16384?413:400,false);
            char Hash[65];if(!SdkMailboxHash(Result,Hash))return false;
            if(!strcmp(State,"completed")){if(SdkField(Work,"result_hash")&&!strcmp(SdkField(Work,"result_hash"),Hash)){*R=SdkWorkInfo(Root,Work,true,false);return *R!=NULL;}return MdoApplicationError(E,"sdk_identity_conflict","Task was completed with a different result","work",409,false);}
            if(strcmp(State,"claimed"))return MdoApplicationError(E,"sdk_work_not_claimed","Claim the task before completing it","work",409,false);
            SdkPut(Work,"state","completed");SdkPut(Work,"result_hash",Hash);xrtValueObjectSet(Work,XRT_STR_LITERAL("result"),Result);
        }else if(Release){
            if(!strcmp(State,"pending")){*R=SdkWorkInfo(Root,Work,true,false);return *R!=NULL;}
            if(strcmp(State,"claimed"))return MdoApplicationError(E,"sdk_work_ended","Ended task cannot be released","work",409,false);
            SdkPut(Work,"state","pending");
        }else return MdoApplicationError(E,"sdk_method_missing","Unknown shared task action","work",404,false);
    }
    bool Ok=SdkWorkSave(Root,Id,Work,E);if(Ok)*R=SdkWorkInfo(Root,Work,true,Claim);*Changed=Ok;return Ok&&*R;
}
