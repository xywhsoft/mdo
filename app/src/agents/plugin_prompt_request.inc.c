typedef struct MdoAgentPluginMessage {char* Text;xllm_role Role;bool Added;} MdoAgentPluginMessage;

/* Only the exact message we own may be replaced. An advanced role that rewrites
 * or removes it owns that request; never reconstruct it by parsing prose. */
static bool MdoAgentsPluginMessageWrite(xllm_request* Request,size_t Replace,cstr Text,xllm_role Role){
    xllm_request Copy;if(!MdoAgentsRequestCopy(Request,&Copy)){xllmRequestUnit(&Copy);return false;}
    for(size_t I=0;I<Copy.iMessageCount;I++)xllmMessageUnit(&Copy.pMessages[I]);
    Copy.iMessageCount=0;
    bool Added=false,Ok=true;
    for(size_t I=0;Ok&&I<Request->iMessageCount;I++){
        bool Insert=Replace!=SIZE_MAX?I==Replace:(!Added&&Request->pMessages[I].eRole!=XLLM_ROLE_SYSTEM);
        if(Insert){if(Text[0])Ok=xllmRequestAddTextMessage(&Copy,Role,Text);Added=true;}
        if(Ok&&I!=Replace)Ok=xllmRequestAddMessage(&Copy,&Request->pMessages[I]);
    }
    if(Ok&&!Added&&Text[0])Ok=xllmRequestAddTextMessage(&Copy,Role,Text);
    if(!Ok){xllmRequestUnit(&Copy);return false;}xllmRequestUnit(Request);*Request=Copy;return true;
}
static bool MdoAgentsPluginRequest(MdoAgentOwner* Owner,MdoAgentInstance* Instance,xwork_agent_extension_context* Native,
    MdoAgentPluginMessage* Binding,bool Final,xwork_error* Error){
    if(!Instance->Plan||!xrtValueCount(MdoModuleCatalogPromptContributions(Owner->Modules)))return true;
    xvalue* Plan=xrtValueClone(Instance->Plan);if(!Plan){MdoAgentsError(Error,XWORK_ERROR_OUT_OF_MEMORY,"cannot copy plugin contribution plan");return false;}
    xvalue* Group=MdoAgentsPluginGroup(Plan);
    if(!Group){xrtValueRelease(Plan);return true;}
    cstr Before=MdoAgentConfigText(Group,"text"),Generated=MdoAgentConfigText(Group,"generated_text");
    cstr PreviousOverride=MdoAgentConfigText(Group,"override");
    bool Override=Before&&Generated&&(strcmp(Before,Generated)||(PreviousOverride&&!strncmp(PreviousOverride,"agent_context",13)));char* Custom=Override?xrtStrDup(Before):NULL;
    bool Ok=Plan&&(!Override||Custom)&&MdoAgentsPluginSelect(Owner,Instance,Native,Native->pRequest,Group,Error);
    if(Ok&&Override){
        uint64 Limit=MDO_ROLE_PLUGIN_PROMPTS_BYTES;MdoAgentConfigNumber(MdoAgentsSourceSpec(Instance->Configuration,"plugins.instructions"),"max_bytes",Limit,Limit,&Limit);if(!Limit)Limit=MDO_ROLE_PLUGIN_PROMPTS_BYTES;
        bool Fits=strlen(Custom)<=Limit;
        Ok=MdoAgentsPlanPut(Group,"text",Fits?Custom:"")&&MdoAgentsPlanPut(Group,"generated_text",Fits?Custom:"")&&MdoAgentsPlanPut(Group,"override",Fits?"agent_context":"agent_context_budget");
    }
    xrtFree(Custom);
    cstr Text=MdoAgentConfigText(Group,"text"),Role=MdoAgentConfigText(Group,"role");
    if(Ok&&(!Text||!Role||(strcmp(Role,"system")&&strcmp(Role,"user"))))Ok=false;
    if(Ok&&!MdoAgentConfigBool(Group,"enabled",true))Text="";
    size_t Replace=SIZE_MAX,Matches=0;
    if(Ok&&Final&&Binding->Added){
        for(size_t I=0;I<Native->pRequest->iMessageCount;I++){
            const xllm_message* M=&Native->pRequest->pMessages[I];
            if(M->eRole==Binding->Role&&!M->iPartCount&&!M->iToolCallCount&&M->sContent&&!strcmp(M->sContent,Binding->Text)){Replace=I;++Matches;}
        }
        if(Matches!=1){Ok=MdoAgentsPlanPut(Group,"delivery","agent_request_override");Text=NULL;}
    }
    if(Ok&&Text){
        xllm_role MessageRole=!strcmp(Role,"user")?XLLM_ROLE_USER:XLLM_ROLE_SYSTEM;
        Ok=MdoAgentsPlanPut(Group,"delivery",Text[0]?"request_message":"omitted");
        char* Remember=Ok&&!Final&&Text[0]?xrtStrDup(Text):NULL;
        if(!Final&&Text[0]&&!Remember)Ok=false;
        bool Same=Final&&Binding->Added&&!strcmp(Text,Binding->Text)&&MessageRole==Binding->Role;
        if(Ok&&!Same&&((Final&&Binding->Added)||Text[0]))Ok=MdoAgentsPluginMessageWrite(Native->pRequest,Replace,Text,MessageRole);
        if(Ok&&Remember){Binding->Text=Remember;Remember=NULL;Binding->Role=MessageRole;Binding->Added=true;}
        xrtFree(Remember);
    }
    if(Ok){xrtMutexLock(Owner->InstanceLock);xvalue* Old=Instance->Plan;Instance->Plan=Plan;Plan=NULL;xrtMutexUnlock(Owner->InstanceLock);xrtValueRelease(Old);}
    xrtValueRelease(Plan);
    if(!Ok&&Error&&Error->eCode==XWORK_ERROR_NONE)MdoAgentsError(Error,XWORK_ERROR_OUT_OF_MEMORY,"cannot prepare plugin prompt contributions");
    return Ok;
}
