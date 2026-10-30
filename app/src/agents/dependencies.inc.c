static bool MdoAgentsBindMcp(MdoAgentOwner* Owner,const MdoModuleAgentInfo* Definition,xvalue* Config,xwork_error* Error)
{
    const xvalue* Input=MdoAgentConfigGet(Config,"mcp_servers");if(!Input)return true;
    xvalue* Out=xrtValueArray();bool Ok=Out&&xrtValueType(Input)==XVALUE_ARRAY&&xrtValueCount(Input)<=128;char Why[512]={0};
    for(size_t I=0;Ok&&I<xrtValueCount(Input);I++){
        xstrview Name={0};Ok=xrtValueGetString(xrtValueArrayGet(Input,I),&Name);if(!Ok)break;
        if(MdoExtensionRefValid(Name.Data)){
            char* Preferred=Definition->ExtensionId?MdoExtensionsBinding(Definition->ExtensionId,Name.Data,Owner->ProductProject,Why):NULL;
            /* Inherited selections retain the declaring provider's account.
             * Different parent choices require an explicit role binding. */
            if(!Preferred&&!Why[0]&&Definition->ExtensionId){
                xvalue* Required=MdoExtensionsDependencies(Definition->ExtensionId,false,Why);if(!Required&&!Why[0])snprintf(Why,sizeof(Why),"Cannot resolve inherited MCP dependency bindings");
                for(size_t P=0;!Why[0]&&P<xrtValueCount(Required);P++){
                    const xvalue* Parent=xrtValueArrayGet(Required,P),*Deps=xrtValueObjectGet(Parent,XRT_STR_LITERAL("dependencies"));bool Uses=false;
                    for(size_t D=0;D<xrtValueCount(Deps);D++){const xvalue* Spec=xrtValueArrayGet(Deps,D);cstr Ref=MdoExtensionText(Spec,"id",MDO_EXTENSION_REF_BYTES);if(Ref&&!strcmp(Ref,Name.Data)&&!MdoAgentConfigBool(Spec,"optional",false))Uses=true;}
                    if(!Uses)continue;
                    char* Choice=MdoExtensionsBinding(MdoExtensionText(Parent,"id",MDO_EXTENSION_REF_BYTES),Name.Data,Owner->ProductProject,Why);
                    if(Choice&&Preferred&&strcmp(Choice,Preferred))snprintf(Why,sizeof(Why),"Inherited MCP dependencies select different accounts; bind an explicit connection for this role");
                    if(!Preferred){Preferred=Choice;Choice=NULL;}xrtFree(Choice);
                }xrtValueRelease(Required);
            }
            char* Connection=Why[0]?NULL:MdoMcpDependencyConnection(Name.Data,Owner->Workspace,Preferred,true,Why);
            Ok=Connection&&xrtValueArrayAppendNew(Out,xrtValueString(xrtStrView(Connection)));xrtFree(Preferred);xrtFree(Connection);
        }else Ok=xrtValueArrayAppend(Out,xrtValueArrayGet(Input,I));
    }
    if(Ok)Ok=xrtValueObjectSet(Config,XRT_STR_LITERAL("mcp_servers"),Out);
    xrtValueRelease(Out);
    if(!Ok)MdoAgentsError(Error,XWORK_ERROR_CONTEXT,Why[0]?Why:"Cannot resolve the role's MCP connection bindings");
    return Ok;
}
static bool MdoAgentsCheckDependencies(MdoAgentOwner* Owner,const MdoModuleAgentInfo* Definition,xwork_agent* Agent,xwork_error* Error)
{
    char Why[512]={0};bool Ok=MdoExtensionsRuntimeCheck(Definition->ExtensionId,Owner->Workspace,Owner->ProductProject,Agent,true,Why);
    if(!Ok){MdoAgentsError(Error,Agent?XWORK_ERROR_POLICY:XWORK_ERROR_CONTEXT,Why);return false;}
    xvalue* Config=Definition->ConfigurationJson?xrtJsonParse(xrtStrView(Definition->ConfigurationJson)):NULL;const xvalue* Refs=MdoAgentConfigGet(Config,"resolved_references");
    if(!MdoAgentsSourceEnabled(MdoAgentsSourceSpec(Config,"knowledge"),true)&&Definition->ExtensionId){
        xvalue* Required=MdoExtensionsDependencies(Definition->ExtensionId,false,Why);Ok=Required!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Required);I++){const xvalue* Row=xrtValueArrayGet(Required,I);cstr Kind=MdoExtensionText(Row,"kind",32);if(Kind&&!strcmp(Kind,"knowledge")){Ok=false;snprintf(Why,sizeof(Why),"%.160s requires knowledge references, but this role disabled the knowledge context source",Definition->ExtensionId);}}xrtValueRelease(Required);
    }
    if(!Agent||!Ok){xrtValueRelease(Config);if(!Ok)MdoAgentsError(Error,XWORK_ERROR_POLICY,Why);return Ok;}
    xwork_tool_catalog* Tools=xworkAgentToolCatalogSnapshot(Agent);
    for(size_t I=0;Ok&&I<xrtValueCount(Refs);I++){
        const xvalue* R=xrtValueArrayGet(Refs,I);cstr Field=MdoAgentConfigText(R,"field"),Name=MdoAgentConfigText(R,"resolved"),Ref=MdoAgentConfigText(R,"ref");if(!Field||strcmp(Field,"tools"))continue;
        if(!Name||!Ref){Ok=false;snprintf(Why,sizeof(Why),"Invalid resolved role reference");break;}
        bool Found=false;for(size_t J=0;J<xworkToolCatalogCount(Tools);J++){xwork_tool_info Info={0};if(xworkToolCatalogToolAt(Tools,J,&Info)&&!strcmp(Info.sName,Name)){Found=true;break;}}
        bool Owned=false;size_t Exports=0;
        for(size_t J=0;J<MdoModuleCatalogToolCount(Owner->Modules);J++){MdoModuleToolInfo Module={0};Module.Size=sizeof(Module);if(MdoModuleCatalogToolAt(Owner->Modules,J,&Module)&&Module.ExtensionId&&!strcmp(Module.ExtensionId,Ref)){Exports++;if(!strcmp(Module.Id,Name))Owned=true;}}
        if(!MdoAgentConfigBool(R,"explicit_export",false)&&Exports!=1){Ok=false;snprintf(Why,sizeof(Why),"%.160s exports multiple tools or none; select an explicit export",Ref);break;}
        Found=Found&&Owned;
        if(!Found){snprintf(Why,sizeof(Why),"%.160s#%.96s is unavailable or excluded by the role/parent tool policy",Ref?Ref:"",Name?Name:"");Ok=false;}
    }
    xworkToolCatalogRelease(Tools);xrtValueRelease(Config);if(!Ok)MdoAgentsError(Error,XWORK_ERROR_POLICY,Why);return Ok;
}
