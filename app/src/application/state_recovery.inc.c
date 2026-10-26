/* Explicit recovery decisions need the actual native ledger, including tools
 * interrupted by process loss before a result receipt was saved. Inspection
 * never invokes a model or changes the member's durable admission/revision. */
static bool SdkMemberRecoveryEpoch(const xvalue* Config,char Epoch[65]){
    char* Text=xrtFormat("mdo-member-recovery\n%s\n%s\n%llu",SdkField(Config,"namespace"),SdkField(Config,"id"),(unsigned long long)SdkStateRevision(Config));
    bool Ok=Text&&MdoExtensionHash(Text,Epoch);xrtFree(Text);return Ok;
}
static bool SdkMemberRecovery(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    char Key[128],Path[160],Identity[65],Directory[160];snprintf(Key,sizeof(Key),"sys.instance.%s",SdkField(P,"id"));
    xvalue *Root=NULL,*Config=NULL;int Slot=-1;bool Ok=false;xcancel* Cancel=xrtCancelCreate();
    if(!Cancel)return false;
    xrtMutexLock(SdkState.Lock);Root=SdkStateLoad(C,P,Path,E);xvalue* Member=Root?SdkInstanceValue(Root,Key):NULL;
    if(!Root)goto unlock;
    if(!Member){MdoApplicationError(E,"sdk_instance_missing","Member is unavailable","instance",404,false);goto unlock;}
    if(!SdkMemberHistoryAllowed(C,Member)){MdoApplicationError(E,"sdk_instance_recovery_scope","Select a member owned by this conversation","instance",403,false);goto unlock;}
    if(!SdkInstancePaths(Path,SdkField(P,"id"),Identity,Directory))goto unlock;
    if(SdkInstanceActive(Identity)>=0){MdoApplicationError(E,"sdk_instance_busy","Wait for this member's active operation before inspecting recovery","instance",409,false);goto unlock;}
    if(SdkState.Stopping){MdoApplicationError(E,"sdk_stopping","Member service is stopping","instance",503,false);goto unlock;}
    for(int I=0;I<8;I++)if(!SdkState.Members[I].Cancel){Slot=I;break;}
    if(Slot<0){MdoApplicationError(E,"sdk_instance_busy","Member maintenance slots are occupied","instance",429,false);goto unlock;}
    Config=SdkInstanceInfo(Member);if(!Config){Slot=-1;goto unlock;}
    snprintf(SdkState.Members[Slot].Identity,65,"%s",Identity);SdkState.Members[Slot].Cancel=Cancel;
unlock:
    xrtValueRelease(Root);xrtMutexUnlock(SdkState.Lock);
    if(!Config)goto done;
    char Snapshot[192],Journal[192];snprintf(Snapshot,sizeof(Snapshot),"%s/snapshot.json",Directory);snprintf(Journal,sizeof(Journal),"%s/journal.jsonl",Directory);
    bool Exists=false,JournalExists=false;xfileinfo Stat={0};
    if(!MdoHomeExternalStat(Snapshot,&Exists,&Stat)||!MdoHomeExternalStat(Journal,&JournalExists,&Stat))goto done;
    *R=xrtValueObject();SdkPutUInt(*R,"revision",SdkStateRevision(Config));SdkPut(*R,"state",SdkField(Config,"state"));
    xrtValueObjectSetNew(*R,XRT_STR_LITERAL("available"),xrtValueBool(Exists||JournalExists));
    xvalue* Items=xrtValueArray();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("items"),Items);
    if(!Exists&&!JournalExists){Ok=true;goto done;}
    MdoModuleCatalog* Modules=MdoModuleCatalogSnapshot();MdoAgentSessionOptions Options;MdoAgentSessionOptionsInit(&Options);
    Options.AgentId=SdkField(Config,"agent_id");Options.ModelId=SdkField(Config,"model_id");Options.ProjectId=C->ProjectId;
    Options.Namespace=SdkField(Config,"namespace");Options.MemberId=SdkField(Config,"id");Options.AllowSubagentDefinition=true;
    Options.ProductSessionId=SdkField(Config,"parent_session");Options.AskScopeId=Options.ProductSessionId;
    Options.PermissionProfile=SdkField(Config,"permission_profile");Options.ModuleCatalog=Modules;
    char* DefaultWorkspace=C->ProjectId&&!strcmp(C->ProjectId,"default")?MdoHomeDefaultWorkspacePath(true):NULL;
    char *NativeSnapshot=MdoHomeExternalPath(Snapshot),*NativeJournal=MdoHomeExternalPath(Journal);
    Options.WorkspaceRoot=DefaultWorkspace?DefaultWorkspace:SdkField(Config,"workspace");
    Options.SessionPath=NativeSnapshot;Options.JournalPath=NativeJournal;Options.ArtifactDirectory=Directory;Options.Recover=true;Options.PreserveRecoveryLedger=true;
    Options.Cancel=Cancel;Options.Deadline=C->Deadline?C->Deadline:MDO_DEADLINE_NEVER;
    xwork_error Error;xworkErrorInit(&Error);MdoAgentSession* Session=NULL;
    if(Modules&&NativeSnapshot&&NativeJournal)Session=MdoAgentSessionCreateWithRuntime(MdoBootstrapRuntime(),&Options,&Error);
    bool Required=false;uint64 Sequence=0;xwork_recovery_snapshot* Pending=NULL;
    Ok=Session&&MdoAgentSessionRecoveryRequired(Session,&Required,&Error)&&MdoAgentSessionLastSequence(Session,&Sequence,&Error);
    if(Ok){Pending=MdoAgentSessionRecoverySnapshot(Session,&Error);Ok=Pending!=NULL||Error.eCode==XWORK_ERROR_NONE;}
    size_t Count=Pending?xworkRecoverySnapshotCount(Pending):0,Total=0;
    if(Count>64)Ok=false;
    for(size_t I=0;Ok&&I<Count;I++){
        xwork_recovery_call_info Call;memset(&Call,0,sizeof(Call));Call.uSize=sizeof(Call);Call.uAbiVersion=XWORK_ABI_VERSION;
        Ok=xworkRecoverySnapshotAt(Pending,I,&Call);if(!Ok)break;
        size_t Bytes=Call.sArgumentsJson?strlen(Call.sArgumentsJson):0;
        if(Bytes>16384||Total>131072-Bytes){Ok=false;break;}Total+=Bytes;
        xvalue* V=xrtValueObject();SdkPut(V,"tool_call_id",Call.sToolCallId);SdkPut(V,"tool",Call.sToolName);
        SdkPut(V,"arguments_json",Call.sArgumentsJson);SdkPutUInt(V,"effect_mask",Call.uEffects);
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("available"),xrtValueBool(Call.bToolAvailable));
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("automatic_retry_safe"),xrtValueBool(Call.bAutomaticRetrySafe));xrtValueArrayAppendNew(Items,V);
    }
    char Token[MDO_AGENT_RECOVERY_TOKEN_CAPACITY],Epoch[65];
    if(Ok)Ok=SdkMemberRecoveryEpoch(Config,Epoch)&&MdoAgentRecoverySnapshotToken(Pending,Required,Sequence,Epoch,Token);
    if(Ok){SdkPut(*R,"recovery_token",Token);SdkPutUInt(*R,"last_sequence",Sequence);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("resume_required"),xrtValueBool(Required));}
    xworkRecoverySnapshotRelease(Pending);MdoAgentSessionRelease(Session);MdoModuleCatalogRelease(Modules);
    xrtFree(NativeSnapshot);xrtFree(NativeJournal);xrtFree(DefaultWorkspace);
    if(!Ok)MdoApplicationError(E,"sdk_instance_recovery_unavailable",Error.sMessage[0]?Error.sMessage:"Native recovery details exceed the bounded view or cannot be opened","instance",409,false);
done:
    if(Slot>=0){xrtMutexLock(SdkState.Lock);SdkState.Members[Slot].Cancel=NULL;SdkState.Members[Slot].Identity[0]=0;xrtCondBroadcast(SdkState.Drained);xrtMutexUnlock(SdkState.Lock);}
    xrtValueRelease(Config);xrtCancelDestroy(Cancel);if(!Ok){xrtValueRelease(*R);*R=NULL;}return Ok;
}
