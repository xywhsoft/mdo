static xvalue* MdoAgentsPluginGroup(xvalue* Plan){
    xvalue* Sources=(xvalue*)MdoAgentConfigGet(Plan,"sources");
    for(size_t I=0;I<xrtValueCount(Sources);I++){
        xvalue* S=xrtValueArrayGet(Sources,I);cstr Id=MdoAgentConfigText(S,"id");if(Id&&!strcmp(Id,"plugins.instructions"))return S;
    }return NULL;
}
static int MdoAgentsPluginCompare(const void* A,const void* B){
    const xvalue *L=*(const xvalue**)A,*R=*(const xvalue**)B;uint64 X=100,Y=100;
    MdoAgentConfigNumber(L,"priority",100,10000,&X);MdoAgentConfigNumber(R,"priority",100,10000,&Y);
    return X!=Y?(X<Y?-1:1):strcmp(MdoAgentConfigText(L,"key"),MdoAgentConfigText(R,"key"));
}
static bool MdoAgentsPluginTool(MdoAgentOwner* Owner,xwork_tool_catalog* Catalog,const xllm_request* Request,cstr Plugin,cstr Name){
    if(Request&&(Request->eToolChoice==XLLM_TOOL_CHOICE_NONE||
       (Request->eToolChoice==XLLM_TOOL_CHOICE_NAMED&&Request->sNamedTool&&strcmp(Request->sNamedTool,Name))))return false;
    bool Own=false;
    for(size_t I=0;I<MdoModuleCatalogToolCount(Owner->Modules);I++){
        MdoModuleToolInfo T={0};T.Size=sizeof(T);
        if(MdoModuleCatalogToolAt(Owner->Modules,I,&T)&&!strcmp(T.Id,Name)&&T.ExtensionId&&!strcmp(T.ExtensionId,Plugin)){Own=true;break;}
    }
    if(!Own||!Catalog)return false;
    bool Available=false;
    for(size_t I=0;I<xworkToolCatalogCount(Catalog);I++){
        xwork_tool_info T={0};if(xworkToolCatalogToolAt(Catalog,I,&T)&&!strcmp(T.sName,Name)&&T.sSource&&!strcmp(T.sSource,"mdo.modules")){Available=true;break;}
    }
    if(!Available)return false;
    if(!Request)return true;
    for(size_t I=0;I<Request->iToolCount;I++)if(!strcmp(Request->pTools[I].sName,Name))return true;
    return false;
}
static bool MdoAgentsPluginCanonicalize(MdoAgentOwner* Owner,xvalue* Group){
    const xvalue* Registry=MdoModuleCatalogPromptContributions(Owner->Modules),*Controls=MdoAgentConfigGet(Group,"items");
    if(xrtValueCount(Controls)>32u*MDO_PLUGIN_PROMPT_COUNT)return false;
    xvalue* Items=xrtValueArray();bool Ok=Items!=NULL;
    for(size_t I=0;Ok&&I<xrtValueCount(Registry);I++){
        const xvalue* Original=xrtValueArrayGet(Registry,I),*Control=NULL;cstr Key=MdoAgentConfigText(Original,"key");
        for(size_t J=0;J<xrtValueCount(Controls);J++){
            const xvalue* C=xrtValueArrayGet(Controls,J);cstr K=MdoAgentConfigText(C,"key");if(K&&!strcmp(Key,K)){Control=C;break;}
        }
        xvalue* Row=xrtValueClone(Original);bool Enabled=Control&&MdoAgentConfigBool(Control,"enabled",true);
        uint64 Priority=100;if(Control&&MdoAgentConfigNumber(Control,"priority",100,10000,&Priority))Ok=Row&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("priority"),xrtValueUInt(Priority));
        cstr Text=MdoAgentConfigText(Control,"text"),Before=MdoAgentConfigText(Original,"text");
        if(Text&&strcmp(Text,Before)){
            char Hash[65];bool Valid=strlen(Text)<=MDO_PLUGIN_PROMPT_BYTES&&MdoExtensionHash(Text,Hash);
            if(!Valid)Enabled=false;
            else Ok=Row&&MdoAgentsPlanPut(Row,"text",Text)&&MdoAgentsPlanPut(Row,"effective_hash",Hash)&&MdoAgentsPlanPut(Row,"override","agent_fragment");
        }
        Ok=Ok&&Row&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&xrtValueArrayAppend(Items,Row);xrtValueRelease(Row);
    }
    Ok=Ok&&xrtValueObjectSet(Group,XRT_STR_LITERAL("items"),Items);xrtValueRelease(Items);return Ok;
}
/* Eligibility is always calculated for this actual role. In particular this
 * does not intersect a writer child's tools with its coordinator's own tools. */
