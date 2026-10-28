static bool MdoAgentsCanDelegate(const MdoModuleAgentInfo* Parent,const MdoModuleAgentInfo* Child)
{
    if(!(Parent->Flags&MDO_AGENT_ALLOW_DELEGATION))return false;
    xvalue* Config=Parent->ConfigurationJson?xrtJsonParse(xrtStrView(Parent->ConfigurationJson)):NULL;
    const xvalue* Delegation=MdoAgentConfigGet(Config,"delegation");
    cstr Short=!strncmp(Child->Id,"subagent.",9)?Child->Id+9:!strncmp(Child->Id,"agent.",6)?Child->Id+6:Child->Id;
    const xvalue* Allow=MdoAgentConfigGet(Delegation,"allow"),*Deny=MdoAgentConfigGet(Delegation,"deny");
    bool Ok=(MdoAgentsListMatch(Allow,Child->Id,true)||MdoAgentsListMatch(Allow,Short,false)) &&
        !MdoAgentsListMatch(Deny,Child->Id,false) && !MdoAgentsListMatch(Deny,Short,false);
    xrtValueRelease(Config);return Ok;
}
static bool MdoAgentsApplyMcpSelection(xwork_agent* Agent,cstr Workspace,const MdoModuleAgentInfo* Definition,xwork_error* Error)
{
    xvalue* Config=Definition->ConfigurationJson?xrtJsonParse(xrtStrView(Definition->ConfigurationJson)):NULL;
    const xvalue* Selected=MdoAgentConfigGet(Config,"mcp_servers");bool Ok=true;
    if(Selected){
        const char* Ids[128];size_t N=xrtValueCount(Selected);
        for(size_t I=0;I<N;I++){xstrview Id;xrtValueGetString(xrtValueArrayGet(Selected,I),&Id);Ids[I]=Id.Data;}
        Ok=MdoMcpConfigureRole(Agent,Workspace,Ids,N,Error);
    }
    xrtValueRelease(Config);return Ok;
}
static MdoAgentInstance* MdoAgentsInstance(MdoAgentOwner* Owner,xwork_agent_extension_context* Native,xwork_error* Error)
{
    if(!Native->sDefinitionId)return NULL;
    xrtMutexLock(Owner->InstanceLock);
    for(MdoAgentInstance* I=Owner->Instances;I;I=I->Next)if(I->Agent==Native->pAgent){xrtMutexUnlock(Owner->InstanceLock);return I;}
    MdoAgentInstance* Instance=xrtCalloc(1,sizeof(*Instance));
    if(Instance){
        Instance->Agent=Native->pAgent;Instance->Model=Owner->RootModel;Instance->Definition.Size=sizeof(Instance->Definition);
        if(!MdoModuleCatalogAgentFind(Owner->Modules,Native->sDefinitionId,&Instance->Definition)){xrtFree(Instance);Instance=NULL;}
    }
    if(Instance){
        Instance->Configuration=Instance->Definition.ConfigurationJson?xrtJsonParse(xrtStrView(Instance->Definition.ConfigurationJson)):xrtValueObject();
        Instance->State=xrtValueObject();
        /* Preserve recovered pre-v1 pinned contexts, while all new v1 roles
         * rebuild from their independently addressable sources. */
        if(!Instance->Definition.HasProgram && !MdoAgentConfigGet(Instance->Configuration,"schema") &&
            !MdoAgentConfigGet(Instance->Configuration,"context") && Native->pAgent==Owner->RootAgent){
            cstr Previous=xllmSessionGetSystemPrompt(Native->pSession);
            Instance->LegacyPrompt=Previous?xrtStrDup(Previous):NULL;
        }
        if(!Instance->Configuration||!Instance->State){xrtValueRelease(Instance->Configuration);xrtValueRelease(Instance->State);xrtFree(Instance->LegacyPrompt);xrtFree(Instance);Instance=NULL;}
    }
    if(Instance){Instance->Next=Owner->Instances;Owner->Instances=Instance;}
    xrtMutexUnlock(Owner->InstanceLock);
    if(!Instance)MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"cannot bind the role's owned runtime state");
    return Instance;
}
static void MdoAgentsFrame(MdoAgentOwner* Owner,MdoAgentInstance* Instance,
    xwork_agent_extension_context* Native,xvalue* Plan,mdo_agent_context_v1* Frame)
{
    memset(Frame,0,sizeof(*Frame));Frame->Size=sizeof(*Frame);Frame->AbiVersion=MDO_MODULE_ABI_VERSION;
    Frame->AgentId=Instance->Definition.Id;Frame->ProjectId=Owner->ProductProject;
    Frame->SessionId=Owner->ProductSession;Frame->WorkspaceRoot=Owner->Workspace;
    Frame->ConfigurationJson=Instance->Definition.ConfigurationJson;
    Frame->Namespace=Owner->Namespace;Frame->MemberId=Owner->MemberId;
    Frame->CancelToken=Native->pCancel;Frame->Deadline=Native->uDeadline;
    Frame->State=Instance->State;Frame->ContextPlan=Plan;Frame->Native=Native;
}
static bool MdoAgentsProgramEvent(MdoAgentOwner* Owner,MdoAgentInstance* Instance,
    xwork_agent_extension_context* Native,xvalue* Plan,mdo_agent_phase Phase,xwork_error* Error)
{
    mdo_agent_context_v1 Frame;MdoAgentsFrame(Owner,Instance,Native,Plan,&Frame);
    return MdoModuleCatalogAgentEvent(Owner->Modules,Instance->Definition.Id,Phase,&Frame,Error);
}
static bool MdoAgentsRefreshContext(MdoAgentOwner*,MdoAgentInstance*,xwork_agent_extension_context*,bool,xwork_error*);
static bool MdoAgentsHistoryConfigure(MdoAgentInstance*,xwork_agent_extension_context*,bool);
static void MdoAgentsLifecycleNotify(MdoAgentOwner* Owner,MdoAgentActiveFrame* Active,mdo_agent_phase Phase,const xwork_event* Event){
    mdo_agent_context_v1 Frame;MdoAgentsFrame(Owner,Active->Instance,Active->Native,Active->Instance->Plan,&Frame);Frame.Event=Event;
    if((Phase>=MDO_AGENT_RUN_FINISHED&&Phase<=MDO_AGENT_RUN_CANCELLED)||Phase==MDO_AGENT_DELEGATE_FINISHED){
        Frame.OutcomeAvailable=true;Frame.Outcome=Active->Status==XWORK_RESULT_OK?MDO_RESULT_OK:Active->Status==XWORK_RESULT_CANCELLED?MDO_RESULT_CANCELLED:Active->Status==XWORK_RESULT_TIMEOUT?MDO_RESULT_TIMEOUT:Active->Status==XWORK_RESULT_LIMIT?MDO_RESULT_LIMIT:MDO_RESULT_ERROR;
        Frame.Error=Active->Error;
    }
    xwork_error Ignored;xworkErrorInit(&Ignored);(void)MdoModuleCatalogAgentEvent(Owner->Modules,Frame.AgentId,Phase,&Frame,&Ignored);
}
static void MdoAgentsLifecycleRuntime(MdoAgentOwner* Owner,const xwork_event* Event){
    mdo_agent_phase Phase=0;
    switch(Event->eKind){
    case XWORK_EVENT_AGENT_START:Phase=MDO_AGENT_RUN_STARTED;break;
    case XWORK_EVENT_AGENT_DONE:Phase=Event->bSuccess?MDO_AGENT_RUN_FINISHED:MDO_AGENT_RUN_FAILED;break;
    case XWORK_EVENT_COMPACTION_START:Phase=MDO_AGENT_COMPACTION_STARTED;break;
    case XWORK_EVENT_COMPACTION_DONE:Phase=MDO_AGENT_COMPACTION_FINISHED;break;
    case XWORK_EVENT_COMPACTION_REJECTED:Phase=MDO_AGENT_COMPACTION_REJECTED;break;
    default:return;
    }
    /* The active stack binds callbacks to their actual synchronous run scope.
     * Nested foreground children push a new frame; background children own a
     * different thread. No workspace or role-name guessing is involved. */
    uint64 Thread=xrtThreadCurrentId();MdoAgentActiveFrame* Active=NULL;
    xrtMutexLock(Owner->InstanceLock);
    for(MdoAgentActiveFrame* A=Owner->ActiveFrames;A;A=A->Next)if(A->Thread==Thread){Active=A;break;}
    bool Notify=Active!=NULL;
    if(Active&&Phase==MDO_AGENT_RUN_STARTED){Notify=!Active->Started;Active->Started=true;}
    if(Active&&Event->eKind==XWORK_EVENT_AGENT_DONE){
        Active->Settled=true;Notify=false;
        xwork_event* D=&Active->Completion;memset(D,0,sizeof(*D));D->eKind=Event->eKind;D->uSchemaVersion=Event->uSchemaVersion;D->uEventId=Event->uEventId;
        D->uAgentId=Event->uAgentId;D->uRunId=Event->uRunId;D->uParentRunId=Event->uParentRunId;D->uAgentDepth=Event->uAgentDepth;D->uTaskId=Event->uTaskId;D->uDelegationId=Event->uDelegationId;
        D->bSuccess=Event->bSuccess;D->iOccurredAtUs=Event->iOccurredAtUs;D->tUsage=Event->tUsage;D->bMetadataTruncated=true;
    }
    xrtMutexUnlock(Owner->InstanceLock);
    if(Notify){
        if(Phase==MDO_AGENT_RUN_STARTED&&Active->Instance->Agent!=Owner->RootAgent)MdoAgentsLifecycleNotify(Owner,Active,MDO_AGENT_DELEGATE_ACCEPTED,Event);
        MdoAgentsLifecycleNotify(Owner,Active,Phase,Event);
    }
}
static xwork_result MdoAgentsLifecycleRun(MdoAgentOwner* Owner,MdoAgentInstance* Instance,xwork_agent_extension_context* Native,xwork_error* Error){
    MdoAgentActiveFrame Active={0};Active.Instance=Instance;Active.Native=Native;Active.Thread=xrtThreadCurrentId();Active.Error=Error;
    xrtMutexLock(Owner->InstanceLock);Active.Next=Owner->ActiveFrames;Owner->ActiveFrames=&Active;xrtMutexUnlock(Owner->InstanceLock);
    Native->bHandled=true;Instance->RunModelCalls=0;xwork_result Status=XWORK_RESULT_ERROR;
    if(MdoAgentsProgramEvent(Owner,Instance,Native,Instance->Plan,MDO_AGENT_RUN_PREPARE,Error)&&
        MdoAgentsHistoryConfigure(Instance,Native,true)&&MdoAgentsRefreshContext(Owner,Instance,Native,true,Error)){
        if(Instance->Definition.HasDriver){
            Active.Started=true;if(Instance->Agent!=Owner->RootAgent)MdoAgentsLifecycleNotify(Owner,&Active,MDO_AGENT_DELEGATE_ACCEPTED,NULL);MdoAgentsLifecycleNotify(Owner,&Active,MDO_AGENT_RUN_STARTED,NULL);
            mdo_agent_context_v1 Frame;MdoAgentsFrame(Owner,Instance,Native,Instance->Plan,&Frame);
            Status=MdoModuleCatalogAgentDrive(Owner->Modules,Frame.AgentId,&Frame,Error);
        }else Status=Native->DefaultRun(Native->pAgent,Native->sPrompt,Native->pUserMessage,Native->bResume,Native->pResumeOptions,Native->pResult,Error);
    }
    Active.Status=Status;mdo_agent_phase Phase=Status==XWORK_RESULT_OK?MDO_AGENT_RUN_FINISHED:Status==XWORK_RESULT_CANCELLED?MDO_AGENT_RUN_CANCELLED:MDO_AGENT_RUN_FAILED;
    MdoAgentsLifecycleNotify(Owner,&Active,Phase,Active.Settled?&Active.Completion:NULL);
    if(Instance->Agent!=Owner->RootAgent)MdoAgentsLifecycleNotify(Owner,&Active,MDO_AGENT_DELEGATE_FINISHED,Active.Settled?&Active.Completion:NULL);
    xrtMutexLock(Owner->InstanceLock);MdoAgentActiveFrame** At=&Owner->ActiveFrames;while(*At&&*At!=&Active)At=&(*At)->Next;if(*At)*At=Active.Next;xrtMutexUnlock(Owner->InstanceLock);return Status;
}

