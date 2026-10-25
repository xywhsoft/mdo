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
        if(Ok)Ok=xrtValueObjectSetNew(D,XRT_STR_LITERAL("read_only"),xrtValueBool((A.Flags&MDO_AGENT_READ_ONLY)!=0));
    }
    char* Text=Ok?xrtJsonStringify(D,true,NULL):NULL;xrtValueRelease(D);MdoModuleCatalogRelease(C);return Text;
}
static bool ExtensionRoleCodeList(cstr Kind,xvalue* Items,const xvalue* Records,bool Known,size_t* Count);
