/* Public visible member history. The model ledger's recovery journal is
 * checkpointed away, so display records have a separate append-only sink.
 * This is the native conversation view, not a plugin-owned copy/database. */
#define SDK_MEMBER_HISTORY_RECORD 98304u
#define SDK_MEMBER_HISTORY_TEXT 8192u
#define SDK_MEMBER_HISTORY_PAGE 131072u
typedef struct SdkMemberHistoryWriter {
    xatomic32 Refs;
    xmutex* Lock;
    xfile File,Lease;
    char MessageId[129];
    char Source[16],Sender[257];
    MdoSessionEventSink Forward;
    xllm_session_stats Context;
    uint64 InputTokens, OutputTokens;
    bool HasContext, HasUsage;
} SdkMemberHistoryWriter;

static cstr SdkMemberHistoryKind(xwork_event_kind Kind){
    switch(Kind){
    case XWORK_EVENT_AGENT_START:return "agent_start";
    case XWORK_EVENT_MODEL_TEXT_DELTA:return "model_text_delta";
    case XWORK_EVENT_TOOL_START:return "tool_start";
    case XWORK_EVENT_TOOL_DONE:return "tool_done";
    case XWORK_EVENT_MODEL_DONE:return "model_done";
    case XWORK_EVENT_AGENT_DONE:return "agent_done";
    case XWORK_EVENT_ERROR:return "error";
    case XWORK_EVENT_COMPACTION_START:return "compaction_start";
    case XWORK_EVENT_COMPACTION_DONE:return "compaction_done";
    case XWORK_EVENT_COMPACTION_REJECTED:return "compaction_rejected";
    case XWORK_EVENT_RECOVERY_REQUIRED:return "recovery_required";
    case XWORK_EVENT_RECOVERY_RESOLVED:return "recovery_resolved";
    case XWORK_EVENT_ARTIFACT_CREATED:return "artifact_created";
    default:return NULL; /* No hidden reasoning, system prompt or wire body. */
    }
}
static bool SdkMemberHistoryOpen(SdkMemberHistoryWriter* W,cstr Directory,const xvalue* Message){
    memset(W,0,sizeof(*W));char Path[192];snprintf(Path,sizeof(Path),"%s/visible.jsonl",Directory);
    W->Lock=xrtMutexCreate();if(!W->Lock)return false;
    char LeasePath[192];snprintf(LeasePath,sizeof(LeasePath),"%s/visible.lock",Directory);
    W->Lease=MdoHomeOpenWrite(LeasePath,XFILE_READ|XFILE_CREATE|XFILE_SYNC);
    if(!W->Lease||!xrtFileLock(W->Lease,XFILE_LOCK_EXCLUSIVE,false))return false;
    /* The durable member turn claim owns this writer. Use a seekable handle
     * so recovery can remove an uncommitted suffix; append-only xrt handles
     * intentionally reject resize. Event writes are serialized by Lock. */
    W->File=MdoHomeOpenWrite(Path,XFILE_CREATE|XFILE_READ|XFILE_WRITE|XFILE_SYNC);
    if(!W->File)return false;
    snprintf(W->MessageId,sizeof(W->MessageId),"%s",SdkField(Message,"message_id")?SdkField(Message,"message_id"):"");
    snprintf(W->Sender,sizeof(W->Sender),"%s",SdkField(Message,"sender")?SdkField(Message,"sender"):"");
    cstr Origin=SdkField(SdkGet(Message,"body"),"origin");
    snprintf(W->Source,sizeof(W->Source),"%s",Origin&&!strcmp(Origin,"user")?"user":Origin&&!strcmp(Origin,"agent")?"agent":"unknown");
    xfileinfo Info={0};if(!xrtFileStat(W->File,&Info)||Info.Type!=XFILE_TYPE_FILE)return false;
    /* A torn final line was never committed/displayed. Recover only that
     * bounded suffix through the already anchored file handle. */
    if(Info.Size){
        size_t N=Info.Size>SDK_MEMBER_HISTORY_RECORD+1u?SDK_MEMBER_HISTORY_RECORD+1u:(size_t)Info.Size;
        char* Tail=xrtMalloc(N);if(!Tail)return false;
        uint64 Start=Info.Size-N;bool Ok=xrtSeek(W->File,(int64)Start,XSEEK_START,NULL)&&xrtReadFull(W->File,Tail,N,NULL);
        size_t Keep=N;while(Keep&&Tail[Keep-1]!='\n')--Keep;
        if(Ok&&Keep!=N)Ok=(Keep||!Start)&&xrtFileResize(W->File,Start+Keep)&&xrtFlush(W->File);
        xrtFree(Tail);if(!Ok)return false;
    }
    return xrtSeek(W->File,0,XSEEK_END,NULL);
}
static void SdkMemberHistoryDestroy(SdkMemberHistoryWriter* W){
    if(W->Forward.Release)W->Forward.Release(W->Forward.UserData);
    if(W->File)xrtClose(W->File);
    if(W->Lease)xrtClose(W->Lease);
    if(W->Lock)xrtMutexDestroy(W->Lock);
    xrtFree(W);
}
static SdkMemberHistoryWriter* SdkMemberHistoryCreate(cstr Directory,const xvalue* Message){
    SdkMemberHistoryWriter* W=xrtCalloc(1,sizeof(*W));if(!W)return NULL;
    if(!SdkMemberHistoryOpen(W,Directory,Message)){SdkMemberHistoryDestroy(W);return NULL;}
    xrtAtomic32Init(&W->Refs,1);return W;
}
static bool SdkMemberHistoryRetain(void* User){
    SdkMemberHistoryWriter* W=User;uint32 Refs=xrtAtomic32Load(&W->Refs,XMEMORY_ACQUIRE);
    for(;;){uint32 Expected=Refs;if(!Refs||Refs==UINT32_MAX)return false;
        if(xrtAtomic32CompareExchange(&W->Refs,&Expected,Refs+1,XMEMORY_ACQ_REL,XMEMORY_ACQUIRE))return true;
        Refs=Expected;
        }
}
static void SdkMemberHistoryRelease(void* User){
    SdkMemberHistoryWriter* W=User;if(W&&xrtAtomic32FetchSub(&W->Refs,1,XMEMORY_ACQ_REL)==1)SdkMemberHistoryDestroy(W);
}
static bool SdkMemberHistoryEvent(void* User,const xwork_event* Event){
    SdkMemberHistoryWriter* W=User;cstr Kind=SdkMemberHistoryKind(Event->eKind);
    xrtMutexLock(W->Lock);
    if(Event->uAgentDepth==0u&&Event->tSessionStats.uContextWindowTokens){W->Context=Event->tSessionStats;W->HasContext=true;}
    if(Event->uAgentDepth==0u&&Event->eKind==XWORK_EVENT_MODEL_DONE&&Event->bSuccess){
        W->InputTokens=Event->tUsage.uInputTokens;W->OutputTokens=Event->tUsage.uOutputTokens;W->HasUsage=true;
    }
    xrtMutexUnlock(W->Lock);
    if(!Kind)return !W->Forward.OnEvent||W->Forward.OnEvent(W->Forward.UserData,Event);
    const char* Text=Event->sText?Event->sText:"";
    size_t Size=Event->sText?(Event->iTextLength==SIZE_MAX?strlen(Text):Event->iTextLength):0;
    /* Non-display events never disclose diagnostic/provider text. */
    if(Event->eKind==XWORK_EVENT_MODEL_DONE||Event->eKind==XWORK_EVENT_COMPACTION_START||
        Event->eKind==XWORK_EVENT_COMPACTION_DONE||Event->eKind==XWORK_EVENT_COMPACTION_REJECTED)Size=0;
    bool Ok=true;xrtMutexLock(W->Lock);
    for(size_t At=0;Ok;){
        size_t N=Size-At;if(N>SDK_MEMBER_HISTORY_TEXT){N=SDK_MEMBER_HISTORY_TEXT;while(N&&((unsigned char)Text[At+N]&0xc0)==0x80)--N;}
        xvalue* V=xrtValueObject();SdkPut(V,"schema","mdo.instance.history.v1");SdkPut(V,"kind",Kind);
        SdkPut(V,"message_id",W->MessageId);SdkPutUInt(V,"source_event_id",Event->uEventId);
        SdkPut(V,"source",W->Source);SdkPut(V,"sender",W->Sender);
        SdkPutUInt(V,"run_id",Event->uRunId);SdkPutUInt(V,"agent_turn",Event->uAgentTurn);
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("occurred_at_us"),xrtValueInt(Event->iOccurredAtUs));
        SdkPutUInt(V,"text_offset",At);SdkPutUInt(V,"text_bytes",Size);
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("text_more"),xrtValueBool(At+N<Size));
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Text+At,N)));
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("source_truncated"),xrtValueBool(Event->bTextTruncated));
        cstr Keys[]={"tool_name","tool_call_id","artifact_path"};cstr Values[]={Event->sToolName,Event->sToolCallId,Event->sArtifactPath};
        for(size_t I=0;I<3;I++)if(Values[I]&&strlen(Values[I])<=4096)SdkPut(V,Keys[I],Values[I]);
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("success"),xrtValueBool(Event->bSuccess));
        SdkPutUInt(V,"input_tokens",Event->tUsage.uInputTokens);SdkPutUInt(V,"output_tokens",Event->tUsage.uOutputTokens);
        char* Json=SdkJson(V);xrtValueRelease(V);size_t Bytes=Json?strlen(Json):0;
        if(Json)Json[Bytes]='\n';
        Ok=Json&&Bytes<=SDK_MEMBER_HISTORY_RECORD&&xrtWriteFull(W->File,Json,Bytes+1,NULL)&&xrtFlush(W->File);xrtFree(Json);
        At+=N;if(At>=Size)break;if(!N){Ok=false;break;}
    }
    xrtMutexUnlock(W->Lock);if(Ok&&W->Forward.OnEvent)Ok=W->Forward.OnEvent(W->Forward.UserData,Event);
    if(!Ok)xrtSetErrorKind(XERR_IO);
    return Ok;
}
static bool SdkMemberHistoryAllowed(const mdo_sdk_context_v1* C,const xvalue* Member){
    cstr Parent=SdkField(Member,"parent_session"),Owner=SdkField(Member,"owner");
    bool SameParent=Parent&&Parent[0]?C->SessionId&&!strcmp(C->SessionId,Parent):
        Owner&&C->ExtensionId&&!strcmp(Owner,C->ExtensionId);
    if(!SameParent)return false;
    if(C->MemberId&&C->MemberId[0])return C->Namespace&&SdkField(Member,"namespace")&&
        !strcmp(C->Namespace,SdkField(Member,"namespace"))&&!strcmp(C->MemberId,SdkField(Member,"id"));
    return true;
}
static bool SdkMemberHistory(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Id=SdkField(P,"id");uint64 After=0,Limit=32;
    if(!SdkStateKey(Id)||strlen(Id)>64||(SdkGet(P,"after")&&!SdkUInt(SdkGet(P,"after"),&After))||
        (SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>64||After>INT64_MAX)
        return MdoApplicationError(E,"sdk_instance_history_arguments","Supply a member id, byte cursor and limit 1..64","instance",400,false);
    char Key[128],StatePath[160],Identity[65],Directory[160],Path[192];snprintf(Key,sizeof(Key),"sys.instance.%s",Id);
    xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,StatePath,E);xvalue* Member=Root?SdkInstanceValue(Root,Key):NULL;
    xvalue* Current=Member?SdkInstanceInfo(Member):NULL;bool Found=Member!=NULL,Allowed=Member&&SdkMemberHistoryAllowed(C,Member);
    xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);
    if(!Found){xrtValueRelease(Current);return E->Code[0]?false:MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);}
    if(!Allowed){xrtValueRelease(Current);return MdoApplicationError(E,"sdk_instance_history_scope","Member history belongs to another owning session or member","instance",403,false);}
    bool HasFormat=SdkField(Current,"history_format")!=NULL;SdkInstancePaths(StatePath,Id,Identity,Directory);
    snprintf(Path,sizeof(Path),"%s/visible.jsonl",Directory);bool Exists=false;xfileinfo Info={0};
    if(!MdoHomeExternalStat(Path,&Exists,&Info)){xrtValueRelease(Current);return MdoApplicationError(E,"sdk_instance_history_io","Cannot inspect member history","instance",503,false);}
    xfile File=Exists?MdoHomeOpenRead(Path):NULL;
    if(Exists&&(!File||!xrtFileStat(File,&Info)||Info.Type!=XFILE_TYPE_FILE||Info.Size>INT64_MAX)){
        if(File)xrtClose(File);
        xrtValueRelease(Current);
        return MdoApplicationError(E,"sdk_instance_history_io","Cannot open member history","instance",503,false);
        }
    uint64 Size=Exists?Info.Size:0;bool Ok=After<=Size;char Previous=0;
    if(Ok&&After)Ok=xrtSeek(File,(int64)After-1,XSEEK_START,NULL)&&xrtReadFull(File,&Previous,1,NULL)&&Previous=='\n';
    if(!Ok){if(File)xrtClose(File);xrtValueRelease(Current);return MdoApplicationError(E,"sdk_instance_history_cursor","History cursor changed or is not a committed record boundary","instance",409,false);}
    xvalue* Items=xrtValueArray();char* Record=xrtMalloc(SDK_MEMBER_HISTORY_RECORD+1u);uint64 Position=After,Cursor=After;size_t Bytes=0;bool Pending=false;
    if(!Record)Ok=false;
    while(Ok&&Position<Size&&xrtValueCount(Items)<Limit){
        uint64 Start=Position;size_t Used=0;bool Complete=false;
        while(Position<Size&&!Complete){
            char Block[8192];size_t N=Size-Position>sizeof(Block)?sizeof(Block):(size_t)(Size-Position);
            if(!xrtSeek(File,(int64)Position,XSEEK_START,NULL)||!xrtReadFull(File,Block,N,NULL)){Ok=false;break;}
            char* End=memchr(Block,'\n',N);size_t Keep=End?(size_t)(End-Block):N;
            if(Keep>SDK_MEMBER_HISTORY_RECORD-Used){Ok=false;break;}
            memcpy(Record+Used,Block,Keep);Used+=Keep;Position+=Keep+(End?1:0);Complete=End!=NULL;
        }
        if(!Ok)break;
        if(!Complete){Pending=true;Position=Start;break;}
        if(Bytes+Used>SDK_MEMBER_HISTORY_PAGE&&xrtValueCount(Items)){Position=Start;break;}
        Record[Used]=0;xvalue* V=SdkParse(Record);
        if(!V||!SdkField(V,"schema")||strcmp(SdkField(V,"schema"),"mdo.instance.history.v1")){xrtValueRelease(V);Ok=false;break;}
        SdkPutUInt(V,"cursor",Position);xrtValueArrayAppendNew(Items,V);Bytes+=Used;Cursor=Position;
    }
    xrtFree(Record);if(File)xrtClose(File);
    if(Ok){*R=xrtValueObject();SdkPut(*R,"id",Id);SdkPut(*R,"state",SdkField(Current,"state"));
        SdkPutUInt(*R,"next_cursor",Cursor);SdkPutUInt(*R,"latest_bytes",Size);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("available"),xrtValueBool(HasFormat||Exists));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("complete_from_creation"),xrtValueBool(HasFormat));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("has_more"),xrtValueBool(!Pending&&Position<Size));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("pending_tail"),xrtValueBool(Pending));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);Items=NULL;
    }
    xrtValueRelease(Items);xrtValueRelease(Current);
    return Ok||MdoApplicationError(E,"sdk_instance_history_corrupt","Visible member history is unreadable; no older snapshot was substituted","instance",503,false);
}
