/* No plugin code runs during prompt collection. The same immutable catalog
 * pins native providers and declarative content for parent and child roles. */
static mdo_result MdoModulesRegistrarAddPrompt(void* U,const mdo_prompt_contribution_v1* D,char* Error,size_t Cap){
    MdoModuleRegistrarContext* R=U;MdoModuleGeneration* M=R?R->Generation:NULL;
    if(!M||R->Failed||!M->PluginEntry||!D||D->Size<sizeof(*D)||D->AbiVersion!=MDO_MODULE_ABI_VERSION||
       !(D->Targets&3u)||(D->Targets&~3u)||D->Activation>MDO_PROMPT_EXPLICIT||D->Priority<0||D->Priority>10000||
       D->AnyToolCount>MDO_PLUGIN_PROMPT_TOOLS||D->AllToolCount>MDO_PLUGIN_PROMPT_TOOLS||
       (D->AnyToolCount&&!D->AnyTools)||(D->AllToolCount&&!D->AllTools)||!D->Id||strlen(D->Id)>64||
       !D->Text||strlen(D->Text)>MDO_PLUGIN_PROMPT_BYTES||(D->Title&&strlen(D->Title)>256))
        return MdoModulesApplicationReject(R,Error,Cap,"Prompt registration requires a plugin and a valid descriptor");
    xvalue* P=xrtValueObject(),*Targets=xrtValueArray(),*When=xrtValueObject(),*Any=xrtValueArray(),*All=xrtValueArray();
    bool Ok=P&&Targets&&When&&Any&&All&&SdkPut(P,"id",D->Id)&&SdkPut(P,"text",D->Text)&&
        SdkPut(P,"title",D->Title?D->Title:D->Id)&&SdkPut(P,"activation",D->Activation==MDO_PROMPT_EXPLICIT?"explicit":"with-tools")&&SdkPutUInt(P,"priority",D->Priority);
    if(Ok&&(D->Targets&MDO_PROMPT_PRIMARY))Ok=xrtValueArrayAppendNew(Targets,xrtValueString(XRT_STR_LITERAL("primary")));
    if(Ok&&(D->Targets&MDO_PROMPT_SUBAGENT))Ok=xrtValueArrayAppendNew(Targets,xrtValueString(XRT_STR_LITERAL("subagent")));
    for(size_t I=0;Ok&&I<D->AnyToolCount;I++)Ok=D->AnyTools[I]&&strlen(D->AnyTools[I])<=128&&xrtValueArrayAppendNew(Any,xrtValueString(xrtStrView(D->AnyTools[I])));
    for(size_t I=0;Ok&&I<D->AllToolCount;I++)Ok=D->AllTools[I]&&strlen(D->AllTools[I])<=128&&xrtValueArrayAppendNew(All,xrtValueString(xrtStrView(D->AllTools[I])));
    Ok=Ok&&xrtValueObjectSet(When,XRT_STR_LITERAL("any_tools"),Any)&&xrtValueObjectSet(When,XRT_STR_LITERAL("all_tools"),All)&&
        xrtValueObjectSet(P,XRT_STR_LITERAL("when"),When)&&xrtValueObjectSet(P,XRT_STR_LITERAL("targets"),Targets)&&MdoPromptValid(P,Error,Cap);
    size_t Bytes=D->Text?strlen(D->Text):0;
    for(size_t I=0;Ok&&I<xrtValueCount(M->Prompts);I++){
        const xvalue* Prev=xrtValueArrayGet(M->Prompts,I);
        if(!strcmp(SdkField(Prev,"id"),D->Id))Ok=false;
        Bytes+=strlen(SdkField(Prev,"text"));
    }
    if(!M->Prompts)M->Prompts=xrtValueArray();
    Ok=Ok&&Bytes<=MDO_PLUGIN_PROMPTS_BYTES&&xrtValueCount(M->Prompts)<MDO_PLUGIN_PROMPT_COUNT&&xrtValueArrayAppend(M->Prompts,P);
    xrtValueRelease(P);xrtValueRelease(Targets);xrtValueRelease(When);xrtValueRelease(Any);xrtValueRelease(All);
    return Ok?MDO_RESULT_OK:MdoModulesApplicationReject(R,Error,Cap,"Invalid, duplicate or oversized prompt contribution");
}
static bool MdoModulesPromptAppend(MdoModuleCatalog* C,const xvalue* P,cstr Owner,cstr Alias,cstr Version,cstr Revision,cstr Module){
    xvalue* Row=xrtValueClone(P);char Hash[65];cstr Text=SdkField(P,"text");
    char* Key=xrtFormat("%s/%s",Owner,SdkField(P,"id"));
    bool Ok=Row&&Key&&Text&&MdoExtensionHash(Text,Hash)&&SdkPut(Row,"key",Key)&&SdkPut(Row,"plugin",Owner)&&SdkPut(Row,"alias",Alias)&&
        SdkPut(Row,"version",Version?Version:"")&&SdkPut(Row,"revision",Revision)&&SdkPut(Row,"hash",Hash)&&SdkPut(Row,"module",Module?Module:"")&&SdkPutUInt(Row,"bytes",strlen(Text));
    for(size_t I=0;Ok&&I<xrtValueCount(C->Prompts);I++){
        const xvalue* Old=xrtValueArrayGet(C->Prompts,I);if(!strcmp(Key,SdkField(Old,"key")))Ok=false;
    }
    Ok=Ok&&xrtValueArrayAppend(C->Prompts,Row);xrtFree(Key);xrtValueRelease(Row);return Ok;
}
static bool MdoModulesPromptSourcesHash(char Hash[65]){
    xvalue* Sources=MdoApplicationPluginPromptSources();char* Json=Sources?SdkJson(Sources):NULL;
    bool Ok=Json&&MdoExtensionHash(Json,Hash);xrtFree(Json);xrtValueRelease(Sources);return Ok;
}
static bool MdoModulesCollectPrompts(MdoModuleCatalog* C){
    C->Prompts=xrtValueArray();xvalue* Plugins=MdoApplicationPluginPromptSources();bool Ok=C->Prompts&&Plugins;
    char* Json=Ok?SdkJson(Plugins):NULL;Ok=Ok&&Json&&MdoExtensionHash(Json,C->PromptSourcesHash);xrtFree(Json);
    for(size_t I=0;Ok&&I<xrtValueCount(Plugins);I++){
        const xvalue* M=xrtValueArrayGet(Plugins,I),*Items=SdkGet(M,"prompts");
        cstr Owner=SdkField(M,"id"),Alias=SdkField(M,"alias");char Module[160];snprintf(Module,sizeof(Module),"plugin.%s",Alias);
        size_t Bytes=0,Count=0;
        for(size_t J=0;Ok&&J<xrtValueCount(Items);J++){
            const xvalue* P=xrtValueArrayGet(Items,J);Bytes+=strlen(SdkField(P,"text"));++Count;
            Ok=MdoModulesPromptAppend(C,P,Owner,Alias,SdkField(M,"version"),SdkField(M,"revision"),Module);
        }
        for(size_t J=0;Ok&&J<C->ModuleCount;J++){
            MdoModuleGeneration* G=C->Modules[J];cstr Ref=MdoExtensionText(G->Directory,"id",MDO_EXTENSION_REF_BYTES);if(!Ref||strcmp(Owner,Ref))continue;
            for(size_t K=0;Ok&&K<xrtValueCount(G->Prompts);K++){
                const xvalue* P=xrtValueArrayGet(G->Prompts,K);Bytes+=strlen(SdkField(P,"text"));++Count;
                Ok=MdoModulesPromptAppend(C,P,Owner,Alias,SdkField(M,"version"),G->DirectoryDigest,G->Id);
            }
        }
        Ok=Ok&&Bytes<=MDO_PLUGIN_PROMPTS_BYTES&&Count<=MDO_PLUGIN_PROMPT_COUNT;
    }
    xrtValueRelease(Plugins);return Ok;
}
const xvalue* MdoModuleCatalogPromptContributions(const MdoModuleCatalog* C){return C?C->Prompts:NULL;}
uint64 MdoModuleCatalogGeneration(const MdoModuleCatalog* C){return C?C->Generation:0;}
bool MdoModuleManagerRefreshPrompts(void){
    MdoModuleCatalog* C=MdoModuleCatalogSnapshot();if(!C)return true;
    char Hash[65];bool Ok=MdoModulesPromptSourcesHash(Hash),Changed=Ok&&strcmp(Hash,C->PromptSourcesHash);
    MdoModuleCatalogRelease(C);return Ok&&(!Changed||MdoModuleManagerReload());
}
