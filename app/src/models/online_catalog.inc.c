/* Public service models are transient catalog entries, never configuration
 * files. Personal provider IDs remain separate and the default model keeps its
 * historical identity, so existing tasks retain their selected wire protocol. */
static char* MdoModelsOnlineJoin(const char* Prefix,const char* Suffix)
{
    size_t A=strlen(Prefix),B=strlen(Suffix);char* Text=xrtMalloc(A+B+1);
    if(Text){memcpy(Text,Prefix,A);memcpy(Text+A,Suffix,B+1);}return Text;
}
static bool MdoModelsMergeOnline(MdoModelCatalog* Catalog,const xvalue* Value)
{
    if(!Value)return true;
    const xvalue* Items=xrtValueObjectGet(Value,XRT_STR_LITERAL("models"));
    if(!g_MdoModelsOnline.Origin || xrtValueType(Items)!=XVALUE_ARRAY || xrtValueCount(Items)>64 || MdoModelsFindProvider(Catalog,"mdo-online"))return false;
    MdoProviderEntry* Providers=xrtRealloc(Catalog->Providers,(Catalog->ProviderCount+1)*sizeof(*Providers));
    if(!Providers)return false;
    Catalog->Providers=Providers;
    MdoProviderEntry* Provider=&Providers[Catalog->ProviderCount++];memset(Provider,0,sizeof(*Provider));
    Provider->Id=xrtStrDup("mdo-online");Provider->Name=xrtStrDup("mdo online");Provider->Builtin=true;Provider->VerifyPeer=true;
    /* A non-secret marker for the read-only provider card. Online execution
     * obtains its actual bearer exclusively from the account lease. */
    Provider->CredentialReference=xrtStrDup("account:session");
    Provider->TimeoutMilliseconds=600000;Provider->Protocols=7;
    const char* Paths[]={"/api/v1/ai/chat/completions","/api/v1/ai/responses","/api/v1/ai/messages"};
    for(size_t p=0;p<3;p++){Provider->Endpoints[p]=MdoModelsOnlineJoin(g_MdoModelsOnline.Origin,Paths[p]);if(!Provider->Endpoints[p])return false;}
    if(!Provider->Id || !Provider->Name || !Provider->CredentialReference)return false;
    MdoModelEntry* Models=xrtRealloc(Catalog->Models,(Catalog->ModelCount+xrtValueCount(Items))*sizeof(*Models));
    if(!Models)return false;
    Catalog->Models=Models;
    for(size_t i=0;i<xrtValueCount(Items);i++){
        const xvalue* Item=xrtValueArrayGet(Items,i);bool Available=false,Member=false,Free=false,Tools=false,Vision=false;
        if(!MdoModelsBool(Item,"available",&Available))return false;
        if(!Available)continue;
        xstrview Id,Title,Reasoning,Field,Default;uint64 Context,Output;
        if(!MdoModelsString(xrtValueObjectGet(Item,XRT_STR_LITERAL("id")),&Id) || !Id.Size || Id.Size>112 ||
            !MdoModelsString(xrtValueObjectGet(Item,XRT_STR_LITERAL("title")),&Title) || Title.Size>128 ||
            !MdoModelsString(xrtValueObjectGet(Item,XRT_STR_LITERAL("reasoning_efforts")),&Reasoning) || Reasoning.Size>128 ||
            !MdoModelsString(xrtValueObjectGet(Item,XRT_STR_LITERAL("output_limit_field")),&Field) ||
            !MdoModelsString(xrtValueObjectGet(Item,XRT_STR_LITERAL("default_protocol")),&Default) ||
            !MdoModelsUnsigned(xrtValueObjectGet(Item,XRT_STR_LITERAL("context_window")),&Context) || Context<64 || Context>2000000 ||
            !MdoModelsUnsigned(xrtValueObjectGet(Item,XRT_STR_LITERAL("max_output")),&Output) || !Output || Output>Context ||
            !MdoModelsBool(Item,"member_only",&Member) || !MdoModelsBool(Item,"free",&Free) ||
            !MdoModelsBool(Item,"tool_calling",&Tools) || !MdoModelsBool(Item,"vision",&Vision))return false;
        for(size_t c=0;c<Id.Size;c++)if(!((Id.Data[c]>='a'&&Id.Data[c]<='z') || (Id.Data[c]>='A'&&Id.Data[c]<='Z') || (Id.Data[c]>='0'&&Id.Data[c]<='9') || Id.Data[c]=='-' || Id.Data[c]=='_' || Id.Data[c]=='.'))return false;
        bool Builtin=MdoModelsViewEqual(Id,MDO_BUILTIN_MODEL_ID);
        char Local[140];snprintf(Local,sizeof(Local),"mdo-online.%.*s",(int)Id.Size,Id.Data);
        char* LocalId=xrtStrDup(Builtin?MDO_BUILTIN_MODEL_ID:Local);if(!LocalId)return false;
        MdoModelEntry* Model=MdoModelsFindModel(Catalog,LocalId);
        if(Model && !Builtin){xrtFree(LocalId);return false;}
        if(Model)MdoModelsModelUnit(Model);else{Model=&Models[Catalog->ModelCount++];memset(Model,0,sizeof(*Model));}
        Model->Id=LocalId;Model->Name=xrtStrDupN(Title.Data,Title.Size);Model->ProviderId=xrtStrDup("mdo-online");Model->WireModel=xrtStrDupN(Id.Data,Id.Size);
        Model->Builtin=true;Model->Free=Free&&!Member;
        Model->WindowMode=XLLM_WINDOW_SHARED_CONTEXT;Model->ContextWindowTokens=Context;Model->MaxInputTokens=Context;
        Model->MaxOutputTokens=(uint32)Output;Model->OutputReserveTokens=(uint32)Output;Model->SummaryTokens=Output<8192?(uint32)Output:8192;
        Model->Capabilities=XLLM_CAP_TEXT_IN|XLLM_CAP_TEXT_OUT|XLLM_CAP_STREAM|XLLM_CAP_JSON_OUT;
        if(Tools)Model->Capabilities|=XLLM_CAP_TOOL_RESULT_IN|XLLM_CAP_TOOL_CALL_OUT|XLLM_CAP_PARALLEL_TOOL_CALL;
        if(Vision){Model->Capabilities|=XLLM_CAP_IMAGE_IN;Model->Attachments=MDO_MODEL_ATTACHMENT_IMAGE;}
        if(MdoModelsViewEqual(Field,"max_completion_tokens"))Model->Capabilities|=XLLM_CAP_MAX_COMPLETION_TOKENS;
        else if(!MdoModelsViewEqual(Field,"max_tokens"))return false;
        const xvalue* Protocols=xrtValueObjectGet(Item,XRT_STR_LITERAL("protocols"));if(xrtValueType(Protocols)!=XVALUE_ARRAY)return false;
        for(size_t p=0;p<xrtValueCount(Protocols);p++){xstrview Protocol;if(!MdoModelsString(xrtValueArrayGet(Protocols,p),&Protocol))return false;
            if(MdoModelsViewEqual(Protocol,"chat"))Model->Protocols|=MDO_MODEL_PROTOCOL_FLAG_CHAT_COMPLETIONS;
            else if(MdoModelsViewEqual(Protocol,"responses"))Model->Protocols|=MDO_MODEL_PROTOCOL_FLAG_RESPONSES;
            else if(MdoModelsViewEqual(Protocol,"anthropic"))Model->Protocols|=MDO_MODEL_PROTOCOL_FLAG_ANTHROPIC;else return false;}
        if(MdoModelsViewEqual(Default,"chat"))Model->DefaultProtocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
        else if(MdoModelsViewEqual(Default,"responses"))Model->DefaultProtocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
        else if(MdoModelsViewEqual(Default,"anthropic"))Model->DefaultProtocol=MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES;else return false;
        if(!(Model->Protocols&MdoModelsProtocolFlag(Model->DefaultProtocol)))return false;
        Model->ReasoningEfforts=xrtCalloc(MDO_MODELS_MAX_REASONING_EFFORTS,sizeof(char*));if(!Model->ReasoningEfforts)return false;
        size_t at=0;while(at<Reasoning.Size){size_t end=at;while(end<Reasoning.Size&&Reasoning.Data[end]!=',')end++;
            if(end==at || end-at>16 || Model->ReasoningEffortCount==MDO_MODELS_MAX_REASONING_EFFORTS)return false;
            char* Word=xrtStrDupN(Reasoning.Data+at,end-at);if(!Word)return false;Model->ReasoningEfforts[Model->ReasoningEffortCount++]=Word;at=end+1;}
        const char* Effort=Model->ReasoningEffortCount?Model->ReasoningEfforts[0]:"";
        for(size_t r=0;r<Model->ReasoningEffortCount;r++)if(!strcmp(Model->ReasoningEfforts[r],"high"))Effort="high";
        if(Builtin)for(size_t r=0;r<Model->ReasoningEffortCount;r++)if(!strcmp(Model->ReasoningEfforts[r],"medium"))Effort="medium";
        Model->DefaultReasoningEffort=xrtStrDup(Effort);
        if(Model->ReasoningEffortCount)Model->Capabilities|=XLLM_CAP_REASONING_OUT|XLLM_CAP_REASONING_CONTROL;
        if(!Model->Name||!Model->ProviderId||!Model->WireModel||!Model->DefaultReasoningEffort)return false;
    }return true;
}
