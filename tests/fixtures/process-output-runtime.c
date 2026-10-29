#include <xsbase.h>
#include <xwork.h>
#include <xllm-time.h>
#include "tool-catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static xvalue* Checks;
static xmutex* CounterLock;
static unsigned Acquired,Released;
static unsigned Serial;
static xllm_executor Executor;
static xllm_executor_result Last;
static bool DenyOutput,DeniedOutputSeen;
static xwork_permission_decision Permission(void* User,const xwork_permission_request* Request){
    (void)User;
    if(DenyOutput)for(size_t i=0;i<Request->iResourceCount;i++){
        const xwork_permission_resource* R=&Request->pResources[i];
        if(R->eKind==XWORK_RESOURCE_PATH&&(R->uAccess&XWORK_RESOURCE_ACCESS_WRITE)&&strstr(R->sResource,"denied.log")){
            DeniedOutputSeen=true;return XWORK_PERMISSION_DENY;
        }
    }
    return XWORK_PERMISSION_DEFAULT;
}
static bool Acquire(void* U,void** Lease){(void)U;*Lease=(void*)1;xrtMutexLock(CounterLock);Acquired++;xrtMutexUnlock(CounterLock);return true;}
static void Release(void* U,void* Lease){(void)U;(void)Lease;xrtMutexLock(CounterLock);Released++;xrtMutexUnlock(CounterLock);}
static void Flag(const char* Name,bool Ok){xrtValueObjectSetNew(Checks,xrtStrView(Name),xrtValueBool(Ok));fprintf(stderr,"process-output check %s: %s\n",Name,Ok?"PASS":"FAIL");}
static bool Bool(const xvalue* V,const char* K){bool B=false;return xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(K)),&B)&&B;}
static uint64 Number(const xvalue* V,const char* K){uint64 N=0;int64 I=0;const xvalue* P=xrtValueObjectGet(V,xrtStrView(K));return xrtValueGetUInt(P,&N)?N:xrtValueGetInt(P,&I)&&I>=0?(uint64)I:0;}
static const char* Text(const xvalue* V,const char* K){xstrview T={0};return xrtValueGetString(xrtValueObjectGet(V,xrtStrView(K)),&T)?T.Data:"";}
static xvalue* Tool(const char* Name,xvalue* Args){
    char Id[48];snprintf(Id,sizeof(Id),"process-output-%u",++Serial);char* Json=xrtJsonStringify(Args,false,NULL);xrtValueRelease(Args);
    xllm_tool_call Call={0};Call.sId=Id;Call.sName=(char*)Name;Call.sArgumentsJson=Json;xllm_executor_ctx C={0};C.uDeadline=XLLM_DEADLINE_NEVER;
    bool Ran=Json&&Executor.pExecute(Executor.pUserData,&Call,&C,&Last);if(Json)xrtFree(Json);
    if(!Ran){fprintf(stderr,"fixture tool transport failed: %s\n",Name);return NULL;}
    if(!Last.bSuccess)fprintf(stderr,"fixture tool outcome %s: %.600s\n",Name,Last.sContent?Last.sContent:"");
    const char* Body=Last.sContent?strchr(Last.sContent,'{'):NULL;
    return Body?xrtJsonParse(xrtStrView(Body)):NULL;
}
static xvalue* Args(const char* Mode){
    xvalue* V=xrtValueObject(),*A=xrtValueArray();const char* Values[]={getenv("MDO_OUTPUT_PYTHON"),"-u",getenv("MDO_OUTPUT_CHILD"),Mode};
    for(unsigned i=0;i<4;i++)xrtValueArrayAppendNew(A,xrtValueString(xrtStrView(Values[i])));
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("argv"),A);return V;
}
static xvalue* IdArgs(uint64 Id){xvalue* V=xrtValueObject();xrtValueObjectSetNew(V,XRT_STR_LITERAL("task_id"),xrtValueUInt(Id));return V;}
static bool Balanced(void){xrtMutexLock(CounterLock);bool B=Acquired==Released;xrtMutexUnlock(CounterLock);return B;}
static bool WaitFor(uint64 Id){
    xvalue* V=xrtValueObject(),*A=xrtValueArray();xrtValueArrayAppendNew(A,xrtValueUInt(Id));xrtValueObjectSetNew(V,XRT_STR_LITERAL("task_ids"),A);
    xrtValueObjectSetNew(V,XRT_STR_LITERAL("timeout_ms"),xrtValueUInt(5000));xvalue* R=Tool("wait",V);bool Ok=R&&Bool(R,"ready");xrtValueRelease(R);return Ok;
}
void ServiceInit(XS_HostInfo* Host){
    (void)Host;Checks=xrtValueObject();CounterLock=xrtMutexCreate();xwork_error Error={0};
    xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_error LE={0};xllm_session* S=xllmSessionCreate(&SC,&LE);
    xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);RC.iMaxRetainedTaskBytes=16384;
    xwork_runtime* Runtime=xworkRuntimeCreate(&RC,&Error);xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);
    DC.sId="process-output-fixture";DC.sModel="no-model-calls";DC.bAutoSaveSession=false;DC.bRequireVerificationAfterWrite=false;
    DC.eApprovalMode=XWORK_APPROVAL_AUTO;DC.iMaxInlineToolBytes=12288u;
    xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&Error);
    xroot Root=xrtRootOpen(getenv("MDO_OUTPUT_ARTIFACTS"));xwork_artifact_store* Store=Root?xworkArtifactStoreCreate(Root,&Error):NULL;
    xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=S;AO.sWorkspaceRoot=getenv("MDO_OUTPUT_WORKSPACE");AO.pArtifactStore=Store;
    if(getenv("MDO_OUTPUT_LEGACY_ROOT")){AO.pArtifactStore=NULL;AO.sArtifactDirectory="artifacts";}
    AO.OnPermission=Permission;
    xwork_agent* Agent=S&&Runtime&&Definition&&Store?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&Error):NULL;
    bool Ok=Agent&&xworkAgentRegisterMcpDiscoveryTools(Agent,&Error)&&xworkAgentSetProcessOwner(Agent,Acquire,Release,NULL,&Error)&&xworkExecutorBind(&Executor,Agent,&Error);
    Flag("agent_initialized",Ok);xvalue* Report=xrtValueObject();
    if(!Ok)goto done;
    bool Compact=false,Legacy=false,Discover=false,Load=false,Fetch=false;
    for(size_t i=0;i<xworkAgentToolCount(Agent);i++) {
        xwork_tool_info Info={0};if(!xworkAgentToolAt(Agent,i,&Info))continue;
        Compact=Compact||!strcmp(Info.sName,"status");Discover=Discover||!strcmp(Info.sName,"tools");
        Load=Load||!strcmp(Info.sName,"load");Fetch=Fetch||!strcmp(Info.sName,"fetch");
        Legacy=Legacy||!strcmp(Info.sName,"poll")||!strcmp(Info.sName,"tool_search")||!strcmp(Info.sName,"tool_load")||!strcmp(Info.sName,"mcp_read");
    }
    Flag("discovery_advertises_compact_names_only",Compact&&Discover&&Load&&Fetch&&!Legacy);
    const char* Old[]={"poll","task_status","ask_user","web_search","web_open","web_find","tool_search","tool_load","mcp_read","WebSearch","WebFetch"};
    const char* New[]={"status","status","ask","search","open","find","tools","load","fetch","search","open"};bool Aliases=true;
    for(size_t i=0;i<sizeof(Old)/sizeof(*Old);i++)Aliases=Aliases&&!MdoBuiltinToolFind(Old[i])&&MdoBuiltinToolFind(New[i])&&!strcmp(MdoBuiltinToolCanonicalId(Old[i]),New[i]);
    Flag("product_aliases_are_reserved_but_not_advertised",Aliases);
    xvalue* R=Tool("exec",Args("short"));const xvalue* Out=xrtValueObjectGet(R,XRT_STR_LITERAL("stdout"));const xvalue* Err=xrtValueObjectGet(R,XRT_STR_LITERAL("stderr"));
    Flag("short_inline_separate",R&&Last.bSuccess&&Bool(R,"output_complete")&&
        (!strcmp(Text(Out,"text"),"short-out\n")||!strcmp(Text(Out,"text"),"short-out\r\n"))&&
        (!strcmp(Text(Err,"text"),"short-err\n")||!strcmp(Text(Err,"text"),"short-err\r\n"))&&!Text(Out,"path")[0]);xrtValueRelease(R);
    xvalue* Denied=Args("short");xrtValueObjectSetNew(Denied,XRT_STR_LITERAL("cwd"),xrtValueString(XRT_STR_LITERAL("sub")));
    xrtValueObjectSetNew(Denied,XRT_STR_LITERAL("stdout_path"),xrtValueString(XRT_STR_LITERAL("denied.log")));
    xrtMutexLock(CounterLock);unsigned Prior=Acquired;xrtMutexUnlock(CounterLock);DenyOutput=true;R=Tool("exec",Denied);DenyOutput=false;
    xrtMutexLock(CounterLock);bool NoLaunch=Prior==Acquired;xrtMutexUnlock(CounterLock);
    Flag("explicit_output_requires_write_permission_before_launch",DeniedOutputSeen&&NoLaunch&&!Last.bSuccess);xrtValueRelease(R);
    xvalue* P=Args("short");xrtValueObjectSetNew(P,XRT_STR_LITERAL("cwd"),xrtValueString(XRT_STR_LITERAL("sub")));
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("stdout_path"),xrtValueString(XRT_STR_LITERAL("chosen-out.log")));
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("stderr_path"),xrtValueString(XRT_STR_LITERAL("chosen-err.log")));
    xvalue* Retry=xrtValueClone(P);R=Tool("exec",P);Out=xrtValueObjectGet(R,XRT_STR_LITERAL("stdout"));
    Flag("explicit_paths_run_successfully",R&&Last.bSuccess&&Bool(R,"output_complete")&&Text(Out,"native_path")[0]);
    xrtValueObjectSetNew(Report,XRT_STR_LITERAL("explicit_stdout"),xrtValueString(xrtStrView(Text(Out,"native_path"))));xrtValueRelease(R);
    xrtMutexLock(CounterLock);unsigned Before=Acquired;xrtMutexUnlock(CounterLock);
    R=Tool("exec",Retry);xrtMutexLock(CounterLock);bool NotLaunched=Before==Acquired;xrtMutexUnlock(CounterLock);
    Flag("existing_output_is_preserved_before_launch",!Last.bSuccess&&NotLaunched);xrtValueRelease(R);
    R=Tool("exec",Args("failure"));Flag("nonzero_exit_retains_output",R&&!Last.bSuccess&&Number(R,"exit_code")==7u&&Bool(R,"output_complete"));xrtValueRelease(R);
    P=xrtValueObject();xvalue* Missing=xrtValueArray();xrtValueArrayAppendNew(Missing,xrtValueString(XRT_STR_LITERAL("mdo-no-such-exec-output-qa")));xrtValueObjectSetNew(P,XRT_STR_LITERAL("argv"),Missing);
    R=Tool("exec",P);Flag("launch_error_keeps_native_cause_and_partial_metadata",!Last.bSuccess&&Last.sContent&&strstr(Last.sContent,"exec/start-or-capture")&&(strstr(Last.sContent,"resolved")||strstr(Last.sContent,"not found"))&&strstr(Last.sContent,"stdout"));xrtValueRelease(R);
    P=Args("short");xrtValueObjectSetNew(P,XRT_STR_LITERAL("merge_stderr"),xrtValueBool(true));R=Tool("exec",P);
    Out=xrtValueObjectGet(R,XRT_STR_LITERAL("stdout"));Flag("explicit_merge_retains_both_streams",R&&Last.bSuccess&&Bool(R,"stderr_merged")&&strstr(Text(Out,"text"),"short-out")&&strstr(Text(Out,"text"),"short-err"));xrtValueRelease(R);
    P=Args("long");xrtValueObjectSetNew(P,XRT_STR_LITERAL("timeout_ms"),xrtValueUInt(100));R=Tool("exec",P);
    Flag("command_timeout_is_a_tool_outcome",R&&!Last.bSuccess&&!strcmp(Text(R,"state"),"timed_out")&&Balanced());xrtValueRelease(R);
    R=Tool("exec",Args("large"));Flag("large_exec_complete",R&&Last.bSuccess&&Bool(R,"output_complete"));
    Out=xrtValueObjectGet(R,XRT_STR_LITERAL("stdout"));Err=xrtValueObjectGet(R,XRT_STR_LITERAL("stderr"));
    xrtValueObjectSetNew(Report,XRT_STR_LITERAL("exec_stdout"),xrtValueString(xrtStrView(Text(Out,"native_path"))));
    xrtValueObjectSetNew(Report,XRT_STR_LITERAL("exec_stderr"),xrtValueString(xrtStrView(Text(Err,"native_path"))));
    char* Locator=xrtStrDup(Text(Out,"path"));Flag("large_has_paths",Locator&&Locator[0]&&Text(Err,"path")[0]);xrtValueRelease(R);
    P=xrtValueObject();xrtValueObjectSetNew(P,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Locator)));xrtValueObjectSetNew(P,XRT_STR_LITERAL("max_bytes"),xrtValueUInt(4096));
    R=Tool("read",P);Flag("read_log_outside_workspace",Last.bSuccess&&Last.sContent&&strstr(Last.sContent,"MDO_STDOUT_BEGIN"));xrtValueRelease(R);
    P=xrtValueObject();xrtValueObjectSetNew(P,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Locator)));
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("pattern"),xrtValueString(XRT_STR_LITERAL("line 060000")));
    R=Tool("grep",P);Flag("grep_finds_late_large_log_match",Last.bSuccess&&Last.sContent&&strstr(Last.sContent,"line 060000")&&strstr(Last.sContent,"@results/process/"));xrtValueRelease(R);
    char* Parent=xrtStrDup(Locator);char* Slash=Parent?strrchr(Parent,'/'):NULL;if(Slash)*Slash=0;
    P=xrtValueObject();xrtValueObjectSetNew(P,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Parent)));
    R=Tool("ls",P);Flag("ls_result_directory",Last.bSuccess&&Last.sContent&&strstr(Last.sContent,"stdout.log")&&!strstr(Last.sContent,".receipt.json"));xrtValueRelease(R);
    P=xrtValueObject();xrtValueObjectSetNew(P,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Parent)));
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("pattern"),xrtValueString(XRT_STR_LITERAL("*.log")));
    R=Tool("glob",P);Flag("glob_returns_readable_log_locator",Last.bSuccess&&Last.sContent&&strstr(Last.sContent,"@results/process/")&&strstr(Last.sContent,"stdout.log"));xrtValueRelease(R);xrtFree(Parent);
    P=xrtValueObject();xrtValueObjectSetNew(P,XRT_STR_LITERAL("path"),xrtValueString(xrtStrView(Locator)));xrtValueObjectSetNew(P,XRT_STR_LITERAL("content"),xrtValueString(XRT_STR_LITERAL("must not mutate")));
    R=Tool("write",P);Flag("result_alias_read_only",!Last.bSuccess);xrtValueRelease(R);if(Locator)xrtFree(Locator);
    R=Tool("spawn",Args("background"));uint64 Id=Number(R,"task_id");Out=xrtValueObjectGet(R,XRT_STR_LITERAL("stdout"));
    Flag("spawn_returns_live_log_path",Id&&Last.bSuccess&&Text(Out,"path")[0]&&!Bool(R,"output_complete"));
    xrtValueObjectSetNew(Report,XRT_STR_LITERAL("spawn_stdout"),xrtValueString(xrtStrView(Text(Out,"native_path"))));xrtValueRelease(R);
    xrtSleep(1500u);Flag("finished_without_poll_releases_owner",Balanced());
    R=Tool(getenv("MDO_OUTPUT_STATUS_TOOL"),IdArgs(Id));Flag("status_after_uncollected_exit",R&&Bool(R,"finished")&&Bool(R,"output_complete")&&!strcmp(Text(R,"state"),"succeeded"));
    Out=xrtValueObjectGet(R,XRT_STR_LITERAL("stdout"));Flag("status_has_no_output_body",!xrtValueObjectGet(Out,XRT_STR_LITERAL("text")));xrtValueRelease(R);
    R=Tool("poll",IdArgs(Id));Flag("historical_poll_alias_is_status_only",R&&Last.bSuccess&&Bool(R,"finished")&&!xrtValueObjectGet(xrtValueObjectGet(R,XRT_STR_LITERAL("stdout")),XRT_STR_LITERAL("text")));xrtValueRelease(R);
    xwork_task_output O={0};bool Read=xworkRuntimeReadTaskOutput(Runtime,Id,0,0,0,1024,&O,&Error);
    Flag("sdk_read_starts_at_zero_after_status",Read&&O.iStdoutSize>=16u&&!memcmp(O.pStdout,"MDO_STDOUT_BEGIN",16u));xworkTaskOutputUnit(&O);
    Flag("wait_confirms_finished",WaitFor(Id));Read=xworkRuntimeReadTaskOutput(Runtime,Id,0,0,0,1024,&O,&Error);
    Flag("wait_does_not_consume_output",Read&&O.iStdoutSize>=16u&&!memcmp(O.pStdout,"MDO_STDOUT_BEGIN",16u));xworkTaskOutputUnit(&O);
    xwork_scheduled_task_config Scheduled;xworkScheduledTaskConfigInit(&Scheduled);Scheduled.sScheduleId="output-report-fixture";Scheduled.uScheduleGeneration=1;Scheduled.sLabel="Synthetic completed report";
    uint64 ReportId=0;bool ScheduledOk=xworkRuntimeCreateScheduledTask(Runtime,&Scheduled,&ReportId,&Error)&&xworkRuntimeStartScheduledTask(Runtime,ReportId,&Error)&&xworkRuntimeFinishScheduledTask(Runtime,ReportId,XWORK_RESULT_OK,"final agent report",&Error);
    R=Tool("status",IdArgs(ReportId));Flag("nonprocess_status_preserves_final_report",ScheduledOk&&R&&Last.bSuccess&&!strcmp(Text(R,"result"),"final agent report"));xrtValueRelease(R);
    char LongReport[32769];memset(LongReport,'a',sizeof(LongReport)-1u);LongReport[sizeof(LongReport)-1u]=0;
    ScheduledOk=xworkRuntimeCreateScheduledTask(Runtime,&Scheduled,&ReportId,&Error)&&xworkRuntimeStartScheduledTask(Runtime,ReportId,&Error)&&xworkRuntimeFinishScheduledTask(Runtime,ReportId,XWORK_RESULT_OK,LongReport,&Error);
    R=Tool("status",IdArgs(ReportId));const xvalue* Kept=xrtValueObjectGet(xrtValueObjectGet(R,XRT_STR_LITERAL("result")),XRT_STR_LITERAL("stdout"));
    Flag("bounded_nonprocess_report_exposes_retained_range",ScheduledOk&&R&&Bool(R,"result_truncated")&&Number(R,"result_offset")==16384u&&Text(Kept,"path")[0]);xrtValueRelease(R);
    Flag("task_receipt_release",xworkRuntimeReleaseTask(Runtime,Id,&Error));
    R=Tool("spawn",Args("long"));Id=Number(R,"task_id");xrtValueRelease(R);P=IdArgs(Id);
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("mode"),xrtValueString(XRT_STR_LITERAL("kill_tree")));xrtValueObjectSetNew(P,XRT_STR_LITERAL("wait_ms"),xrtValueUInt(5000));
    R=Tool("stop",P);Flag("stop_retains_receipt_and_releases_owner",R&&Last.bSuccess&&Balanced()&&Bool(R,"finished"));xrtValueRelease(R);
    R=Tool(getenv("MDO_OUTPUT_STATUS_TOOL"),IdArgs(Id));Flag("stopped_task_still_queryable",R&&!strcmp(Text(R,"state"),"cancelled"));xrtValueRelease(R);
    R=Tool("spawn",Args("input"));Id=Number(R,"task_id");xrtValueRelease(R);P=IdArgs(Id);
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("input"),xrtValueString(XRT_STR_LITERAL("qa-input")));
    xrtValueObjectSetNew(P,XRT_STR_LITERAL("close_stdin"),xrtValueBool(true));R=Tool("stdin",P);
    bool Input=Last.bSuccess;xrtValueRelease(R);Input=Input&&WaitFor(Id);
    Read=xworkRuntimeReadTaskOutput(Runtime,Id,0,0,0,1024,&O,&Error);
    Flag("stdin_survives_automatic_handle_release",Input&&Read&&O.iStdoutSize>=8u&&!memcmp(O.pStdout,"qa-input",8u)&&Balanced());xworkTaskOutputUnit(&O);
    R=Tool("spawn",Args("descendant"));Id=Number(R,"task_id");xrtValueRelease(R);
    Flag("exited_leader_drains_descendant_pipes",Id&&WaitFor(Id)&&Balanced());
    xwork_mcp_stdio_config MC;xworkMcpStdioConfigInit(&MC);const char* MArgs[]={"-u",getenv("MDO_OUTPUT_CHILD"),"mcp"};
    MC.sServerName="output-parser-fixture";MC.sProgram=getenv("MDO_OUTPUT_PYTHON");MC.psArguments=MArgs;MC.iArgumentCount=3;
    MC.uStartupTimeoutMs=3000;MC.uRequestTimeoutMs=3000;xwork_mcp_client* Client=xworkMcpClientCreate(&MC,&Error);
    xwork_tool_output MO;xworkToolOutputInit(&MO);
    bool Mcp=Client&&xworkMcpClientConnect(Client,&Error)&&xworkMcpClientCallTool(Client,"echo","{}",NULL,XLLM_DEADLINE_NEVER,&MO,&Error)==XWORK_RESULT_OK&&MO.bSuccess&&MO.sContent&&strstr(MO.sContent,"mcp-ok");
    Flag("mcp_transport_capture_is_preserved",Mcp);xworkToolOutputUnit(&MO);if(Client)xworkMcpClientDestroy(Client);
    R=Tool("spawn",Args("long"));Flag("running_task_before_runtime_shutdown",R&&Last.bSuccess&&Number(R,"task_id"));xrtValueRelease(R);
done:
    if(Executor.pUserData)xworkExecutorUnbind(&Executor);if(Agent)xworkAgentDestroy(Agent);if(Runtime)xworkRuntimeRelease(Runtime);
    Flag("runtime_shutdown_drains_process_and_releases_owner",Balanced());
    if(Definition)xworkAgentDefinitionRelease(Definition);if(Store)xworkArtifactStoreRelease(Store);if(Root)xrtRootClose(Root);if(S)xllmSessionDestroy(S);
    xrtValueObjectSetNew(Report,XRT_STR_LITERAL("checks"),Checks);xrtValueObjectSetNew(Report,XRT_STR_LITERAL("error"),xrtValueString(xrtStrView(Error.sMessage)));
    char* Json=xrtJsonStringify(Report,false,NULL);if(Json){printf("process_output_report=%s\n",Json);xrtFree(Json);}xrtValueRelease(Report);
    xrtMutexDestroy(CounterLock);printf("probe_done=1\n");
}
