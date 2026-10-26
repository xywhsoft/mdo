/* One native execution lease, shared by explicit instance steps and routed
 * interactive runs. Admission is durable before C/model code is entered. */
struct MdoApplicationMemberTurn {
    mdo_sdk_context_v1 Context;
    MdoApplicationInvocation Invocation;
    char* Strings[10];
    xvalue *Params,*Config,*Message;
    char Key[128],Identity[65],Directory[160];
    MdoModuleCatalog* Modules;
    MdoApprovalScope* Scope;
    MdoAgentSession* Session;
    xcancel *Cancel,*ParentCancel;
    int Slot;
    bool Finished;
};
void MdoApplicationMemberTurnRelease(MdoApplicationMemberTurn* T){
    if(!T)return;
    MdoAgentSessionRelease(T->Session);
    MdoSessionPermissionScopeRelease(T->Context.ProjectId,SdkField(T->Config,"parent_session"),T->Scope);
    if(T->Slot>=0){xrtMutexLock(SdkState.Lock);SdkState.Members[T->Slot].Cancel=NULL;SdkState.Members[T->Slot].Identity[0]=0;
        xrtCondBroadcast(SdkState.Drained);xrtMutexUnlock(SdkState.Lock);}
    xrtCancelDestroy(T->Cancel);xrtCancelDestroy(T->ParentCancel);MdoModuleCatalogRelease(T->Modules);
    xrtValueRelease(T->Params);xrtValueRelease(T->Config);xrtValueRelease(T->Message);
    for(size_t I=0;I<10;I++)xrtFree(T->Strings[I]);xrtFree(T);
}
MdoAgentSession* MdoApplicationMemberTurnAgentRef(MdoApplicationMemberTurn* T){return T?MdoAgentSessionRef(T->Session):NULL;}
const char* MdoApplicationMemberTurnPrompt(MdoApplicationMemberTurn* T){return T?SdkField(SdkGet(T->Message,"body"),"prompt"):NULL;}
static bool SdkMemberTurnContext(MdoApplicationMemberTurn* T,const mdo_sdk_context_v1* C){
    T->Context=*C;MdoApplicationInvocation* I=C->Opaque;if(I)T->Invocation=*I;T->Context.Opaque=&T->Invocation;
    cstr Text[]={C->ExtensionId,C->InvocationId,C->DeviceId,C->ProjectId,C->SessionId,C->RunId,C->Workspace,C->Method,C->Namespace,C->MemberId};
    const char** Fields[]={&T->Context.ExtensionId,&T->Context.InvocationId,&T->Context.DeviceId,&T->Context.ProjectId,&T->Context.SessionId,
        &T->Context.RunId,&T->Context.Workspace,&T->Context.Method,&T->Context.Namespace,&T->Context.MemberId};
    for(size_t J=0;J<10;J++){T->Strings[J]=xrtStrDup(Text[J]?Text[J]:"");if(!T->Strings[J])return false;*Fields[J]=T->Strings[J];}
    T->Cancel=xrtCancelCreate();T->ParentCancel=I&&I->Cancel?xrtCancelRef(I->Cancel):NULL;T->Invocation.Cancel=T->ParentCancel;
    T->Modules=I&&I->Catalog?MdoModuleCatalogRef(I->Catalog):MdoModuleCatalogSnapshot();T->Invocation.Catalog=T->Modules;
    return T->Cancel&&T->Modules&&(!(I&&I->Cancel)||T->ParentCancel);
}
MdoApplicationMemberTurn* MdoApplicationMemberTurnOpen(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,
    bool* Ready,xwork_error* Error,mdo_error_v1* E){
    *Ready=false;xworkErrorInit(Error);bool Recover=strcmp(Method,"instance.step")!=0;
    cstr Id=SdkField(P,"id");if(!SdkStateKey(Id)||strlen(Id)>64){MdoApplicationError(E,"sdk_instance_arguments","A portable member id is required","instance",400,false);return NULL;}
    MdoApplicationMemberTurn* T=xrtCalloc(1,sizeof(*T));if(!T)return NULL;T->Slot=-1;
    if(!SdkMemberTurnContext(T,C))goto failed;
    T->Params=xrtValueClone(P);if(!T->Params)goto failed;
    C=&T->Context;P=T->Params;Id=SdkField(P,"id");snprintf(T->Key,sizeof(T->Key),"sys.instance.%s",Id);
    char Path[160];xvalue *Mail=xrtValueObject(),*Root=NULL;bool Claimed=false;
    if(!Mail)goto failed;
    xrtMutexLock(SdkState.Lock);Root=SdkStateLoad(C,P,Path,E);xvalue* Member=Root?SdkInstanceValue(Root,T->Key):NULL;
    if(!Root)goto unlock;
    if(!Member){MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);goto unlock;}
    if(!SdkMemberHistoryAllowed(C,Member)){MdoApplicationError(E,"sdk_instance_scope","Select a member owned by this conversation","instance",403,false);goto unlock;}
    xvalue* Current=SdkInstanceInfo(Member);cstr State=SdkField(Current,"state");
    bool Allowed=State&&!strcmp(State,Recover?"requires_review":"idle");xrtValueRelease(Current);
    if(!Allowed){MdoApplicationError(E,"sdk_instance_not_idle","Member is running, closed or needs an explicit resume/abandon decision","instance",409,false);goto unlock;}
    if(Recover){uint64 Expected=0,Revision=0;SdkUInt(SdkGet(Member,"revision"),&Revision);cstr MessageId=SdkField(P,"message_id"),Active=SdkField(Member,"active_message");
        if(!SdkUInt(SdkGet(P,"expected_revision"),&Expected)||Expected!=Revision||!Active||!MessageId||strcmp(MessageId,Active)){
            MdoApplicationError(E,"sdk_instance_conflict","Recovery requires the current member revision and active_message","instance",409,false);goto unlock;}}
    if(SdkState.Stopping){MdoApplicationError(E,"sdk_stopping","Member service is stopping","instance",503,false);goto unlock;}
    if(!SdkInstancePaths(Path,Id,T->Identity,T->Directory))goto unlock;
    if(SdkInstanceActive(T->Identity)>=0){MdoApplicationError(E,"sdk_instance_busy","Member is being inspected or run by another operation","instance",409,false);goto unlock;}
    int Slot=-1;for(int I=0;I<8;I++)if(!SdkState.Members[I].Cancel){Slot=I;break;}
    if(Slot<0){MdoApplicationError(E,"sdk_instance_busy","Eight member turns are active; wait for idle notification","instance",429,false);goto unlock;}
    SdkPut(Mail,"mailbox",SdkField(Member,"mailbox"));SdkPutUInt(Mail,"limit",1);
    if(Recover)SdkPut(Mail,"message_id",SdkField(Member,"active_message"));
    xvalue* Page=NULL;bool Changed=false;
    if(!SdkMailboxApply(C,Recover?"mailbox.get":"mailbox.read",Mail,Root,&Page,E,&Changed)){xrtValueRelease(Page);goto unlock;}
    const xvalue* Next=Recover?Page:xrtValueArrayGet(SdkGet(Page,"items"),0);
    if(!Next){xrtValueRelease(Page);MdoApplicationError(E,"sdk_instance_no_work","Member inbox has no pending turn","instance",409,false);goto unlock;}
    T->Message=xrtValueClone(Next);T->Config=xrtValueClone(Member);xrtValueRelease(Page);
    if(!T->Message||!T->Config)goto unlock;
    SdkPut(Member,"state","running");SdkPut(Member,"epoch",SdkState.Epoch);SdkPut(Member,"runtime_identity",T->Identity);SdkPut(Member,"active_message",SdkField(T->Message,"message_id"));
    Claimed=SdkInstanceStore(Root,T->Key,Member,E)&&SdkStateSave(Path,Root,E);
    if(Claimed){snprintf(SdkState.Members[Slot].Identity,65,"%s",T->Identity);SdkState.Members[Slot].Cancel=T->Cancel;T->Slot=Slot;}