static bool MdoAgentsRefreshContext(MdoAgentOwner* Owner,MdoAgentInstance* Instance,
    xwork_agent_extension_context* Native,bool Force,xwork_error* Error)
{
    bool Refresh=Force || !Instance->Plan;
    const xvalue* Context=MdoAgentConfigGet(Instance->Configuration,"context");
    for(size_t I=0;!Refresh && I<xrtValueCount(Context);I++){
        const xvalue* Spec=xrtValueObjectAt(Context,I,NULL);
        cstr Policy=MdoAgentConfigText(Spec,"refresh");if(Policy&&!strcmp(Policy,"request"))Refresh=true;
    }
    if(!Refresh)return true;
    xvalue* Plan=NULL;
    if(Force){xrtValueRelease(Instance->SourcePlan);Instance->SourcePlan=NULL;}
    if(!MdoAgentsContextCollect(Owner,Instance,&Instance->Definition,Native,&Plan,Error))return false;
    xvalue* SourcePlan=MdoAgentConfigClone(Plan);
    if(!SourcePlan){xrtValueRelease(Plan);return false;}
    if(!MdoAgentsProgramEvent(Owner,Instance,Native,Plan,MDO_AGENT_CONTEXT,Error)){xrtValueRelease(SourcePlan);xrtValueRelease(Plan);return false;}
    char* System=MdoAgentsContextRender(Plan,"system",Error);
    char* User=System?MdoAgentsContextRender(Plan,"user",Error):NULL;
    xllm_error ModelError;
    bool Ok=System && User && xllmSessionSetSystemPrompt(Native->pSession,System,&ModelError);
    if(Ok){
        xrtValueRelease(Instance->SourcePlan);Instance->SourcePlan=SourcePlan;SourcePlan=NULL;
        xrtMutexLock(Owner->InstanceLock);xvalue* Old=Instance->Plan;Instance->Plan=Plan;Plan=NULL;xrtMutexUnlock(Owner->InstanceLock);xrtValueRelease(Old);
    } else if(Error && Error->eCode==XWORK_ERROR_NONE)MdoAgentsModelError(Error,&ModelError,"cannot apply the role context plan");
    xrtFree(System);xrtFree(User);xrtValueRelease(SourcePlan);xrtValueRelease(Plan);return Ok;
}
static bool MdoAgentsRequestCopy(const xllm_request* Source,xllm_request* Copy)
{
    *Copy=*Source;Copy->pMessages=NULL;Copy->iMessageCount=Copy->iMessageCap=0;
    Copy->pTools=NULL;Copy->iToolCount=Copy->iToolCap=0;
    Copy->pbMessageBorrowed=NULL;Copy->pbToolBorrowed=NULL;
    Copy->sModel=Copy->sReasoningEffort=Copy->sNamedTool=Copy->sStop=Copy->sExtraBodyJson=NULL;
    Copy->pStablePrefixOwner=NULL;Copy->uStablePrefixStamp=0;Copy->iStableMessages=0;
    if((Source->sModel&&!xllmRequestSetModel(Copy,Source->sModel)) ||
        (Source->sReasoningEffort&&!xllmRequestSetReasoningEffort(Copy,Source->sReasoningEffort)) ||
        (Source->sNamedTool&&!xllmRequestSetToolChoice(Copy,Source->eToolChoice,Source->sNamedTool)) ||
        (Source->sStop&&!xllmRequestSetStop(Copy,Source->sStop)) ||
        (Source->sExtraBodyJson&&!xllmRequestSetExtraBody(Copy,Source->sExtraBodyJson)))return false;
    for(size_t I=0;I<Source->iMessageCount;I++)if(!xllmRequestAddMessage(Copy,&Source->pMessages[I]))return false;
    for(size_t I=0;I<Source->iToolCount;I++){const xllm_tool* Tool=&Source->pTools[I];
        if(!xllmRequestAddTool(Copy,Tool->sName,Tool->sDescription,Tool->sParametersJson,Tool->bStrict))return false;}
    return true;
}
static bool MdoAgentsRequestContext(MdoAgentInstance* Instance,xllm_request* Request,xwork_error* Error)
{
    char* User=Instance->Plan?MdoAgentsContextRender(Instance->Plan,"user",Error):xrtStrDup("");
    if(!User)return false;
    if(!User[0]){xrtFree(User);return true;}
    xllm_request Copy;if(!MdoAgentsRequestCopy(Request,&Copy)){xllmRequestUnit(&Copy);xrtFree(User);return false;}
    for(size_t I=0;
    I<Copy.iMessageCount;
    I++)xllmMessageUnit(&Copy.pMessages[I]);
    Copy.iMessageCount=0;
    bool Added=false,Ok=true;
    for(size_t I=0;Ok && I<Request->iMessageCount;I++){
        if(!Added && Request->pMessages[I].eRole!=XLLM_ROLE_SYSTEM){Ok=xllmRequestAddTextMessage(&Copy,XLLM_ROLE_USER,User);Added=true;}
        if(Ok)Ok=xllmRequestAddMessage(&Copy,&Request->pMessages[I]);
    }
    if(Ok&&!Added)Ok=xllmRequestAddTextMessage(&Copy,XLLM_ROLE_USER,User);
    xrtFree(User);if(!Ok){xllmRequestUnit(&Copy);return false;}
    xllmRequestUnit(Request);*Request=Copy;return true;
}
static bool MdoAgentsRequestOptions(MdoAgentInstance* Instance,xllm_request* Request,xwork_error* Error)
{
    const xvalue* Options=MdoAgentConfigGet(Instance->Configuration,"model_options");
    const xvalue* Value=MdoAgentConfigGet(Options,"temperature");
    if(Value){double Number=0;int64 Integer=0;if(!xrtValueGetFloat(Value,&Number)){if(!xrtValueGetInt(Value,&Integer))goto invalid;Number=(double)Integer;}if(Number<0||Number>2)goto invalid;Request->bHasTemperature=true;Request->fTemperature=Number;}
    Value=MdoAgentConfigGet(Options,"top_p");
    if(Value){double Number=0;int64 Integer=0;if(!xrtValueGetFloat(Value,&Number)){if(!xrtValueGetInt(Value,&Integer))goto invalid;Number=(double)Integer;}if(Number<0||Number>1)goto invalid;Request->bHasTopP=true;Request->fTopP=Number;}
    Request->bParallelToolCalls=MdoAgentConfigBool(Options,"parallel_tool_calls",Request->bParallelToolCalls);
    Value=MdoAgentConfigGet(Options,"extra_body");
    if(Value){char* Json=xrtJsonStringify(Value,false,NULL);bool Ok=Json&&xllmRequestSetExtraBody(Request,Json);xrtFree(Json);if(!Ok)goto invalid;}
    cstr Choice=MdoAgentConfigText(Options,"tool_choice");
    if(Choice){if(!strcmp(Choice,"auto"))Request->eToolChoice=XLLM_TOOL_CHOICE_AUTO;else if(!strcmp(Choice,"none"))Request->eToolChoice=XLLM_TOOL_CHOICE_NONE;else if(!strcmp(Choice,"required"))Request->eToolChoice=XLLM_TOOL_CHOICE_REQUIRED;else if(!xllmRequestSetToolChoice(Request,XLLM_TOOL_CHOICE_NAMED,Choice))goto invalid;}
    return true;
invalid:MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"invalid model_options in role configuration");return false;
}
static xvalue* MdoAgentsRequestPreview(const xllm_request* Request)
{
    xvalue* Data=xrtValueObject(),*Messages=xrtValueArray(),*Tools=xrtValueArray();
    uint64 Tokens=0;size_t Budget=1024u*1024u;bool Truncated=false;
    bool Ok=Data && Messages && Tools && MdoAgentsPlanPut(Data,"model",Request->sModel) &&
        MdoAgentsPlanPut(Data,"reasoning_effort",Request->sReasoningEffort);
    for(size_t I=0;Ok && I<Request->iMessageCount;I++){
        const xllm_message* Message=&Request->pMessages[I];Tokens+=xllmEstimateMessageTokens(Message);
        xvalue* Item=xrtValueObject();cstr Text=Message->sContent?Message->sContent:"";size_t N=strlen(Text);
        if(N>Budget){N=Budget;while(N&&((unsigned char)Text[N]&0xc0)==0x80)--N;Truncated=true;}Budget-=N;
        Ok=Item && xrtValueObjectSetNew(Item,XRT_STR_LITERAL("role"),xrtValueInt(Message->eRole)) &&
            xrtValueObjectSetNew(Item,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Text,N))) &&
            xrtValueObjectSetNew(Item,XRT_STR_LITERAL("tool_calls"),xrtValueUInt(Message->iToolCallCount)) &&
            xrtValueObjectSetNew(Item,XRT_STR_LITERAL("parts"),xrtValueUInt(Message->iPartCount)) && xrtValueArrayAppend(Messages,Item);
        xrtValueRelease(Item);
    }
    for(size_t I=0;Ok && I<Request->iToolCount;I++){
        const xllm_tool* Tool=&Request->pTools[I];Tokens+=xllmEstimateTextTokens(Tool->sDescription)+xllmEstimateTextTokens(Tool->sParametersJson)+16u;
        Ok=xrtValueArrayAppendNew(Tools,xrtValueString(xrtStrView(Tool->sName)));
    }
    if(Ok)Ok=xrtValueObjectSet(Data,XRT_STR_LITERAL("messages"),Messages) && xrtValueObjectSet(Data,XRT_STR_LITERAL("tools"),Tools) &&
        xrtValueObjectSetNew(Data,XRT_STR_LITERAL("estimated_input_tokens"),xrtValueUInt(Tokens)) &&
        xrtValueObjectSetNew(Data,XRT_STR_LITERAL("max_output_tokens"),xrtValueUInt(Request->uMaxOutputTokens)) &&
        xrtValueObjectSetNew(Data,XRT_STR_LITERAL("preview_truncated"),xrtValueBool(Truncated));
    xrtValueRelease(Messages);xrtValueRelease(Tools);if(!Ok){xrtValueRelease(Data);Data=NULL;}return Data;
}
static xwork_result MdoAgentsExtension(void* Data,xwork_agent_extension_context* Native,xwork_error* Error)
{
    MdoAgentOwner* Owner=Data;
    /* Native SDK clients may publish their own runtime-only child definitions.
     * They retain xwork's policy and lifecycle without becoming module roles. */
    MdoModuleAgentInfo Known={0};Known.Size=sizeof(Known);
    if(!Native->sDefinitionId || !MdoModuleCatalogAgentFind(Owner->Modules,Native->sDefinitionId,&Known))return XWORK_RESULT_OK;
    MdoAgentInstance* Instance=MdoAgentsInstance(Owner,Native,Error);
    if(!Instance)return XWORK_RESULT_ERROR;
    if(Native->ePhase==XWORK_AGENT_EXTENSION_MODEL_DISPATCH){
        /* Includes compaction meta-requests, which must use the same role's
         * provider even when two profiles share a wire model name. */
        xrtMutexLock(Owner->InstanceLock);Instance->CurrentRequest=Native->pRequest;xrtMutexUnlock(Owner->InstanceLock);
        return XWORK_RESULT_OK;
    }
    if(Native->ePhase==XWORK_AGENT_EXTENSION_CHILD){
        if(!MdoAgentsDelegationContext(Owner,Instance,Native,Error))return XWORK_RESULT_ERROR;
        Native->pDelegationTools=Owner->DelegationTools;Native->pDelegationToolOwner=Owner->RootAgent;
        Native->bOverrideDelegationApproval=Owner->IndependentDelegation;
        Native->eDelegationApproval=Owner->ApprovalScope && Owner->DelegationApproval!=XWORK_APPROVAL_READ_ONLY?
            XWORK_APPROVAL_CALLBACK:Owner->DelegationApproval;
        if(Instance->Definition.PermissionProfile && !strcmp(Instance->Definition.PermissionProfile,"balanced") && Native->eDelegationApproval==XWORK_APPROVAL_AUTO)
            Native->eDelegationApproval=XWORK_APPROVAL_CALLBACK;
        return MdoAgentsApplyMcpSelection(Native->pAgent,Owner->Workspace,&Instance->Definition,Error)?XWORK_RESULT_OK:XWORK_RESULT_ERROR;
    }
    if(Native->ePhase==XWORK_AGENT_EXTENSION_CHILD_READY){
        MdoAgentResolvedModel Model,ParentModel=Owner->RootModel;
        xrtMutexLock(Owner->InstanceLock);
        for(MdoAgentInstance* I=Owner->Instances;I;I=I->Next)if(I->Agent==Native->pParent)ParentModel=I->Model;
        xrtMutexUnlock(Owner->InstanceLock);
        const char* Id=Instance->Definition.Model?Instance->Definition.Model:ParentModel.Info.Id;
        bool Same=!strcmp(Id,ParentModel.Info.Id);
        const char* Effort=Instance->Definition.ReasoningEffort?Instance->Definition.ReasoningEffort:Same?ParentModel.ReasoningEffort:NULL;
        uint32 Output=Instance->Definition.MaxOutputTokens?Instance->Definition.MaxOutputTokens:Same?ParentModel.MaxOutputTokens:0;
        if(!MdoAgentsResolveModel(Owner->Models,Id,Same?ParentModel.Protocol:0,Effort,Output,&Model,Error))return XWORK_RESULT_ERROR;
        Instance->Model=Model;
        xllm_model_profile Profile;xllm_session_config Config;xllm_error ModelError;
        if(!MdoModelCatalogProfile(Owner->Models,Model.Info.Id,Model.Protocol,&Profile,&ModelError) ||
            !MdoAgentsSessionConfig(&Profile,&Instance->Definition,Model.MaxOutputTokens,&Config,&ModelError) ||
            !xllmSessionApplyProfile(Native->pSession,&Config,&Profile,&ModelError)){
            MdoAgentsModelError(Error,&ModelError,"cannot apply the child model's own session profile");return XWORK_RESULT_ERROR;
        }
        size_t Published=0;
        if(!MdoAgentsPublishSubagents(Owner,Native->pAgent,&Instance->Definition,&Model,&Published,Error))return XWORK_RESULT_ERROR;
        size_t ToolCount=0;uint64 Generation=0;
        if(!MdoAgentsApplyToolPolicy(Native->pAgent,&Instance->Definition,Instance->Definition.AllowedEffects & Owner->DelegationEffects,&ToolCount,&Generation,Error))return XWORK_RESULT_ERROR;
        return XWORK_RESULT_OK;
    }
    if(Native->ePhase==XWORK_AGENT_EXTENSION_CONTEXT){
        if(!MdoAgentsHistoryConfigure(Instance,Native,false))return XWORK_RESULT_ERROR;
        return MdoAgentsRefreshContext(Owner,Instance,Native,false,Error)?XWORK_RESULT_OK:XWORK_RESULT_ERROR;
    }
    if(Native->ePhase==XWORK_AGENT_EXTENSION_RELEASE){
        (void)MdoAgentsProgramEvent(Owner,Instance,Native,Instance->Plan,MDO_AGENT_INSTANCE_RELEASE,Error);
        if(Instance->HistoryConfigured)xllmSessionSetHooks(Native->pSession,NULL);
        xrtMutexLock(Owner->InstanceLock);MdoAgentInstance** At=&Owner->Instances;
        while(*At && *At!=Instance)At=&(*At)->Next;
        if(*At)*At=Instance->Next;
        xrtMutexUnlock(Owner->InstanceLock);
        xrtValueRelease(Instance->Configuration);xrtValueRelease(Instance->Plan);xrtValueRelease(Instance->SourcePlan);xrtValueRelease(Instance->State);xrtValueRelease(Instance->RequestPreview);xrtFree(Instance->LegacyPrompt);xrtFree(Instance);
        return XWORK_RESULT_OK;
    }
    if(Native->ePhase==XWORK_AGENT_EXTENSION_RUN)return MdoAgentsLifecycleRun(Owner,Instance,Native,Error);
    if(Native->ePhase==XWORK_AGENT_EXTENSION_REQUEST){
        uint64 Limit=0;
        if(!MdoAgentConfigNumber(MdoAgentConfigGet(Instance->Configuration,"runtime"),"max_model_calls",0,100000,&Limit) || (Limit && Instance->RunModelCalls>=Limit)){
            MdoAgentsError(Error,XWORK_ERROR_LIMIT,"role model-call budget exhausted");return XWORK_RESULT_LIMIT;
        }
        xllm_request Copy;if(!MdoAgentsRequestCopy(Native->pRequest,&Copy)){xllmRequestUnit(&Copy);return XWORK_RESULT_ERROR;}
        xllmRequestUnit(Native->pRequest);*Native->pRequest=Copy;
        if(!MdoAgentsRequestContext(Instance,Native->pRequest,Error) || !MdoAgentsRequestOptions(Instance,Native->pRequest,Error) ||
            !MdoAgentsProgramEvent(Owner,Instance,Native,Instance->Plan,MDO_AGENT_BEFORE_REQUEST,Error))return XWORK_RESULT_ERROR;
        /* Tool schemas may be customized, but executing a name still requires
         * its real managed descriptor and live permission checks. */
        xwork_tool_catalog* Catalog=xworkAgentToolCatalogSnapshot(Native->pAgent);
        for(size_t I=0;I<Native->pRequest->iToolCount;I++){
            if(!MdoAgentsCatalogHasTool(Catalog,Native->pRequest->pTools[I].sName,XWORK_TOOL_EFFECT_ALL)){
                xworkToolCatalogRelease(Catalog);MdoAgentsError(Error,XWORK_ERROR_POLICY,"role request names an unavailable managed tool");return XWORK_RESULT_ERROR;
            }
        }
        xworkToolCatalogRelease(Catalog);
        xvalue* Preview=MdoAgentsRequestPreview(Native->pRequest);if(!Preview)return XWORK_RESULT_ERROR;
        xllm_session_budget Budget;xllmSessionBudgetInit(&Budget);uint64 Input=0;
        if(!xllmSessionGetBudget(Native->pSession,&Budget) ||
           !MdoAgentConfigNumber(Preview,"estimated_input_tokens",0,UINT64_MAX,&Input)){
            xrtValueRelease(Preview);MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"cannot inspect the role request budget");return XWORK_RESULT_ERROR;
        }
        if(Native->pRequest->uMaxOutputTokens>Instance->Model.MaxOutputTokens){
            xrtValueRelease(Preview);MdoAgentsError(Error,XWORK_ERROR_LIMIT,"role request exceeds its model output limit");return XWORK_RESULT_LIMIT;
        }
        if(Input>Budget.uInputBudgetTokens){
            Native->bCompactAndRetry=true;xrtValueRelease(Preview);return XWORK_RESULT_OK;
        }
        xrtMutexLock(Owner->InstanceLock);xvalue* Old=Instance->RequestPreview;Instance->RequestPreview=Preview;Instance->CurrentRequest=Native->pRequest;++Instance->ModelCalls;++Instance->RunModelCalls;xrtMutexUnlock(Owner->InstanceLock);xrtValueRelease(Old);
        return XWORK_RESULT_OK;
    }
    if(Native->ePhase==XWORK_AGENT_EXTENSION_RESPONSE)
        return MdoAgentsProgramEvent(Owner,Instance,Native,Instance->Plan,MDO_AGENT_AFTER_RESPONSE,Error)?XWORK_RESULT_OK:XWORK_RESULT_ERROR;
    if(Native->ePhase==XWORK_AGENT_EXTENSION_BEFORE_TOOL || Native->ePhase==XWORK_AGENT_EXTENSION_AFTER_TOOL){
        bool Before=Native->ePhase==XWORK_AGENT_EXTENSION_BEFORE_TOOL;
        if(!MdoAgentsProgramEvent(Owner,Instance,Native,Instance->Plan,Before?MDO_AGENT_BEFORE_TOOL:MDO_AGENT_AFTER_TOOL,Error))return XWORK_RESULT_ERROR;
        if(Native->pToolEvent && !strcmp(Native->pToolEvent->sToolName,"agent")){
            xwork_agent_extension_context Current=*Native;xwork_hook_event Event=*Native->pToolEvent;
            if(!Before && Native->pToolOutput){Event.sOutput=Native->pToolOutput->sContent;Event.bSuccess=Native->pToolOutput->bSuccess;}
            Current.pToolEvent=&Event;
            if(!MdoAgentsProgramEvent(Owner,Instance,&Current,Instance->Plan,Before?MDO_AGENT_BEFORE_DELEGATE:MDO_AGENT_AFTER_DELEGATE,Error))return XWORK_RESULT_ERROR;
        }
    }
    return XWORK_RESULT_OK;
}
