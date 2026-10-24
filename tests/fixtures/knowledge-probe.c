/* Test-only route in a copied app. No provider/model connection is made. */
static bool KnowledgeProbeCall(xwork_agent* Agent,cstr Name,cstr Arguments,xvalue* Data,cstr Key)
{
    xllm_executor Executor={0};xllm_executor_ctx Context={0};xllm_executor_result Result={0};xllm_tool_call Call={0};xwork_error Error;
    if(!xworkExecutorBind(&Executor,Agent,&Error))return false;
    Context.uRound=1;Context.uDeadline=MdoDeadlineAfterUs(5000000);Call.sId=(char*)"knowledge-probe";Call.sName=(char*)Name;Call.sArgumentsJson=(char*)Arguments;
    bool Ran=Executor.pExecute&&Executor.pExecute(Executor.pUserData,&Call,&Context,&Result);bool Ok=Ran&&MdoApiValueSetBool(Data,Key,Result.bSuccess);
    if(!strcmp(Key,"read"))Ok=Ok&&MdoApiValueSetString(Data,"read_text",Result.sContent?Result.sContent:"");
    xworkExecutorUnbind(&Executor);return Ok;
}
static bool KnowledgeProbeRoute(MdoApiContext* C)
{
    xvalue* Data=xrtValueObject();char* Prompt=xrtStrDup("Test base prompt");bool Ok=Data&&Prompt&&MdoKnowledgeAppendPrompt(&Prompt)&&MdoKnowledgeAppendPrompt(&Prompt)&&MdoApiValueSetString(Data,"prompt",Prompt);xrtFree(Prompt);
    xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);xwork_error E;xwork_runtime* Runtime=xworkRuntimeCreate(&RC,&E);
    xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);DC.sId="knowledge.probe";DC.eApprovalMode=XWORK_APPROVAL_READ_ONLY;DC.bRegisterBuiltinTools=true;xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&E);
    xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_session* Session=xllmSessionCreate(&SC,NULL);
    xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=Session;AO.sWorkspaceRoot=".";xwork_agent* Agent=Runtime&&Definition&&Session?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&E):NULL;
    Ok=Ok&&Agent&&MdoKnowledgeConfigureFileTools(Agent,&E)&&
        KnowledgeProbeCall(Agent,"read","{\"path\":\"@knowledge/local-reference/topic.md\"}",Data,"read")&&
        KnowledgeProbeCall(Agent,"grep","{\"path\":\"@knowledge/local-reference\",\"pattern\":\"edited\"}",Data,"grep")&&
        KnowledgeProbeCall(Agent,"glob","{\"path\":\"@knowledge/local-reference\",\"pattern\":\"*.md\"}",Data,"glob")&&
        KnowledgeProbeCall(Agent,"read","{\"path\":\"@knowledge/../config/defaults.json\"}",Data,"traversal")&&
        KnowledgeProbeCall(Agent,"read","{\"path\":\"../outside.txt\"}",Data,"external");
    xworkAgentDestroy(Agent);xllmSessionDestroy(Session);xworkAgentDefinitionRelease(Definition);xworkRuntimeRelease(Runtime);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,500,"knowledge_probe_failed",E.sMessage,NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