static bool MdoAgentsPluginSelect(MdoAgentOwner* Owner,MdoAgentInstance* Instance,xwork_agent_extension_context* Native,
    const xllm_request* Request,xvalue* Group,xwork_error* Error){
    const xvalue* Spec=MdoAgentsSourceSpec(Instance->Configuration,"plugins.instructions");
    if(xrtValueCount(MdoAgentConfigGet(Group,"items"))>32u*MDO_PLUGIN_PROMPT_COUNT){MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"too many role plugin prompt controls");return false;}
    if(!MdoAgentsPluginCanonicalize(Owner,Group))return false;
    xvalue* Items=(xvalue*)MdoAgentConfigGet(Group,"items");size_t Count=xrtValueCount(Items);
    if(Count>32u*MDO_PLUGIN_PROMPT_COUNT){MdoAgentsError(Error,XWORK_ERROR_CONTEXT,"too many plugin prompt contributions");return false;}
    const xvalue* Sorted[32u*MDO_PLUGIN_PROMPT_COUNT];for(size_t I=0;I<Count;I++)Sorted[I]=xrtValueArrayGet(Items,I);
    if(Count>1)qsort(Sorted,Count,sizeof(*Sorted),MdoAgentsPluginCompare);
    uint64 Limit=MDO_ROLE_PLUGIN_PROMPTS_BYTES;MdoAgentConfigNumber(Spec,"max_bytes",MDO_ROLE_PLUGIN_PROMPTS_BYTES,MDO_ROLE_PLUGIN_PROMPTS_BYTES,&Limit);
    if(!Limit)Limit=MDO_ROLE_PLUGIN_PROMPTS_BYTES;
    bool Runtime=Native&&Native->pAgent,Subagent=Runtime?(Owner->RootAgent&&Native->pAgent!=Owner->RootAgent):((Instance->Definition.Flags&MDO_AGENT_MAIN)==0);
    xwork_tool_catalog* Catalog=Runtime?xworkAgentToolCatalogSnapshot(Native->pAgent):NULL;
    char* Text=xrtStrDup("");bool Ok=Text!=NULL;size_t Used=0,PluginCount=0,PluginBytes[32]={0};const char* Plugins[32]={0};
    for(size_t I=0;Ok&&I<Count;I++){
        xvalue* Row=(xvalue*)Sorted[I];cstr Key=MdoAgentConfigText(Row,"key"),Plugin=MdoAgentConfigText(Row,"plugin");
        cstr Why=NULL;bool Targets=true;const xvalue* T=MdoAgentConfigGet(Row,"targets");
        if(T){Targets=false;for(size_t J=0;J<xrtValueCount(T);J++){xstrview S;xrtValueGetString(xrtValueArrayGet(T,J),&S);if(xrtStrEqual(S,xrtStrView(Subagent?"subagent":"primary")))Targets=true;}}
        bool Include=MdoAgentsListMatch(MdoAgentConfigGet(Spec,"include"),Plugin,true)||MdoAgentsListMatch(MdoAgentConfigGet(Spec,"include"),Key,false);
        bool Exclude=MdoAgentsListMatch(MdoAgentConfigGet(Spec,"exclude"),Plugin,false)||MdoAgentsListMatch(MdoAgentConfigGet(Spec,"exclude"),Key,false);
        cstr Activation=MdoAgentConfigText(Row,"activation");
        bool Explicit=MdoAgentConfigGet(Spec,"include")&&Include;
        cstr Scope=MdoAgentConfigText(MdoAgentConfigGet(Group,"scope"),Plugin);
        if(!MdoAgentConfigBool(Group,"enabled",true))Why="source_disabled";
        else if(!MdoAgentConfigBool(Row,"enabled",true))Why="agent_disabled";
        else if(Scope&&Scope[0])Why=Scope;
        else if(Runtime&&!Targets)Why="role_target";
        else if(!Include||Exclude)Why="role_selection";
        else if(Activation&&!strcmp(Activation,"explicit")&&!Explicit)Why="explicit_selection_required";
        else if(!Runtime)Why="runtime_pending";
        const xvalue* When=MdoAgentConfigGet(Row,"when"),*Any=MdoAgentConfigGet(When,"any_tools"),*All=MdoAgentConfigGet(When,"all_tools");
        bool AnyFound=!xrtValueCount(Any),AllFound=true;
        for(size_t J=0;!Why&&J<xrtValueCount(Any);J++){xstrview S;xrtValueGetString(xrtValueArrayGet(Any,J),&S);if(MdoAgentsPluginTool(Owner,Catalog,Request,Plugin,S.Data))AnyFound=true;}
        for(size_t J=0;!Why&&J<xrtValueCount(All);J++){xstrview S;xrtValueGetString(xrtValueArrayGet(All,J),&S);if(!MdoAgentsPluginTool(Owner,Catalog,Request,Plugin,S.Data))AllFound=false;}
        if(!Why&&(!AnyFound||!AllFound))Why="tools_unavailable";
        size_t PluginIndex=0;for(;PluginIndex<PluginCount;PluginIndex++)if(!strcmp(Plugins[PluginIndex],Plugin))break;
        if(PluginIndex==PluginCount&&PluginCount<32)Plugins[PluginCount++]=Plugin;
        cstr Body=MdoAgentConfigText(Row,"text");char* Block=!Why?xrtFormat("Plugin instructions (%s):\n%s",Key,Body?Body:""):NULL;
        size_t Bytes=Block?strlen(Block):0;
        if(!Why&&!Block)Ok=false;
        if(!Why&&Ok&&(PluginIndex>=32||strlen(Body)>MDO_PLUGIN_PROMPTS_BYTES-PluginBytes[PluginIndex]))Why="plugin_budget";
        if(!Why&&Ok&&(Bytes+(Used?2u:0u)>Limit-Used))Why="budget";
        if(!Why&&Ok){if(Used)Ok=MdoAgentsAppendPrompt(&Text,"\n\n",2,Error);if(Ok)Ok=MdoAgentsAppendPrompt(&Text,Block,Bytes,Error);Used=strlen(Text);PluginBytes[PluginIndex]+=strlen(Body);}
        Ok=Ok&&MdoAgentsPlanPut(Row,"reason",Why?Why:"selected")&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("selected"),xrtValueBool(!Why));xrtFree(Block);
    }
    xworkToolCatalogRelease(Catalog);
    if(Ok)Ok=MdoAgentsPlanPut(Group,"text",Text)&&MdoAgentsPlanPut(Group,"generated_text",Text)&&
        MdoAgentsPlanPut(Group,"resolution",Runtime?"runtime":"candidate")&&xrtValueObjectSetNew(Group,XRT_STR_LITERAL("bytes"),xrtValueUInt(Used));
    xrtFree(Text);return Ok;
}
static bool MdoAgentsPluginSource(MdoAgentOwner* Owner,MdoAgentInstance* Instance,xwork_agent_extension_context* Native,xvalue* Plan,xwork_error* Error){
    const xvalue* Registry=MdoModuleCatalogPromptContributions(Owner->Modules);
    const xvalue* Spec=MdoAgentsSourceSpec(Instance->Configuration,"plugins.instructions");
    /* This source renders whole fragments; the generic byte truncator is not
     * used. Its content is immutable in the pinned registry, selection is not. */
    xvalue* Options=Spec&&xrtValueType(Spec)==XVALUE_OBJECT?xrtValueClone(Spec):xrtValueObject();
    if(!Options)return false;
    xrtValueObjectRemove(Options,XRT_STR_LITERAL("max_bytes"));
    bool Ok=MdoAgentsPlanSource(Plan,"plugins.instructions",xrtStrDup(""),Options,1300,MdoAgentsSourceEnabled(Spec,true),"plugins.instructions",Error);xrtValueRelease(Options);
    xvalue* Group=Ok?MdoAgentsPluginGroup(Plan):NULL; xvalue* Items=Registry?xrtValueClone(Registry):xrtValueArray();
    xvalue* Scope=xrtValueObject();
    for(size_t I=0;Ok&&Scope&&I<xrtValueCount(Registry);I++){
        cstr Plugin=MdoAgentConfigText(xrtValueArrayGet(Registry,I),"plugin");if(MdoAgentConfigGet(Scope,Plugin))continue;
        cstr Reason="";
        if(Owner->Workspace&&Owner->Workspace[0]){
            char Why[512]={0};
            if(!MdoExtensionsPreflight(Plugin,Why))Reason="plugin_unavailable";
            else if(!MdoExtensionsProjectAllows(Plugin,Owner->Workspace,Why))Reason="project_scope";
            xrtClearError();
        }
        Ok=MdoAgentsPlanPut(Scope,Plugin,Reason);
    }
    if(Ok)Ok=Items&&Scope&&xrtValueObjectSet(Group,XRT_STR_LITERAL("items"),Items)&&xrtValueObjectSet(Group,XRT_STR_LITERAL("scope"),Scope)&&MdoAgentsPlanPut(Group,"refresh","request")&&MdoAgentsPluginSelect(Owner,Instance,Native,NULL,Group,Error);
    xrtValueRelease(Scope);
    xrtValueRelease(Items);return Ok;
}
