#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>
#include <xllm-executor.h>
void ServiceInit(XS_HostInfo* Host){
 (void)Host;xwork_error Error;xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);
 xwork_runtime* Runtime=xworkRuntimeCreate(&RC,&Error);xwork_mcp_server_config MCP;xworkMcpServerConfigInit(&MCP);
 const char* Args[]={"QA_UTF8_PEER"};MCP.sServerId="utf8-peer";MCP.sSummary="Unicode";MCP.sProgram="python";MCP.psArguments=Args;MCP.iArgumentCount=1;MCP.bEnabled=true;MCP.sProtocolVersion="2025-11-25";
 if(!Runtime||!xworkRuntimeRegisterMcpServer(Runtime,&MCP,&Error))exit(2);
 xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);DC.sId="qa-discovery";DC.bRegisterBuiltinTools=false;DC.eApprovalMode=XWORK_APPROVAL_AUTO;
 xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&Error);xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_session* Session=xllmSessionCreate(&SC,NULL);
 xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=Session;AO.sWorkspaceRoot=".";
 xwork_agent* Agent=Definition&&Session?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&Error):NULL;
 xllm_executor Executor={0};xllm_executor_ctx Context={0};xllm_executor_result Result={0};xllm_tool_call Call={0};
 if(!Agent||!xworkAgentRegisterMcpDiscoveryTools(Agent,&Error)||!xworkExecutorBind(&Executor,Agent,&Error))exit(3);
 Call.sId="discovery";Call.sName="tools";Call.sArgumentsJson="{\"server\":\"utf8-peer\",\"limit\":100}";
 bool Ok=Executor.pExecute(Executor.pUserData,&Call,&Context,&Result)&&Result.bSuccess&&Result.sContent&&xrtUtf8Valid(xrtStrView(Result.sContent),NULL)&&strstr(Result.sContent,"tool=two")&&strstr(Result.sContent,"tool=three")&&strstr(Result.sContent,"tool=four");
 if(!Ok){printf("discovery_error=%s\n",Result.sContent?Result.sContent:"no result");exit(17);}
 xworkExecutorUnbind(&Executor);xworkAgentDestroy(Agent);xworkAgentDefinitionRelease(Definition);xllmSessionDestroy(Session);xworkRuntimeRelease(Runtime);puts("mcp_utf8_discovery=PASS");fflush(stdout);exit(0);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
