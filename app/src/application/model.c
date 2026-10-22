#include "internal.h"
#include "../../include/mdo/models.h"
#include "../../include/mdo/time.h"
static xvalue* SdkUsageValue(const xllm_usage* U){
    xvalue* V=xrtValueObject();SdkPutUInt(V,"input_tokens",U->uInputTokens);SdkPutUInt(V,"output_tokens",U->uOutputTokens);SdkPutUInt(V,"total_tokens",U->uTotalTokens);SdkPutUInt(V,"cached_input_tokens",U->uCachedInputTokens);SdkPutUInt(V,"cache_write_tokens",U->uCacheWriteTokens);SdkPutUInt(V,"reasoning_tokens",U->uReasoningTokens);return V;
}
typedef struct SdkModelStream {const mdo_sdk_context_v1* Context;cstr Handle;bool Overflow;} SdkModelStream;
static bool SdkModelEvent(void* User,const xllm_event* Event){
    SdkModelStream* S=User;if(SdkCancelled(S->Context))return false;
    xvalue* V=xrtValueObject();SdkPutUInt(V,"kind",Event->eKind);
    if(Event->eKind==XLLM_EVENT_TEXT_DELTA||Event->eKind==XLLM_EVENT_REASONING_DELTA){
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Event->as.tText.sData,Event->as.tText.iLen)));SdkPutUInt(V,"block",Event->as.tText.iBlock);
    }else if(Event->eKind==XLLM_EVENT_TOOL_CALL_DELTA){SdkPut(V,"id",Event->as.tToolCall.sIdDelta);SdkPut(V,"name",Event->as.tToolCall.sNameDelta);SdkPut(V,"arguments",Event->as.tToolCall.sArgumentsDelta);SdkPutUInt(V,"index",Event->as.tToolCall.iIndex);}
    else if(Event->eKind==XLLM_EVENT_USAGE)xrtValueObjectSetNew(V,XRT_STR_LITERAL("usage"),SdkUsageValue(&Event->as.tUsage));
    else if(Event->eKind==XLLM_EVENT_BLOCK_META){SdkPutUInt(V,"block",Event->as.tBlockMeta.iBlock);SdkPutUInt(V,"block_kind",Event->as.tBlockMeta.eKind);xrtValueObjectSetNew(V,XRT_STR_LITERAL("end"),xrtValueBool(Event->as.tBlockMeta.bEnd));}
    else if(Event->eKind==XLLM_EVENT_RESPONSE_START||Event->eKind==XLLM_EVENT_RESPONSE_DONE){SdkPutUInt(V,"http_status",Event->as.tResponse.uHttpStatus);SdkPut(V,"request_id",Event->as.tResponse.sRequestId);}
    char* Json=SdkJson(V);char* Line=Json?xrtFormat("%s\n",Json):NULL;xrtFree(Json);xrtValueRelease(V);
    xvalue* Params=xrtValueObject(),*Result=NULL;SdkPut(Params,"handle",S->Handle);SdkPut(Params,"text",Line);xrtFree(Line);bool Found=false;mdo_error_v1 Error={0};
    /* Engine callbacks must never block. A slow consumer gets an explicit
     * overflow and cancels this model call; no delta is silently discarded. */
    bool Ok=MdoApplicationStreamsCall(S->Context,"stream.write",Params,&Result,&Error,&Found);xrtValueRelease(Params);xrtValueRelease(Result);if(!Ok)S->Overflow=true;return Ok;
}
static bool SdkModelMessage(const xvalue* Source,xllm_request* Request){
    cstr Role=SdkField(Source,"role"),Text=SdkField(Source,"content"),Reasoning=SdkField(Source,"reasoning"),CallId=SdkField(Source,"tool_call_id"),Native=SdkField(Source,"native");
    const cstr Roles[]={"system","user","assistant","tool"};size_t Kind=0;while(Kind<4&&(!Role||strcmp(Role,Roles[Kind])))Kind++;
    if(Kind==4)return false;
    xllm_message M;xllmMessageInit(&M,(xllm_role)Kind);bool Ok=(!Text||xllmMessageSetContent(&M,Text))&&(!Reasoning||xllmMessageSetReasoning(&M,Reasoning))&&(!CallId||xllmMessageSetToolCallId(&M,CallId))&&(!Native||xllmMessageSetNative(&M,Native));
    const xvalue* Calls=SdkGet(Source,"tool_calls"),*Parts=SdkGet(Source,"parts");
    if((Calls&&xrtValueType(Calls)!=XVALUE_ARRAY)||(Parts&&(Text||xrtValueType(Parts)!=XVALUE_ARRAY)))Ok=false;
    for(size_t i=0;Ok&&i<xrtValueCount(Calls);i++){
        const xvalue* T=xrtValueArrayGet(Calls,i);cstr Id=SdkField(T,"id"),Name=SdkField(T,"name");char* Args=SdkGet(T,"arguments")?SdkJson(SdkGet(T,"arguments")):xrtStrDup("{}");
        cstr Raw=SdkField(T,"arguments_json");Ok=Id&&Name&&Args&&xllmMessageAddToolCall(&M,Id,Name,Raw?Raw:Args);xrtFree(Args);
    }
    for(size_t i=0;Ok&&i<xrtValueCount(Parts);i++){
        const xvalue* T=xrtValueArrayGet(Parts,i);cstr Type=SdkField(T,"type"),Mime=SdkField(T,"media_type"),Url=SdkField(T,"url");xllm_part Part;xllmPartInit(&Part,XLLM_PART_TEXT);
        if(Type&&!strcmp(Type,"text")){cstr Value=SdkField(T,"text");Ok=Value&&xllmPartSetText(&Part,Value);}
        else if(Type&&!strcmp(Type,"image")&&Url)Ok=xllmPartSetImageUrl(&Part,Url,Mime);
        else if(Type&&(!strcmp(Type,"image")||!strcmp(Type,"audio")||!strcmp(Type,"file"))){unsigned char* Bytes=NULL;size_t Size=0;mdo_error_v1 E={0};Ok=SdkInputBytes(T,&Bytes,&Size,&E);if(Ok)Ok=!strcmp(Type,"image")?xllmPartSetImageData(&Part,Bytes,Size,Mime):!strcmp(Type,"audio")?xllmPartSetAudioData(&Part,Bytes,Size,Mime):xllmPartSetFileData(&Part,Bytes,Size,Mime);xrtFree(Bytes);}
        else if(Type&&!strcmp(Type,"native")){cstr Value=SdkField(T,"json"),NativeType=SdkField(T,"native_type");Ok=Value&&NativeType&&xllmPartSetNative(&Part,NativeType,Value);}
        else Ok=false;
        if(Ok)Ok=xllmMessageAddPart(&M,&Part);
        xllmPartUnit(&Part);
    }
    if(Ok)Ok=xllmRequestAddMessage(Request,&M);
    xllmMessageUnit(&M);return Ok;
}
static bool SdkModelCall(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Handle=SdkField(P,"stream");bool Dispatched=false;
    if(Handle){xvalue* Params=xrtValueObject(),*Result=NULL;SdkPut(Params,"handle",Handle);bool Found=false;
        bool Ok=MdoApplicationStreamsCall(C,"stream.stat",Params,&Result,E,&Found),Closed=false;xrtValueGetBool(SdkGet(Result,"closed"),&Closed);xrtValueRelease(Params);xrtValueRelease(Result);
        if(!Ok)return false;
        if(Closed)return MdoApplicationError(E,"sdk_stream_closed","Model output stream is already closed","stream",409,false);
    }
    const xvalue *Messages=SdkGet(P,"messages"),*Tools=SdkGet(P,"tools");
    if(xrtValueType(Messages)!=XVALUE_ARRAY||!xrtValueCount(Messages)||xrtValueCount(Messages)>4096||(Tools&&xrtValueType(Tools)!=XVALUE_ARRAY)||xrtValueCount(Tools)>256)
        return MdoApplicationError(E,"sdk_arguments_invalid","Supply 1..4096 messages and an optional tool array","model",400,false);
    MdoModelCatalog* Catalog=MdoModelCatalogSnapshot();MdoModelClientOptions Options;MdoModelClientOptionsInit(&Options);Options.ModelId=SdkField(P,"model");Options.ReasoningEffort=SdkField(P,"reasoning_effort");
    uint64 Max=0,Protocol=0;bool Ok=(!SdkGet(P,"max_output_tokens")||SdkUInt(SdkGet(P,"max_output_tokens"),&Max))&&Max<=UINT32_MAX&&(!SdkGet(P,"protocol")||SdkUInt(SdkGet(P,"protocol"),&Protocol))&&Protocol<=3;
    Options.MaxOutputTokens=(uint32)Max;Options.Protocol=(MdoModelProtocol)Protocol;
    xllm_request Request;xllmRequestInit(&Request);xllm_response* Response=NULL;xllm_client* Client=NULL;xllm_error Error;xllmErrorInit(&Error);xllm_model_profile Profile;
    if(!Catalog){Ok=false;MdoApplicationError(E,"sdk_unavailable","Model catalog unavailable","model",503,false);goto done;}
    for(size_t i=0;Ok&&i<xrtValueCount(Messages);i++)Ok=SdkModelMessage(xrtValueArrayGet(Messages,i),&Request);
    for(size_t i=0;Ok&&i<xrtValueCount(Tools);i++){
        const xvalue* T=xrtValueArrayGet(Tools,i);char* Schema=SdkJson(SdkGet(T,"parameters"));bool Strict=false;xrtValueGetBool(SdkGet(T,"strict"),&Strict);cstr Name=SdkField(T,"name");Ok=Name&&Schema&&xllmRequestAddTool(&Request,Name,SdkField(T,"description"),Schema,Strict);xrtFree(Schema);
    }
    const xvalue* Extra=SdkGet(P,"extra_body");if(Ok&&Extra){char* Json=SdkJson(Extra);Ok=xrtValueType(Extra)==XVALUE_OBJECT&&Json&&xllmRequestSetExtraBody(&Request,Json);xrtFree(Json);}
    if(!Ok){MdoApplicationError(E,"sdk_arguments_invalid","Invalid model options, message content, parts or tool schema","model",400,false);goto done;}
    if(!MdoModelCatalogProfile(Catalog,Options.ModelId,Options.Protocol,&Profile,&Error)){Ok=false;goto done;}
    MdoModelInfo Info={0};Info.Size=sizeof(Info);Ok=MdoModelCatalogModelFind(Catalog,Profile.sId,&Info)&&xllmRequestSetModel(&Request,Info.WireModel)&&xllmRequestSetReasoningEffort(&Request,Options.ReasoningEffort?Options.ReasoningEffort:Info.DefaultReasoningEffort);
    Request.uMaxOutputTokens=Options.MaxOutputTokens?Options.MaxOutputTokens:Info.MaxOutputTokens;
    MdoApplicationInvocation* Invocation=C->Opaque;xllmRequestSetCancel(&Request,Invocation?Invocation->Cancel:NULL);xllmRequestSetDeadline(&Request,C->Deadline?C->Deadline:MdoDeadlineAfterUs(600000000));
    SdkModelStream Stream={C,Handle,false};xllm_stream_callbacks Callbacks={&Stream,SdkModelEvent};
    if(Ok&&!MdoModelIsOnline(Catalog,Info.Id)){Client=MdoModelClientCreate(Catalog,&Options,NULL,&Error);Ok=Client!=NULL;}
    if(Ok){Dispatched=true;Ok=(MdoModelIsOnline(Catalog,Info.Id)?MdoModelOnlineComplete(Catalog,&Options,&Request,Handle?&Callbacks:NULL,&Response,&Error):MdoModelComplete(Client,&Request,Handle?&Callbacks:NULL,&Response,&Error))==XLLM_RESULT_OK;}
    if(Stream.Overflow){Ok=MdoApplicationError(E,"sdk_stream_overflow","Model stream consumer did not drain its buffer; call cancelled without dropping deltas","stream",409,false);}
    if(Ok&&Response){
        *R=xrtValueObject();SdkPut(*R,"id",Response->sId);SdkPut(*R,"model",Response->sModel);SdkPut(*R,"text",Response->sContent);SdkPut(*R,"reasoning",Response->sReasoningContent);SdkPut(*R,"finish_reason",Response->sFinishReason);SdkPut(*R,"request_id",Response->sRequestId);SdkPutUInt(*R,"http_status",Response->uHttpStatus);
        SdkPut(*R,"refusal",Response->sRefusal);SdkPutUInt(*R,"finish",Response->eFinish);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("usage"),SdkUsageValue(&Response->tUsage));SdkPutUInt(*R,"attempts",Response->tStats.uAttempts);
        xvalue* Calls=xrtValueArray();for(size_t i=0;i<Response->iToolCallCount;i++){xvalue* T=xrtValueObject();SdkPut(T,"id",Response->pToolCalls[i].sId);SdkPut(T,"name",Response->pToolCalls[i].sName);SdkPut(T,"arguments_json",Response->pToolCalls[i].sArgumentsJson);xrtValueArrayAppendNew(Calls,T);}xrtValueObjectSetNew(*R,XRT_STR_LITERAL("tool_calls"),Calls);
        xvalue* Blocks=xrtValueArray();for(size_t i=0;i<Response->iBlockCount;i++){const xllm_block* Block=&Response->pBlocks[i];xvalue* B=xrtValueObject();SdkPutUInt(B,"kind",Block->eKind);SdkPut(B,"text",Block->sText);SdkPutUInt(B,"tool_index",Block->iToolIndex);SdkPut(B,"native",Block->sNative);xrtValueArrayAppendNew(Blocks,B);}xrtValueObjectSetNew(*R,XRT_STR_LITERAL("blocks"),Blocks);
        xvalue* Stats=xrtValueObject();SdkPutUInt(Stats,"connect_ms",Response->tStats.uConnectMs);SdkPutUInt(Stats,"first_byte_ms",Response->tStats.uFirstByteMs);SdkPutUInt(Stats,"first_token_ms",Response->tStats.uFirstTokenMs);SdkPutUInt(Stats,"total_ms",Response->tStats.uTotalMs);SdkPutUInt(Stats,"request_bytes",Response->tStats.uRequestBytes);SdkPutUInt(Stats,"response_bytes",Response->tStats.uResponseBytes);xrtValueObjectSetNew(*R,XRT_STR_LITERAL("stats"),Stats);
    }
