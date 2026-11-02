"""Real Cua schemas and read-only invocation, plus recovered-context refresh."""
import json
import contextlib
import io
from pathlib import Path
import manual_mcp_computer_use_acceptance as fixture

EXTRA = r'''
 MdoAgentOwner Owner={0};Owner.Workspace=(char*)SdkField(P,"root");snprintf(Owner.ProductProject,sizeof(Owner.ProductProject),"default");
 MdoAgentInstance Instance={0};Instance.Definition.Id="qa-mcp-visibility";Instance.Definition.AllowedEffects=MDO_TOOL_EFFECT_ALL;
 Instance.LegacyPrompt="PINNED-HISTORY-MARKER";Instance.Configuration=xrtValueObject();
 xwork_agent_extension_context Native={0};Native.pAgent=Agent;Native.pSession=Session;
 xvalue* Plan=NULL;bool Collected=Visible&&MdoAgentsContextCollect(&Owner,&Instance,&Instance.Definition,&Native,&Plan,&E);
 char* Prompt=Collected?MdoAgentsContextRender(Plan,"system",&E):NULL;
 bool Advertised=Prompt&&strstr(Prompt,"PINNED-HISTORY-MARKER")&&strstr(Prompt,"Computer Use")&&strstr(Prompt,"\"state\":\"ready\"")&&strstr(Prompt,"list_windows")&&strlen(Prompt)<40u*1024u;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("recovered_history_receives_live_capabilities_and_instructions"),xrtValueBool(Advertised));
 char* Base=MdoAgentsLegacyPrompt(Prompt);char* Again=MdoAgentsLegacyPrompt(Base);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("restart_does_not_accumulate_capability_blocks"),xrtValueBool(Base&&Again&&!strcmp(Base,Instance.LegacyPrompt)&&!strcmp(Base,Again)));xrtFree(Base);xrtFree(Again);
 bool Frozen=false;Instance.SourcePlan=Plan;MdoAgentsFrozenSource(&Instance,Plan,"mcp.instructions",NULL,&Frozen);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("capability_source_refreshes_each_request"),xrtValueBool(!Frozen));Instance.SourcePlan=NULL;
 if(Executor.pUserData)xworkExecutorUnbind(&Executor);memset(&Executor,0,sizeof(Executor));
 xwork_mcp_server_info WarmBefore,WarmAfter;xworkMcpServerInfoInit(&WarmBefore);xworkMcpServerInfoInit(&WarmAfter);
 char WarmId[97];McpDefaultId("mcp:mdo.computer-use",WarmId);xworkRuntimeMcpServerGetInfo(Runtime,WarmId,&WarmBefore);
 MdoMcpPreloadAgentTools(Agent,Owner.Workspace,Owner.ProductProject);
 MdoMcpPreloadAgentTools(Agent,Owner.Workspace,Owner.ProductProject);
 xworkRuntimeMcpServerGetInfo(Runtime,WarmId,&WarmAfter);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("warmup_metadata_requests"),xrtValueUInt(WarmAfter.uRequestsCompleted-WarmBefore.uRequestsCompleted));
 xwork_tool_catalog* Tools=xworkAgentToolCatalogSnapshot(Agent);size_t Loaded=0,SchemaBytes=0;char* WindowTool=NULL;
 for(size_t I=0;I<xworkToolCatalogCount(Tools);I++){xwork_tool_info T={0};if(xworkToolCatalogToolAt(Tools,I,&T)&&!strncmp(T.sName,"mcp__",5)){
  Loaded++;SchemaBytes+=T.sParametersJson?strlen(T.sParametersJson):0;if(strstr(T.sName,"list_windows"))WindowTool=xrtStrDup(T.sName);
 }}xworkToolCatalogRelease(Tools);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("core_schemas_preload_without_model_discovery"),xrtValueBool(Loaded>0&&Loaded<=4&&WindowTool));
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("preloaded_schema_count"),xrtValueUInt(Loaded));xrtValueObjectSetNew(R,XRT_STR_LITERAL("preloaded_schema_bytes"),xrtValueUInt(SchemaBytes));
 xvalue* Probe=xrtValueObject(),*ProbeTools=xrtValueArray();Tools=xworkAgentToolCatalogSnapshot(Agent);
 for(size_t I=0;I<xworkToolCatalogCount(Tools);I++){xwork_tool_info T={0};if(xworkToolCatalogToolAt(Tools,I,&T)){
  xvalue* Tool=xrtValueObject();SdkPut(Tool,"name",T.sName);SdkPut(Tool,"description",T.sDescription);
  xvalue* Parameters=T.sParametersJson?xrtJsonParse(xrtStrView(T.sParametersJson)):xrtValueObject();
  xrtValueObjectSetNew(Tool,XRT_STR_LITERAL("parameters"),Parameters);xrtValueObjectSetNew(Tool,XRT_STR_LITERAL("strict"),xrtValueBool(T.bStrict));xrtValueArrayAppendNew(ProbeTools,Tool);
 }}xworkToolCatalogRelease(Tools);SdkPut(Probe,"system",Prompt);xrtValueObjectSetNew(Probe,XRT_STR_LITERAL("tools"),ProbeTools);xrtValueObjectSetNew(R,XRT_STR_LITERAL("model_probe"),Probe);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("repeated_warmup_keeps_the_core_schema_budget"),xrtValueBool(Loaded<=4&&SchemaBytes<=24u*1024u));
 bool Invoked=WindowTool&&xworkExecutorBind(&Executor,Agent,&E);xllm_executor_result Operation={0};
 Call.sId="qa-real-cua-read";Call.sName=WindowTool;Call.sArgumentsJson="{\"app\":\"Calculator\"}";
 Invoked=Invoked&&Executor.pExecute(Executor.pUserData,&Call,&Context,&Operation)&&Operation.bSuccess;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("preloaded_tool_executes_real_cua_read_only"),xrtValueBool(Invoked));
 xrtValueRelease(Instance.Configuration);Instance.Configuration=SdkParse("{\"context\":{\"mcp.instructions\":false}}");xvalue* Hidden=NULL;
 bool OptOut=MdoAgentsContextCollect(&Owner,&Instance,&Instance.Definition,&Native,&Hidden,&E);char* OptText=OptOut?MdoAgentsContextRender(Hidden,"system",&E):NULL;
 OptOut=OptText&&!strcmp(OptText,Instance.LegacyPrompt);xrtFree(OptText);xrtValueRelease(Hidden);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("role_can_explicitly_disable_mcp_context"),xrtValueBool(OptOut));
 xrtValueRelease(Instance.Configuration);Instance.Configuration=SdkParse("{\"context\":{\"skills.preload\":false,\"user_instructions\":false,\"reply_language\":false,\"external_tools\":false,\"skills.discovery\":false,\"knowledge\":false,\"memory.global\":false,\"memory.project\":false,\"file_access\":false}}");
 cstr Pinned=Instance.LegacyPrompt;Instance.LegacyPrompt=NULL;Instance.Definition.SystemPrompt="MODERN-ROLE-MARKER";
 bool Modern=MdoConfigInit();Hidden=NULL;Modern=Modern&&MdoAgentsContextCollect(&Owner,&Instance,&Instance.Definition,&Native,&Hidden,&E);
 char* ModernText=Modern?MdoAgentsContextRender(Hidden,"system",&E):NULL;Modern=ModernText&&strstr(ModernText,"MODERN-ROLE-MARKER")&&strstr(ModernText,"Computer Use");
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("new_roles_enable_live_mcp_instructions_by_default"),xrtValueBool(Modern));xrtFree(ModernText);xrtValueRelease(Hidden);
 const char* ReadOnlyTools[]={"read"};Instance.Definition.Tools=ReadOnlyTools;Instance.Definition.ToolCount=1;Hidden=NULL;
 bool Limited=MdoAgentsContextCollect(&Owner,&Instance,&Instance.Definition,&Native,&Hidden,&E);ModernText=Limited?MdoAgentsContextRender(Hidden,"system",&E):NULL;
 Limited=ModernText&&!strstr(ModernText,"Computer Use");xrtValueObjectSetNew(R,XRT_STR_LITERAL("roles_without_mcp_discovery_do_not_advertise_mcp"),xrtValueBool(Limited));xrtFree(ModernText);xrtValueRelease(Hidden);
 Instance.Definition.Tools=NULL;Instance.Definition.ToolCount=0;Instance.LegacyPrompt=(char*)Pinned;MdoConfigUnit();
 xrtValueRelease(Instance.Configuration);Instance.Configuration=xrtValueObject();
 char AutoId[97];McpDefaultId("mcp:mdo.computer-use",AutoId);
 bool Disabled=MdoMcpManagerSetEnabled(AutoId,false,&E);Hidden=NULL;Disabled=Disabled&&MdoAgentsContextCollect(&Owner,&Instance,&Instance.Definition,&Native,&Hidden,&E);
 char* DisabledText=Disabled?MdoAgentsContextRender(Hidden,"system",&E):NULL;
 Disabled=DisabledText&&!strstr(DisabledText,AutoId);xrtFree(DisabledText);xrtValueRelease(Hidden);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("next_request_drops_disabled_server_summary"),xrtValueBool(Disabled));
 xllm_executor_result Blocked={0};bool Refused=WindowTool&&Executor.pExecute(Executor.pUserData,&Call,&Context,&Blocked)&&!Blocked.bSuccess;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("disabled_server_blocks_preloaded_tool_invocation"),xrtValueBool(Refused));xrtFree(WindowTool);
 xrtFree(Prompt);xrtValueRelease(Plan);xrtValueRelease(Instance.Configuration);
'''

