/* A standalone one-role C module is also a local authoring resource. Listing
 * reads its already-published descriptor; it never compiles C on a GET. */
static char* ExtensionRoleCodeConfig(cstr Kind,cstr Id,cstr Source)
{
    if(!MdoPackageRoleKind(Kind))return NULL;
    char Full[96],Virtual[192],Hash[65];snprintf(Full,sizeof(Full),"%s.%s",!strcmp(Kind,"agents")?"agent":"subagent",Id);
    snprintf(Virtual,sizeof(Virtual),"/app/default-home/modules/%s/%s.c",Kind,Id);
    if(!MdoExtensionHash(Source,Hash))return NULL;
    MdoModuleCatalog* C=MdoModuleCatalogSnapshot();MdoModuleAgentInfo A={0};A.Size=sizeof(A);bool Found=MdoModuleCatalogAgentFind(C,Full,&A),Owned=false;
    for(size_t I=0;Found&&I<MdoModuleCatalogModuleCount(C);I++){
        MdoModuleInfo M={0};M.Size=sizeof(M);
        if(MdoModuleCatalogModuleAt(C,I,&M)&&!strcmp(M.Id,A.ModuleId)&&M.SourceRevision&&!strcmp(M.SourcePath,Virtual)&&!strcmp(M.SourceRevision,Hash))Owned=M.AgentCount==1&&M.ToolCount==0;
    }
    bool Enabled=true;MdoExtensionEnabled(Kind,Id,&Enabled);
    /* A disabled standalone C role remains manageable, but cannot be shared
     * until re-enabled and matched to an actual published descriptor. */
    xvalue* D=Owned||!Enabled?xrtValueObject():NULL;
    bool Ok=D&&MdoAgentConfigSetText(D,"schema","mdo.agent.v1")&&MdoAgentConfigSetText(D,"name",Owned?A.Name:Id)&&
        MdoAgentConfigSetText(D,"description",Owned?A.Description:"Standalone C role")&&MdoAgentConfigSetText(D,"mode",Owned&&(A.Flags&MDO_AGENT_MAIN)&&(A.Flags&MDO_AGENT_SUBAGENT)?"both":!strcmp(Kind,"agents")?"primary":"subagent")&&
        xrtValueObjectSetNew(D,XRT_STR_LITERAL("code"),xrtValueBool(true));
    if(Ok&&Owned){
        if(A.Model&&A.Model[0])Ok=MdoAgentConfigSetText(D,"model",A.Model);
        if(Ok&&A.ReasoningEffort&&A.ReasoningEffort[0])Ok=MdoAgentConfigSetText(D,"reasoning_effort",A.ReasoningEffort);
        const char* Keys[]={"read_only","allow_background","allow_delegation","require_verification_after_write"};
        const mdo_agent_flags Bits[]={MDO_AGENT_READ_ONLY,MDO_AGENT_ALLOW_BACKGROUND,MDO_AGENT_ALLOW_DELEGATION,MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE};
        for(size_t J=0;Ok&&J<4;J++)Ok=xrtValueObjectSetNew(D,xrtStrView(Keys[J]),xrtValueBool((A.Flags&Bits[J])!=0));
        if(Ok&&A.PermissionProfile&&A.PermissionProfile[0])Ok=MdoAgentConfigSetText(D,"permission_profile",A.PermissionProfile);
        xvalue* Limits=xrtValueObject();
        const char* Names[]={"context_tokens","input_tokens","output_tokens","max_turns","timeout_ms","max_final_bytes","max_depth"};
        const uint64 Values[]={A.ContextWindowTokens,A.MaxInputTokens,A.MaxOutputTokens,A.MaxTurns,A.TimeoutMilliseconds,A.MaxFinalBytes,A.MaxDepth};
        for(size_t J=0;Ok&&J<7;J++)Ok=Limits&&xrtValueObjectSetNew(Limits,xrtStrView(Names[J]),xrtValueUInt(Values[J]));
        if(Ok)Ok=xrtValueObjectSet(D,XRT_STR_LITERAL("runtime"),Limits);xrtValueRelease(Limits);
    }
    char* Text=Ok?xrtJsonStringify(D,true,NULL):NULL;xrtValueRelease(D);MdoModuleCatalogRelease(C);return Text;
}
static bool ExtensionRoleCodeList(cstr Kind,xvalue* Items,const xvalue* Records,bool Known,size_t* Count);
