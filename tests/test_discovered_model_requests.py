"""Discovered profiles build usable requests without inventing capabilities.

Compile current source in place with the existing host. Serialization only:
no account, provider key, external HTTP, copied app, or isolated checkout.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
PROBE=r'''
#define ServiceInit MdoProductServiceInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned Failed;
static void Check(bool Ok,const char* Name){if(!Ok){++Failed;printf("FAIL %s\n",Name);}}
static void RunProfile(MdoModelProtocol Protocol,bool Controlled){
    MdoModelCatalog Catalog={0};MdoProviderEntry Provider={0};MdoModelEntry Model={0};
    char* Efforts[]={"none"};
    Provider.Id="discovered";Provider.Name="Discovered";Provider.TimeoutMilliseconds=2000;
    Provider.Protocols=MdoModelsProtocolFlag(Protocol);
    size_t Index=0;MdoModelsEndpointIndex(Protocol,&Index);
    Provider.Endpoints[Index]=Protocol==MDO_MODEL_PROTOCOL_OPENAI_RESPONSES?"http://127.0.0.1:9/v1/responses":
        Protocol==MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS?"http://127.0.0.1:9/v1/chat/completions":"http://127.0.0.1:9/v1/messages";
    Model.Id="openai-gpt-6.1-terra";Model.Name="gpt-6.1-terra";Model.WireModel="gpt-6.1-terra";Model.ProviderId=Provider.Id;
    Model.Protocols=Provider.Protocols;Model.DefaultProtocol=Protocol;
    Model.Capabilities=XLLM_CAP_TEXT_IN|XLLM_CAP_TOOL_RESULT_IN|XLLM_CAP_TEXT_OUT|XLLM_CAP_TOOL_CALL_OUT|XLLM_CAP_STREAM;
    if(Controlled)Model.Capabilities|=XLLM_CAP_REASONING_CONTROL|XLLM_CAP_REASONING_OUT|XLLM_CAP_PARALLEL_TOOL_CALL;
    Model.WindowMode=XLLM_WINDOW_SHARED_CONTEXT;Model.ContextWindowTokens=65536;Model.MaxInputTokens=57344;
    Model.MaxOutputTokens=8192;Model.OutputReserveTokens=4096;Model.SummaryTokens=2048;
    Model.ReasoningEfforts=Efforts;Model.ReasoningEffortCount=1;Model.DefaultReasoningEffort="none";
    Catalog.Providers=&Provider;Catalog.ProviderCount=1;Catalog.Models=&Model;Catalog.ModelCount=1;
    xllm_error Error={0};xllm_model_profile Profile;
    Check(MdoModelCatalogProfile(&Catalog,Model.Id,Protocol,&Profile,&Error),"profile");
    xllm_request Request;xllmRequestInit(&Request);
    xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,"Hello");
    xllmRequestAddTool(&Request,"inspect","Inspect only","{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",false);
    xllmRequestSetReasoningEffort(&Request,"none");Request.bParallelToolCalls=true;Request.uMaxOutputTokens=8192;
    if(!Controlled){
        xllm_client_config Original;xllmClientConfigInit(&Original);Original.sBaseUrl=Provider.Endpoints[Index];
        Original.sModel=Model.WireModel;Original.sApiKey="fixture-not-used";Original.pModelProfile=&Profile;
        Original.eProvider=Profile.eProvider;Original.sReasoningEffort="none";Original.uMaxOutputTokens=Model.MaxOutputTokens;
        xllm_client* Before=xllmClientCreate(&Original,&Error);
        char* Json=Before?xllmClientBuildRequestJson(Before,&Request,&Error):NULL;
        Check(!Json&&Error.eCode==XLLM_ERROR_INVALID_ARGUMENT&&strstr(Error.sMessage,"capabilities"),"reproduce_original_validation");
        printf("original_protocol_%u=%s\n",(unsigned)Protocol,Error.sMessage);
        xllmFree(Json);xllmClientDestroy(Before);
    }
    MdoModelClientOptions Options;MdoModelClientOptionsInit(&Options);
    Options.ModelId=Model.Id;Options.Protocol=Protocol;Options.ReasoningEffort="none";
    xllm_client* Client=MdoModelClientCreate(&Catalog,&Options,NULL,&Error);
    Check(Client!=NULL,"product_client");
    if(Client){
        xllm_request View;const xllm_request* Prepared=MdoModelsClientRequest(Client,&Request,&View);
        Check(Prepared->pMessages==Request.pMessages&&Prepared->pTools==Request.pTools,"borrow_ledger_and_tools");
        Check(Request.bParallelToolCalls&&!strcmp(Request.sReasoningEffort,"none"),"original_request_unchanged");
        Check(Prepared->bParallelToolCalls==Controlled,"parallel_capability");
        Check(Controlled?Prepared->sReasoningEffort&&!strcmp(Prepared->sReasoningEffort,"none"):Prepared->sReasoningEffort==NULL,"reasoning_capability");
        char* Json=xllmClientBuildRequestJson(Client,Prepared,&Error);
        Check(Json!=NULL,"serialized_request");
        printf("prepared_protocol_%u_controlled_%u=%u\n",(unsigned)Protocol,Controlled,Json!=NULL);
        xllmFree(Json);
        if(!Controlled){
            xllmRequestSetReasoningEffort(&Request,"high");
            Prepared=MdoModelsClientRequest(Client,&Request,&View);
            Json=xllmClientBuildRequestJson(Client,Prepared,&Error);
            Check(!Json,"explicit_unsupported_reasoning_rejected");xllmFree(Json);
            xllmRequestSetReasoningEffort(&Request,"none");Request.uMaxOutputTokens=8193;
            Prepared=MdoModelsClientRequest(Client,&Request,&View);
            Json=xllmClientBuildRequestJson(Client,Prepared,&Error);
            Check(!Json,"output_limit_still_enforced");xllmFree(Json);
        }
        xllmClientDestroy(Client);
    }
    xllmRequestUnit(&Request);
}
static bool SchemaHookRequest(xllm_client* C,xllm_request* R,void* P){(void)C;(void)R;((unsigned*)P)[0]++;return true;}
static bool SchemaHookBody(xllm_client* C,xllm_wire* W,void* P){(void)C;((unsigned*)P)[1]++;char* Copy=malloc(W->iBodySize+1);if(!Copy)return false;memcpy(Copy,W->sBody,W->iBodySize+1);W->sBody=Copy;W->iBodyCapacity=W->iBodySize+1;return true;}
static bool SchemaHookRetry(xllm_client* C,const xllm_diagnostics* D,uint32 N,void* P){(void)C;(void)D;(void)N;((unsigned*)P)[2]++;return true;}
static bool SchemaHookResponseBody(xllm_client* C,xllm_wire* W,void* P){(void)C;(void)W;((unsigned*)P)[3]++;return true;}
static bool SchemaHookTool(xllm_client* C,xllm_tool_call* T,uint32 N,void* P){(void)C;(void)T;(void)N;((unsigned*)P)[4]++;return true;}
static bool SchemaHookResponse(xllm_client* C,xllm_response* R,void* P){(void)C;(void)R;((unsigned*)P)[5]++;return true;}
static void RunToolSchemas(void){
    const char* Good="{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"],\"additionalProperties\":false}";
    const char* Optional="{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"path\"],\"additionalProperties\":false}";
    Check(MdoModelsToolStrictCompatible(Good),"strict_valid");
    Check(!MdoModelsToolStrictCompatible(Optional),"optional_is_not_strict");
    Check(!MdoModelsToolStrictCompatible("{\"type\":\"object\",\"properties\":{},\"required\":[],\"additionalProperties\":true}"),"open_object_is_not_strict");
    Check(!MdoModelsToolStrictCompatible("{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"string\"},\"b\":{\"type\":\"string\"}},\"required\":[\"a\",\"a\"],\"additionalProperties\":false}"),"duplicate_required_rejected");
    Check(!MdoModelsToolStrictCompatible("{\"type\":\"object\",\"properties\":{\"items\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"string\"}},\"required\":[],\"additionalProperties\":false}}},\"required\":[\"items\"],\"additionalProperties\":false}"),"nested_optional_rejected");
    for(unsigned Provider=0;Provider<3;Provider++){
        xllm_provider Protocol=Provider==0?XLLM_PROVIDER_OPENAI_COMPAT:Provider==1?XLLM_PROVIDER_OPENAI_RESPONSES:XLLM_PROVIDER_ANTHROPIC;
        xllm_model_profile Profile;xllmModelProfileInit(&Profile);Profile.sId="schema-test";Profile.sModel="schema-test";Profile.eProvider=Protocol;
        Profile.uCapabilities=XLLM_CAP_TEXT_IN|XLLM_CAP_TEXT_OUT|XLLM_CAP_TOOL_CALL_OUT|XLLM_CAP_TOOL_RESULT_IN|XLLM_CAP_STREAM;
        Profile.eWindowMode=XLLM_WINDOW_SHARED_CONTEXT;Profile.uContextWindowTokens=65536;Profile.uMaxInputTokens=57344;Profile.uMaxOutputTokens=8192;
        xllm_client_config Config;xllmClientConfigInit(&Config);Config.sBaseUrl="http://127.0.0.1:9/v1";Config.sModel="schema-test";
        Config.sApiKey="not-used";Config.eProvider=Protocol;Config.pModelProfile=&Profile;Config.uMaxOutputTokens=1024;xllm_error Error={0};
        xllm_client* Client=xllmClientCreate(&Config,&Error);Check(Client!=NULL,"schema_client");
        xllm_request Request;xllmRequestInit(&Request);xllmRequestAddTextMessage(&Request,XLLM_ROLE_USER,"OK");
        xllmRequestAddTool(&Request,"optional","Optional fields",Optional,true);xllmRequestAddTool(&Request,"valid","Strict fields",Good,true);
        xllmRequestAddTool(&Request,"best_effort","Explicit non-strict",Good,false);
        xllm_request View=Request;if(Client)MdoModelsClientRequest(Client,&Request,&View);
        Check(Request.pTools[0].bStrict&&Request.pTools[0].sParametersJson==View.pTools[0].sParametersJson&&
            Request.pMessages==View.pMessages&&Request.pTools[0].sName==View.pTools[0].sName,"caller_and_schema_unchanged");
        char* Json=Client?xllmClientBuildRequestJson(Client,&View,&Error):NULL;Check(Json!=NULL,"adapted_tool_wire_json");
        xllm_wire Wire={0};Wire.sBody=Json;Wire.iBodySize=Json?strlen(Json):0;Wire.iBodyCapacity=Wire.iBodySize+1;
        unsigned Seen[6]={0};xllm_hooks Original={0},Wrapped;MdoModelHooks Context;
        Original.pOnRequest=SchemaHookRequest;Original.pOnRequestBody=SchemaHookBody;Original.pOnRetry=SchemaHookRetry;
        Original.pOnResponseBody=SchemaHookResponseBody;Original.pOnToolCall=SchemaHookTool;Original.pOnResponse=SchemaHookResponse;Original.pUserData=Seen;
        Request.pHooks=&Original;View.pHooks=&Original;MdoModelsRequestHooks(&View,&Context,&Wrapped);
        Check(Json&&Wrapped.pOnRequest(Client,&View,Wrapped.pUserData)&&Wrapped.pOnRequestBody(Client,&Wire,Wrapped.pUserData),"strict_wire_seam");
        Wrapped.pOnRetry(Client,NULL,2,Wrapped.pUserData);Wrapped.pOnResponseBody(Client,&Wire,Wrapped.pUserData);
        Wrapped.pOnToolCall(Client,NULL,0,Wrapped.pUserData);Wrapped.pOnResponse(Client,NULL,Wrapped.pUserData);
        for(unsigned I=0;I<6;I++)Check(Seen[I]==1,"caller_hook_forwarded_once");
        Check(Request.pHooks==&Original&&Original.pUserData==Seen,"caller_hooks_unchanged");
        xllm_hooks RetryOnly={0};RetryOnly.pOnRetry=SchemaHookRetry;RetryOnly.pUserData=Seen;View.pHooks=&RetryOnly;
        MdoModelsRequestHooks(&View,&Context,&Wrapped);
        Check(!Wrapped.pOnRequest&&!Wrapped.pOnResponseBody&&!Wrapped.pOnToolCall&&!Wrapped.pOnResponse&&Wrapped.pOnRetry&&Wrapped.pOnRequestBody,"absent_hooks_stay_absent");
        if(Json&&Protocol!=XLLM_PROVIDER_ANTHROPIC){xvalue* Root=xrtJsonParse(xrtStrView(Wire.sBody));
            xvalue* Tools=xrtValueObjectGet(Root,XRT_STR_LITERAL("tools"));xvalue* Tool=xrtValueArrayGet(Tools,0);
            if(Protocol==XLLM_PROVIDER_OPENAI_COMPAT)Tool=xrtValueObjectGet(Tool,XRT_STR_LITERAL("function"));
            bool Strict=true;Check(xrtValueGetBool(xrtValueObjectGet(Tool,XRT_STR_LITERAL("strict")),&Strict)&&!Strict,"wire_explicit_false");
            const xvalue* Params=xrtValueObjectGet(Tool,XRT_STR_LITERAL("parameters"));
            Check(xrtValueCount(xrtValueObjectGet(Params,XRT_STR_LITERAL("required")))==1&&xrtValueCount(xrtValueObjectGet(Params,XRT_STR_LITERAL("properties")))==2,"optional_schema_not_rewritten");
            Tool=xrtValueArrayGet(Tools,1);if(Protocol==XLLM_PROVIDER_OPENAI_COMPAT)Tool=xrtValueObjectGet(Tool,XRT_STR_LITERAL("function"));
            Check(xrtValueGetBool(xrtValueObjectGet(Tool,XRT_STR_LITERAL("strict")),&Strict)&&Strict,"valid_strict_preserved");
            Tool=xrtValueArrayGet(Tools,2);if(Protocol==XLLM_PROVIDER_OPENAI_COMPAT)Tool=xrtValueObjectGet(Tool,XRT_STR_LITERAL("function"));
            Check(xrtValueGetBool(xrtValueObjectGet(Tool,XRT_STR_LITERAL("strict")),&Strict)&&!Strict,"explicit_nonstrict_preserved");xrtValueRelease(Root);}
        if(Wire.sBody!=Json)xllmFree(Wire.sBody);xllmFree(Json);xllmRequestUnit(&Request);xllmClientDestroy(Client);
    }
    printf("strict_tool_contract=1\n");
}
void ServiceInit(XS_HostInfo* Host){
    (void)Host;Check(MdoHomeInit()&&MdoConfigInit(),"init");
    RunProfile(MDO_MODEL_PROTOCOL_OPENAI_RESPONSES,false);
    RunProfile(MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS,false);
    RunProfile(MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES,false);
    RunProfile(MDO_MODEL_PROTOCOL_OPENAI_RESPONSES,true);
    RunToolSchemas();
    MdoConfigUnit();MdoHomeUnit();printf("failed=%u\nprobe_done=1\n",Failed);fflush(stdout);
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    from tools import build_mdo
    build_mdo.write_if_changed(build_mdo.UNITY_PATH,build_mdo.generated_unity(
        build_mdo.load_object(build_mdo.LOCK_PATH),build_mdo.source_list()).encode())
    probe=ROOT/'app/generated/discovered_model_request_test.c'
    config=ROOT/'app/xs.discovered-model-request-test.json'
    assert not probe.exists() and not config.exists(),'An owned probe is already running'
    evidence=ROOT/'.build/acceptance/discovered-model-20261030';evidence.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='discovered-model-',dir=ROOT/'.build') as raw:
        base=Path(raw)
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        config.write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'model-profile-probe','ip':'127.0.0.1','port':port,
            'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/discovered_model_request_test.c'}}]}))
        probe.write_text(PROBE,encoding='utf-8');proc=None
        try:
            log=base/'runtime.log'
            with log.open('wb') as output:
                proc=subprocess.Popen([str(args.host.resolve()),str(config)],cwd=ROOT/'app',stdout=output,stderr=output,
                    env={**os.environ,'MDO_HOME':str(base/'home')},creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                until=time.monotonic()+35
                while time.monotonic()<until and proc.poll() is None:
                    if 'probe_done=1' in log.read_text(errors='replace'):break
                    time.sleep(.1)
            text=log.read_text(errors='replace');(evidence/'native-request-contract.txt').write_text(text,encoding='utf-8')
            for expected in ['prepared_protocol_2_controlled_0=1','prepared_protocol_1_controlled_0=1',
                'prepared_protocol_3_controlled_0=1','prepared_protocol_2_controlled_1=1','strict_tool_contract=1','failed=0','probe_done=1']:
                assert expected in text,text[-16000:]
            print('Discovered Chat/Responses/Anthropic profiles: serialization, request ownership and capability boundaries PASS')
        finally:
            if proc and proc.poll() is None:
                if os.name=='nt':subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
                else:proc.terminate()
                proc.wait(timeout=8)
            probe.unlink(missing_ok=True);config.unlink(missing_ok=True)


if __name__=='__main__':
    import sys
    sys.path.insert(0,str(ROOT));main()