done:
    if(Handle){xvalue* Params=xrtValueObject(),*Result=NULL;SdkPut(Params,"handle",Handle);bool Found=false;mdo_error_v1 CloseError={0};(void)MdoApplicationStreamsCall(C,"stream.close",Params,&Result,&CloseError,&Found);xrtValueRelease(Params);xrtValueRelease(Result);}
    if(Ok&&!Response)Ok=MdoApplicationError(E,"sdk_model_response_missing","Model boundary returned no response object","model",502,false);
    if(!Ok&&!E->Code[0])MdoApplicationError(E,MdoModelErrorKind(&Error),MdoModelErrorMessage(&Error),"model",Error.eCode==XLLM_ERROR_CANCELLED?499:Error.eCode==XLLM_ERROR_TIMEOUT?504:Error.eCode==XLLM_ERROR_INVALID_ARGUMENT?400:Error.iHttpStatus?(uint32)Error.iHttpStatus:502,false);
    if(!Ok&&Dispatched&&(E->Status>=500||E->Status==499||!strcmp(E->Code,"sdk_stream_overflow")))E->MayHaveCompleted=true;
    xllmResponseDestroy(Response);xllmClientDestroy(Client);xllmRequestUnit(&Request);MdoModelCatalogRelease(Catalog);return Ok;
}