unlock:
    xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);xrtValueRelease(Mail);if(!Claimed)goto failed;
    /* Never hold state/catalog locks across C role creation or callbacks. */
    MdoAgentSessionOptions O;MdoAgentSessionOptionsInit(&O);O.AgentId=SdkField(T->Config,"agent_id");O.ModelId=SdkField(T->Config,"model_id");
    O.ProjectId=C->ProjectId;O.Namespace=SdkField(T->Config,"namespace");O.MemberId=Id;
    char* Default=C->ProjectId&&!strcmp(C->ProjectId,"default")?MdoHomeDefaultWorkspacePath(true):NULL;
    O.WorkspaceRoot=Default?Default:SdkField(T->Config,"workspace");O.ProductSessionId=SdkField(T->Config,"parent_session");
    char Ask[33];memcpy(Ask,T->Identity,32);Ask[32]=0;O.AskScopeId=O.ProductSessionId&&O.ProductSessionId[0]?O.ProductSessionId:Ask;
    O.PermissionProfile=SdkField(T->Config,"permission_profile");O.AllowSubagentDefinition=true;O.ModuleCatalog=T->Modules;
    O.Cancel=T->Cancel;O.Deadline=C->Deadline?C->Deadline:MDO_DEADLINE_NEVER;O.UseRunPermissionScope=true;O.OnPermission=MdoApprovalOnPermission;
    bool Valid=true;MdoSessionInfo Info={0};Info.Size=sizeof(Info);
    if(O.ProductSessionId&&O.ProductSessionId[0]){
        MdoSession* Parent=MdoSessionLoad(C->ProjectId,O.ProductSessionId,Error);Valid=Parent&&MdoSessionGetInfo(Parent,&Info)&&Info.Status==MDO_SESSION_ACTIVE;MdoSessionRelease(Parent);
        if(Valid){O.PermissionProfile=Info.PermissionProfile;T->Scope=MdoSessionPermissionScopeAcquire(C->ProjectId,O.ProductSessionId,Error);O.LivePermissionScope=T->Scope;Valid=T->Scope!=NULL;}
    }
    char Snapshot[192],Journal[192];snprintf(Snapshot,sizeof(Snapshot),"%s/snapshot.json",T->Directory);snprintf(Journal,sizeof(Journal),"%s/journal.jsonl",T->Directory);
    bool Exists=false,JournalExists=false,DirectoryExists=false;xfileinfo Stat={0};
    Valid=Valid&&(MdoHomeCreateDirectory(T->Directory)||(MdoHomeExternalStat(T->Directory,&DirectoryExists,&Stat)&&DirectoryExists&&Stat.Type==XFILE_TYPE_DIRECTORY));
    Valid=Valid&&MdoHomeExternalStat(Snapshot,&Exists,&Stat)&&MdoHomeExternalStat(Journal,&JournalExists,&Stat);
    char *NativeSnapshot=MdoHomeExternalPath(Snapshot),*NativeJournal=MdoHomeExternalPath(Journal);
    Valid=Valid&&NativeSnapshot&&NativeJournal;O.SessionPath=NativeSnapshot;O.JournalPath=NativeJournal;O.ArtifactDirectory=T->Directory;O.Recover=Exists||JournalExists;O.PreserveRecoveryLedger=Recover;
    SdkMemberHistoryWriter* History=NULL;
    if(Valid&&strcmp(Method,"instance.abandon")){
        History=SdkMemberHistoryCreate(T->Directory,T->Message);Valid=History!=NULL;
        if(Valid){O.OnEvent=SdkMemberHistoryEvent;O.EventUserData=History;O.OwnerUserData=History;O.OnOwnerRetain=SdkMemberHistoryRetain;O.OnOwnerRelease=SdkMemberHistoryRelease;}
    }
    if(Valid)T->Session=MdoAgentSessionCreateWithRuntime(MdoBootstrapRuntime(),&O,Error);
    SdkMemberHistoryRelease(History);xrtFree(NativeSnapshot);xrtFree(NativeJournal);xrtFree(Default);
    bool Required=false;Valid=T->Session&&MdoAgentSessionRecoveryRequired(T->Session,&Required,Error);
    cstr Token=SdkField(P,"expected_recovery_token");
    if(Valid&&!strcmp(Method,"instance.resume")&&Token){
        uint64 Sequence=0;xwork_recovery_snapshot* Pending=MdoAgentSessionRecoverySnapshot(T->Session,Error);char Actual[MDO_AGENT_RECOVERY_TOKEN_CAPACITY],Epoch[65];
        Valid=(Pending||Error->eCode==XWORK_ERROR_NONE)&&SdkMemberRecoveryEpoch(T->Config,Epoch)&&MdoAgentSessionLastSequence(T->Session,&Sequence,Error)&&MdoAgentRecoverySnapshotToken(Pending,Required,Sequence,Epoch,Actual)&&!strcmp(Actual,Token);
        xworkRecoverySnapshotRelease(Pending);
        if(!Valid){Error->eCode=XWORK_ERROR_RECOVERY_REQUIRED;snprintf(Error->sMessage,sizeof(Error->sMessage),"Recovery details changed; inspect native calls again before deciding");}
    }
    if(Valid&&!Recover&&Required){Valid=false;Error->eCode=XWORK_ERROR_RECOVERY_REQUIRED;snprintf(Error->sMessage,sizeof(Error->sMessage),"Member requires an explicit recovery decision");}
    if(Valid)Valid=MdoAgentSessionCheckpoint(T->Session,Error);
    if(!Valid&&Error->eCode==XWORK_ERROR_NONE){Error->eCode=XWORK_ERROR_IO;snprintf(Error->sMessage,sizeof(Error->sMessage),"Cannot open the member's native execution state");}
    *Ready=Valid;return T;