def main():
    needle=' if(Executor.pUserData)xworkExecutorUnbind(&Executor);if(Agent)xworkAgentDestroy(Agent);'
    assert fixture.CONTRACT.count(needle)==1
    fixture.CONTRACT=fixture.CONTRACT.replace(needle,EXTRA+needle)
    # Keep the full public schema snapshot in the local evidence file. Normal
    # test output should not dump thousands of lines of tool parameters.
    with contextlib.redirect_stdout(io.StringIO()):
        fixture.main()
    result=json.loads((Path(fixture.ROOT)/'.build/mcp-auto-20261102/cua-auto-result.json').read_text(encoding='utf-8'))
    result=result.get('result',result)
    for key in ('recovered_history_receives_live_capabilities_and_instructions','restart_does_not_accumulate_capability_blocks',
                'capability_source_refreshes_each_request','core_schemas_preload_without_model_discovery',
                'preloaded_tool_executes_real_cua_read_only','role_can_explicitly_disable_mcp_context','next_request_drops_disabled_server_summary',
                'new_roles_enable_live_mcp_instructions_by_default','roles_without_mcp_discovery_do_not_advertise_mcp',
                'repeated_warmup_keeps_the_core_schema_budget','disabled_server_blocks_preloaded_tool_invocation'):
        assert result.get(key) is True,(key,result)
    print('PASS 11 real Cua capability and recovered-context contracts; no model call or desktop mutation')

if __name__=='__main__':main()
