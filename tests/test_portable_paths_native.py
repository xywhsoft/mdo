"""Current product C: Home-relative persistence and temporary pre-1.0 recovery.

Uses an existing QA host; no model calls, copied runtimes or user Home writes.
"""
import argparse
import json
from pathlib import Path
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
static void QaPathResult(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
static bool QaPathEqual(char* A,cstr B){bool Ok=A&&B&&!strcmp(A,B);xrtFree(A);return Ok;}
static xvalue* QaPathRead(cstr Path){xfile F=MdoHomeOpenRead(Path);char* Text=NULL;size_t N=0;bool Ok=F&&MdoHomeReadFileBounded(F,1048576,&Text,&N);xrtClose(F);xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxInputBytes=1048576;C.MaxValues=50000;C.MaxContainerItems=4096;xvalue* V=Ok?xrtJsonRead(xrtStrViewN(Text,N),&C):NULL;xrtFree(Text);return V;}
static bool QaPathWrite(cstr Path,xvalue* V){char* Text=xrtJsonStringify(V,false,NULL);bool Ok=Text&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);return Ok;}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();cstr Root=SdkField(P,"root"),App=SdkField(P,"app");
 if(!QaProductHome(Root,App,V)){QaPathResult(R,"home",false);return R;}
 char* File=MdoHomeExternalPath("1.txt"),*Workspace=MdoHomeExternalPath("workspace");
 char* Parent=xrtPathParent(Root);char* Old=xrtPathJoin(Parent,"missing/mdo-home/workspace"),*Other=xrtPathJoin(Root,"original/mdo-home/keep.txt");xrtFree(Parent);
 QaPathResult(R,"relative_paths_are_home_based",QaPathEqual(MdoPathResolve("1.txt"),File));
 QaPathResult(R,"store_paths_are_relative",QaPathEqual(MdoPathStore(File),"1.txt"));
 QaPathResult(R,"home_root_roundtrips",QaPathEqual(MdoPathStore(Root),".")&&QaPathEqual(MdoPathResolve("."),Root));
 QaPathResult(R,"missing_old_home_rebases",QaPathEqual(MdoPathResolve(Old),Workspace));
 QaPathResult(R,"legacy_store_converges_to_relative",QaPathEqual(MdoPathStore(Old),"workspace"));
 QaPathResult(R,"existing_absolute_path_wins",QaPathEqual(MdoPathResolve(Other),Other));
 char* Similar=xrtPathJoin(Root,"missing/mdo-home-backup/workspace");char* Expected=xrtPathAbs(Similar);
 QaPathResult(R,"similar_directory_is_not_rebased",QaPathEqual(MdoPathResolve(Similar),Expected));xrtFree(Expected);xrtFree(Similar);
 char* Bad=xrtFormat("%s/missing/mdo-home/../escape.txt",Root);char* Invalid=MdoPathResolve(Bad);
 QaPathResult(R,"legacy_traversal_is_rejected",!Invalid);xrtFree(Invalid);xrtFree(Bad);xrtClearError();
 Invalid=MdoPathResolve("../escape.txt");QaPathResult(R,"relative_traversal_is_rejected",!Invalid);xrtFree(Invalid);xrtClearError();
 cstr External=SdkField(P,"external");QaPathResult(R,"external_project_binding_is_preserved",QaPathEqual(MdoPathStore(External),External));
 MdoSessionInfo S={0};S.Size=sizeof(S);S.Revision=7;S.CreatedAt=S.UpdatedAt=S.LastActivatedAt=MdoUtcUs();
 S.Status=S.PreviousStatus=MDO_SESSION_ACTIVE;S.Protocol=MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;S.MaxOutputTokens=1024;
 strcpy(S.Id,"0123456789abcdef0123456789abcdef");strcpy(S.ProjectId,"default");strcpy(S.AgentId,"mdo.default");strcpy(S.ModelId,"fixture");strcpy(S.PermissionProfile,"full-access");
 snprintf(S.WorkspaceRoot,sizeof(S.WorkspaceRoot),"%s",Workspace);
 size_t N=0;char* Text=MdoSessionsInternalMetaJson(&S,&N);xvalue* Meta=Text?xrtJsonParse(xrtStrViewN(Text,N)):NULL;xrtFree(Text);
 QaPathResult(R,"session_serializer_uses_relative_workspace",Meta&&SdkField(Meta,"workspace_root")&&!strcmp(SdkField(Meta,"workspace_root"),"workspace"));
 MdoSessionInfo Restored={0};Text=Meta?xrtJsonStringify(Meta,false,NULL):NULL;
 QaPathResult(R,"relative_session_recovers_native_workspace",Text&&MdoSessionsInternalMetaParse(S.ProjectId,S.Id,xrtStrView(Text),&Restored)&&!strcmp(Restored.WorkspaceRoot,Workspace)&&Restored.Revision==7);xrtFree(Text);
 char* ProjectFile=xrtPathJoin(Workspace,"1.txt");QaPathResult(R,"process_arguments_remain_workspace_relative",QaPathEqual(SdkProcessPath(Workspace,"1.txt"),ProjectFile));xrtFree(ProjectFile);
 SdkPut(Meta,"workspace_root",Old);
 Text=xrtJsonStringify(Meta,false,NULL);bool Recovered=Text&&MdoSessionsInternalMetaParse(S.ProjectId,S.Id,xrtStrView(Text),&Restored);xrtFree(Text);
 xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_error LE={0};xllm_session* ModelSession=xllmSessionCreate(&SC,&LE);
 xwork_runtime_config RC;xworkRuntimeConfigInit(&RC);xwork_error WE={0};xwork_runtime* Runtime=xworkRuntimeCreate(&RC,&WE);
 xwork_agent_definition_config DC;xworkAgentDefinitionConfigInit(&DC);DC.sId="qa-portable";DC.sModel="fixture-no-network";DC.bRegisterBuiltinTools=true;DC.eApprovalMode=XWORK_APPROVAL_AUTO;
 xwork_agent_definition* Definition=xworkAgentDefinitionCreate(&DC,&WE);xwork_agent_options AO;xworkAgentOptionsInit(&AO);AO.pSession=ModelSession;AO.sWorkspaceRoot=Restored.WorkspaceRoot;
 xwork_agent* Agent=Recovered&&Runtime&&Definition&&ModelSession?xworkAgentCreateWithRuntime(Runtime,Definition,&AO,&WE):NULL;
 xllm_executor Executor={0};xllm_tool_call Call={0};Call.sId="portable-read";Call.sName="read";Call.sArgumentsJson="{\"path\":\"project-file.txt\"}";
 xllm_executor_ctx EC={0};EC.uDeadline=XLLM_DEADLINE_NEVER;xllm_executor_result Output={0};
 bool Readable=Agent&&xworkExecutorBind(&Executor,Agent,&WE)&&Executor.pExecute(Executor.pUserData,&Call,&EC,&Output)&&Output.bSuccess&&Output.sContent&&strstr(Output.sContent,"portable workspace");
 QaPathResult(R,"recovered_workspace_opens_and_reads_without_a_model_call",Readable);
 if(Executor.pUserData)xworkExecutorUnbind(&Executor);if(Agent)xworkAgentDestroy(Agent);xworkAgentDefinitionRelease(Definition);if(Runtime)xworkRuntimeRelease(Runtime);if(ModelSession)xllmSessionDestroy(ModelSession);
 bool Written=QaPathWrite("sessions/default/0123456789abcdef0123456789abcdef/meta.json",Meta)&&QaPathWrite("sessions/default/0123456789abcdef0123456789abcdef/meta.json.bak",Meta);
 cstr Ledger="{\"user_text\":\"preserve original history\"}\n";
 Written=Written&&MdoHomeAtomicWrite("sessions/default/0123456789abcdef0123456789abcdef/snapshot.json",Ledger,strlen(Ledger),false);
 xvalue* Project=xrtValueObject();SdkPut(Project,"workspace_root",Old);SdkPutUInt(Project,"revision",9);Written=Written&&QaPathWrite("projects/owned.json",Project);
 SdkPut(Project,"workspace_root",External);Written=Written&&QaPathWrite("projects/external.json",Project);xrtValueRelease(Project);
 bool Migrated=Written&&MdoPathMigrate();xvalue* Read=QaPathRead("sessions/default/0123456789abcdef0123456789abcdef/meta.json");
 QaPathResult(R,"startup_migrates_existing_metadata",Migrated&&Read&&!strcmp(SdkField(Read,"workspace_root"),"workspace"));
 uint64 Rev=0;SdkUInt(SdkGet(Read,"revision"),&Rev);QaPathResult(R,"migration_preserves_metadata_revision",Rev==7);xrtValueRelease(Read);
 Read=QaPathRead("sessions/default/0123456789abcdef0123456789abcdef/meta.json.bak");QaPathResult(R,"startup_migrates_backups",Read&&!strcmp(SdkField(Read,"workspace_root"),"workspace"));xrtValueRelease(Read);
 Read=QaPathRead("projects/external.json");QaPathResult(R,"migration_keeps_external_projects",Read&&!strcmp(SdkField(Read,"workspace_root"),External));xrtValueRelease(Read);
 QaPathResult(R,"startup_is_idempotent",MdoPathMigrate());
 xfile F=MdoHomeOpenRead("sessions/default/0123456789abcdef0123456789abcdef/snapshot.json");Text=NULL;N=0;bool Unchanged=F&&MdoHomeReadFileBounded(F,1024,&Text,&N)&&N==strlen(Ledger)&&!memcmp(Text,Ledger,N);xrtClose(F);xrtFree(Text);
 QaPathResult(R,"migration_does_not_rewrite_model_history",Unchanged);
 char Error[MDO_MCP_ERROR_LIMIT]={0};xvalue* Cwd=xrtValueString(XRT_STR_LITERAL("workspace"));char* Directory=NULL;
 bool Mcp=MdoMcpParseWorkingDirectory(Cwd,&Directory,Error)&&Directory&&!strcmp(Directory,Workspace);xrtFree(Directory);xrtValueRelease(Cwd);
 QaPathResult(R,"mcp_working_directory_is_home_relative",Mcp);
 xvalue* Template=SdkParse("{\"inputs\":[{\"id\":\"folder\",\"type\":\"directory\",\"required\":true}]}");xvalue* Values=xrtValueObject();SdkPut(Values,"folder",Old);
 xvalue* Input=McpDirectoryInput(Template,Values,"folder");xstrview NativeInput={0};
 QaPathResult(R,"mcp_typed_path_rebases_before_launch",xrtValueGetString(Input,&NativeInput)&&!strcmp(NativeInput.Data,Workspace));xrtValueRelease(Input);xrtValueRelease(Values);xrtValueRelease(Template);
 xvalue* Intent=xrtValueObject();SdkPut(Intent,"id","member");SdkPut(Intent,"agent_id","mdo.default");SdkPut(Intent,"model_id","fixture");SdkPut(Intent,"permission_profile","balanced");SdkPut(Intent,"workspace",Old);SdkPut(Intent,"parent_session",S.Id);SdkPut(Intent,"owner","qa");
 char BeforeHash[65],AfterHash[65];bool Hashed=SdkInstanceIntentHash(Intent,BeforeHash);SdkPut(Intent,"workspace",Workspace);Hashed=Hashed&&SdkInstanceIntentHash(Intent,AfterHash);
 QaPathResult(R,"member_creation_identity_survives_home_move",Hashed&&!strcmp(BeforeHash,AfterHash));
 xvalue* State=xrtValueObject(),*Records=xrtValueObject(),*Record=xrtValueObject(),*Opaque=xrtValueObject(),*OpaqueValue=xrtValueObject();
 SdkPut(Intent,"workspace",Old);xrtValueObjectSet(Record,XRT_STR_LITERAL("value"),Intent);xrtValueObjectSet(Records,XRT_STR_LITERAL("sys.instance.member"),Record);
 SdkPut(OpaqueValue,"workspace",Old);xrtValueObjectSet(Opaque,XRT_STR_LITERAL("value"),OpaqueValue);xrtValueObjectSet(Records,XRT_STR_LITERAL("third-party"),Opaque);xrtValueObjectSet(State,XRT_STR_LITERAL("records"),Records);
 bool Changed=false;bool Upgraded=SdkStateUpgradePaths(State,&Changed);
 QaPathResult(R,"host_member_state_is_upgraded",Upgraded&&Changed&&!strcmp(SdkField(Intent,"workspace"),"workspace"));
 QaPathResult(R,"third_party_state_is_opaque",!strcmp(SdkField(OpaqueValue,"workspace"),Old));
 Changed=false;QaPathResult(R,"member_upgrade_is_idempotent",SdkStateUpgradePaths(State,&Changed)&&!Changed);
 xrtValueRelease(Intent);xrtValueRelease(Record);xrtValueRelease(Records);xrtValueRelease(Opaque);xrtValueRelease(OpaqueValue);xrtValueRelease(State);
 xvalue* Task=xrtValueObject();SdkPut(Task,"id","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");SdkPut(Task,"target_workspace",Workspace);SdkPut(Task,"destination_workspace",Workspace);
 bool TaskSaved=MdoDtTaskSave(true,Task);xvalue* Stored=QaPathRead("data/device-transfers/incoming/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/task.json");
 QaPathResult(R,"transfer_task_paths_persist_relatively",TaskSaved&&Stored&&!strcmp(SdkField(Stored,"target_workspace"),"workspace"));xrtValueRelease(Stored);
 Stored=MdoDtTaskRead(true,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");QaPathResult(R,"transfer_task_paths_resolve_on_read",Stored&&!strcmp(SdkField(Stored,"destination_workspace"),Workspace));xrtValueRelease(Stored);xrtValueRelease(Task);
 xvalue* Terminal=SdkParse("{\"id\":\"pty-qa\",\"owner\":\"qa\",\"key\":\"qa\",\"request_hash\":\"qa\",\"state\":\"ended\",\"context\":{\"project\":\"default\",\"session\":\"\",\"run\":\"\"}}");
 SdkPut(Terminal,"cwd",Workspace);SdkPut(Terminal,"program",File);SdkPut((xvalue*)SdkGet(Terminal,"context"),"workspace",Workspace);SdkTerminals.Slots[0].Record=Terminal;
 bool TerminalSaved=SdkTerminalSave();Stored=QaPathRead("data/application/terminals.json");const xvalue* Row=xrtValueArrayGet(SdkGet(Stored,"terminals"),0);
 QaPathResult(R,"terminal_save_separates_native_and_durable_paths",TerminalSaved&&Row&&!strcmp(SdkField(Row,"cwd"),"workspace")&&!strcmp(SdkField(Terminal,"cwd"),Workspace));
 xrtValueRelease(Stored);SdkTerminals.Slots[0].Record=NULL;xrtValueRelease(Terminal);
 Text=xrtJsonStringify(Meta,false,NULL);MdoBackupOwnedFile Files[2]={0};strcpy(Files[0].Path,"meta.json");Files[0].Data=Text;Files[0].Bytes=strlen(Text);strcpy(Files[1].Path,"snapshot.json");
 MdoSessionBackup Backup={0};Backup.Files=Files;Backup.Count=2;MdoDecode Decode={0};Decode.Backup=&Backup;Decode.Manifest=xrtValueObject();
 SdkPut(Decode.Manifest,"project_id",S.ProjectId);SdkPut(Decode.Manifest,"session_id",S.Id);SdkPutUInt(Decode.Manifest,"revision",S.Revision);SdkPut(Decode.Manifest,"source_workspace",Old);
 QaPathResult(R,"backup_identity_accepts_relocated_workspace",MdoDecodeMeta(&Decode,false));
 const char Embedded[]="workspace\0untrusted-suffix";xrtValueObjectSetNew(Decode.Manifest,XRT_STR_LITERAL("source_workspace"),xrtValueString(xrtStrViewN(Embedded,sizeof(Embedded)-1)));
 QaPathResult(R,"backup_identity_rejects_embedded_nul",!MdoDecodeMeta(&Decode,false));xrtFree(Text);xrtValueRelease(Decode.Manifest);xrtClearError();
 xrtValueRelease(Meta);xrtFree(File);xrtFree(Workspace);xrtFree(Old);xrtFree(Other);MdoHomeUnit();return R;
}
'''


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--url', required=True)
    p.add_argument('--home', type=Path, required=True)
    p.add_argument('--project', default='default')
    args = p.parse_args()
    base = ROOT / '.build/portable-paths-native' / uuid.uuid4().hex[:10]
    fixture = base / 'mdo-home'
    (fixture / 'workspace').mkdir(parents=True)
    (fixture / 'workspace/project-file.txt').write_text('portable workspace', encoding='utf-8')
    (fixture / '1.txt').write_text('Home file', encoding='utf-8')
    original = fixture / 'original/mdo-home'
    original.mkdir(parents=True)
    (original / 'keep.txt').write_text('Existing original wins', encoding='utf-8')
    external = base / 'external-project'
    external.mkdir()
    # Inject only the owned external project fixture, never a real project.
    contract = CONTRACT.replace('cstr External=SdkField(P,"external");',
                                'cstr External=' + json.dumps(str(external)) + ';')
    result = product(args, fixture, contract, base / 'result.json')
    assert result and all(v is True for v in result.values()), result
    print('PASS', len(result), 'native portable path contracts; evidence:', base / 'result.json')


if __name__ == '__main__':
    main()