failed:
    MdoApplicationMemberTurnRelease(T);return NULL;
}
bool MdoApplicationMemberTurnFinish(MdoApplicationMemberTurn* T,xwork_result Status,const xwork_run_result* Result,
    const xwork_error* Failure,xvalue** R,mdo_error_v1* E){
    if(!T||T->Finished)return MdoApplicationError(E,"sdk_instance_settled","Member execution lease was already settled","instance",409,false);
    const mdo_sdk_context_v1* C=&T->Context;const xvalue* P=T->Params;xwork_error Error,Check;xworkErrorInit(&Error);if(Failure)Error=*Failure;xworkErrorInit(&Check);
    bool Recovery=false;bool Checkpoint=T->Session&&MdoAgentSessionWithQuiescentCheckpoint(T->Session,NULL,NULL,&Check);
    if(Checkpoint&&!MdoAgentSessionRecoveryRequired(T->Session,&Recovery,&Check))Checkpoint=false;
    if(!Checkpoint&&Error.eCode==XWORK_ERROR_NONE)Error=Check;
    xvalue* Outcome=xrtValueObject();cstr State=Status==XWORK_RESULT_OK?"succeeded":Status==XWORK_RESULT_CANCELLED?"cancelled":Status==XWORK_RESULT_TIMEOUT?"timed_out":Status==XWORK_RESULT_LIMIT?"limited":"failed";
    if(!Checkpoint||Recovery)State="requires_review";
    SdkPut(Outcome,"outcome",State);SdkPut(Outcome,"message_id",SdkField(T->Message,"message_id"));SdkPut(Outcome,"error",Status==XWORK_RESULT_OK?"":Error.sMessage);SdkPutUInt(Outcome,"error_code",Error.eCode);
    if(Recovery&&T->Session){xwork_recovery_snapshot* Pending=MdoAgentSessionRecoverySnapshot(T->Session,&Check);xvalue* Calls=xrtValueArray();
        for(size_t I=0;Pending&&I<xworkRecoverySnapshotCount(Pending)&&I<64;I++){xwork_recovery_call_info Call;xworkRecoveryCallInfoInit(&Call);if(xworkRecoverySnapshotAt(Pending,I,&Call)){
            xvalue* V=xrtValueObject();SdkPut(V,"tool_call_id",Call.sToolCallId);SdkPut(V,"tool",Call.sToolName);xrtValueObjectSetNew(V,XRT_STR_LITERAL("automatic_retry_safe"),xrtValueBool(Call.bAutomaticRetrySafe));xrtValueArrayAppendNew(Calls,V);}}
        xworkRecoverySnapshotRelease(Pending);xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("pending_tools"),Calls);}
    cstr Text=Result&&Result->sFinalText?Result->sFinalText:"";size_t Bytes=strlen(Text),Keep=Bytes>8192?8192:Bytes;while(Keep<Bytes&&Keep&&(Text[Keep]&0xc0)==0x80)Keep--;
    xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Text,Keep)));xrtValueObjectSetNew(Outcome,XRT_STR_LITERAL("text_truncated"),xrtValueBool(Bytes>Keep));
    char Path[160];bool Ok=false;xrtMutexLock(SdkState.Lock);xvalue* Root=SdkStateLoad(C,P,Path,E),*Member=Root?SdkInstanceValue(Root,T->Key):NULL;
    if(Member){
        xvalue* Ack=xrtValueObject();SdkPut(Ack,"mailbox",SdkField(T->Config,"mailbox"));SdkPut(Ack,"message_id",SdkField(T->Message,"message_id"));xvalue* Receipt=NULL;bool Changed=false;
        Ok=SdkMailboxApply(C,"mailbox.ack",Ack,Root,&Receipt,E,&Changed);xrtValueRelease(Receipt);
        if(Ok){char MailKey[128];SdkMailboxIdentity(Ack,MailKey);xvalue* Mail=SdkInstanceValue(Root,MailKey);
            Ok=xrtValueObjectSet(Mail,XRT_STR_LITERAL("outcome"),Outcome)&&SdkPut(Member,"state",!strcmp(State,"requires_review")?"requires_review":"idle")&&
                xrtValueObjectSet(Member,XRT_STR_LITERAL("last_outcome"),Outcome)&&SdkInstanceStore(Root,T->Key,Member,E)&&SdkStateSave(Path,Root,E);}
        xrtValueRelease(Ack);if(Ok)*R=SdkInstanceInfo(Member);
    }
    if(T->Slot>=0){SdkState.Members[T->Slot].Cancel=NULL;SdkState.Members[T->Slot].Identity[0]=0;T->Slot=-1;xrtCondBroadcast(SdkState.Drained);}
    xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);xrtValueRelease(Outcome);T->Finished=true;
    if(Ok)SdkInstanceNotify(C,*R);return Ok;
}
