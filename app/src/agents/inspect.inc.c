xvalue* MdoAgentDefinitionInspect(cstr Id,cstr Text,bool Main,cstr Workspace,
    cstr ProjectId,xwork_error* Error)
{
    MdoAgentOwner Owner={0};MdoAgentInstance Instance={0};MdoAgentFile File={0};
    xwork_agent_extension_context Native={0};xvalue* Plan=NULL;
    xworkErrorInit(Error);
    Owner.Modules=MdoModuleCatalogSnapshot();Owner.Workspace=xrtPathAbs(Workspace?Workspace:".");
    snprintf(Owner.ProductProject,sizeof(Owner.ProductProject),"%s",ProjectId?ProjectId:"");
    Owner.Skills=MdoSkillCatalogForWorkspace(Owner.Workspace?Owner.Workspace:".");
    Instance.Definition.Size=sizeof(Instance.Definition);
    bool Ok=Owner.Modules && Owner.Workspace && Owner.Skills;
    if(Ok && Text){
        char Message[1024]={0};Ok=MdoAgentFileParse(Id,Text,Main,&File,Message,sizeof(Message));
        if(!Ok)MdoAgentsError(Error,XWORK_ERROR_CONTEXT,Message);
        else {
            Instance.Definition.Id=File.Agent.Id;Instance.Definition.SystemPrompt=File.Agent.SystemPrompt;
            Instance.Definition.Tools=File.Agent.Tools;Instance.Definition.ToolCount=File.Agent.ToolCount;
            Instance.Definition.Skills=File.Agent.Skills;Instance.Definition.SkillCount=File.Agent.SkillCount;
            Instance.Definition.Flags=File.Agent.Flags;Instance.Definition.ConfigurationJson=File.ConfigurationJson;
            Instance.Definition.PermissionProfile=File.Agent.PermissionProfile;
            Instance.Definition.AllowedEffects=File.Agent.AllowedEffects;
        }
    } else if(Ok)Ok=MdoModuleCatalogAgentFind(Owner.Modules,Id,&Instance.Definition);
    if(Ok){
        Instance.Configuration=Instance.Definition.ConfigurationJson?xrtJsonParse(xrtStrView(Instance.Definition.ConfigurationJson)):xrtValueObject();
        Ok=Instance.Configuration && MdoAgentsContextCollect(&Owner,&Instance,&Instance.Definition,&Native,&Plan,Error);
    }
    char* System=Ok?MdoAgentsContextRender(Plan,"system",Error):NULL;
    char* User=System?MdoAgentsContextRender(Plan,"user",Error):NULL;
    if(Ok)Ok=System && User && MdoAgentsPlanPut(Plan,"inspection","definition") &&
        MdoAgentsPlanPut(Plan,"system_prompt",System) && MdoAgentsPlanPut(Plan,"user_context",User) &&
        xrtValueObjectSet(Plan,XRT_STR_LITERAL("configuration"),Instance.Configuration) &&
        xrtValueObjectSetNew(Plan,XRT_STR_LITERAL("code_callbacks_executed"),xrtValueBool(false)) &&
        xrtValueObjectSetNew(Plan,XRT_STR_LITERAL("has_program"),xrtValueBool(Instance.Definition.HasProgram||File.UseCode)) &&
        xrtValueObjectSetNew(Plan,XRT_STR_LITERAL("module_generation"),xrtValueUInt(MdoModuleManagerGeneration()));
    xrtFree(System);xrtFree(User);xrtValueRelease(Instance.Configuration);MdoAgentFileUnit(&File);
    MdoModuleCatalogRelease(Owner.Modules);MdoSkillCatalogRelease(Owner.Skills);xrtFree(Owner.Workspace);
    if(!Ok){xrtValueRelease(Plan);if(Error && Error->eCode==XWORK_ERROR_NONE)MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"role definition is unavailable");return NULL;}
    return Plan;
}
xvalue* MdoAgentSessionInspect(MdoAgentSession* Session,xwork_error* Error)
{
    if(!Session || !Session->Owner){MdoAgentsError(Error,XWORK_ERROR_INVALID_ARGUMENT,"an owned Agent session is required");return NULL;}
    MdoAgentOwner* Owner=Session->Owner;xvalue* Data=xrtValueObject();xvalue* Children=xrtValueArray();
    bool Ok=Data && Children;xrtMutexLock(Owner->InstanceLock);
    for(MdoAgentInstance* I=Owner->Instances;Ok && I;I=I->Next){
        xvalue* Item=xrtValueObject();
        Ok=Item && MdoAgentsPlanPut(Item,"agent_id",I->Definition.Id) &&
            xrtValueObjectSetNew(Item,XRT_STR_LITERAL("model_calls"),xrtValueUInt(I->ModelCalls));
        if(Ok && I->Agent==Session->Agent){
            if(I->Plan)Ok=xrtValueObjectSetNew(Data,XRT_STR_LITERAL("context"),MdoAgentConfigClone(I->Plan));
            if(Ok && I->RequestPreview)Ok=xrtValueObjectSetNew(Data,XRT_STR_LITERAL("request"),MdoAgentConfigClone(I->RequestPreview));
        }
        if(Ok)Ok=xrtValueArrayAppend(Children,Item);
        xrtValueRelease(Item);
    }
    xrtMutexUnlock(Owner->InstanceLock);
    if(Ok)Ok=MdoAgentsPlanPut(Data,"inspection","runtime") && MdoAgentsPlanPut(Data,"agent_id",Session->AgentId) &&
        xrtValueObjectSet(Data,XRT_STR_LITERAL("instances"),Children);
    xrtValueRelease(Children);if(!Ok){xrtValueRelease(Data);Data=NULL;}return Data;
}
