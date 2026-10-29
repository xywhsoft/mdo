"""Actual built-in exec/spawn resource ownership, without any model calls."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]
CONTRACT=r'''
static unsigned QaAcquires,QaReleases;static bool QaDeny;
static bool QaAcquire(void* User,void** Lease){(void)User;QaAcquires++;if(QaDeny){xrtSetErrorInfo(XERR_STATE,"fixture",1,"fixture resources unavailable");return false;}return MdoAgentsProcessAcquire(NULL,Lease);}
static void QaRelease(void* User,void* Lease){(void)User;QaReleases++;MdoAgentsProcessRelease(NULL,Lease);}
static bool QaTool(xllm_executor* Executor,cstr Name,cstr Arguments,xllm_executor_result* Out){static unsigned Serial;char Id[40];snprintf(Id,sizeof(Id),"qa-process-%u",++Serial);xllm_tool_call Call={0};Call.sId=Id;Call.sName=(char*)Name;Call.sArgumentsJson=(char*)Arguments;xllm_executor_ctx C={0};C.uDeadline=XLLM_DEADLINE_NEVER;return Executor->pExecute(Executor->pUserData,&Call,&C,Out);}
static void QaFlag(xvalue* R,cstr Key,bool Value){xrtValueObjectSetNew(R,xrtStrView(Key),xrtValueBool(Value));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Ok=Home&&MdoExtensionsInit()&&MdoCliInit(NULL);
 xllm_session_config SC;xllmSessionConfigInit(&SC);xllm_error LE={0};xllm_session* S=xllmSessionCreate(&SC,&LE);xwork_agent_config AC;xworkAgentConfigInit(&AC);AC.pSession=S;AC.sWorkspaceRoot=SdkField(P,"root");AC.sModel="fixture-no-network";AC.bAutoSaveSession=false;AC.bAllowArtifactWrites=true;AC.sArtifactDirectory="artifacts";AC.bRequireVerificationAfterWrite=false;AC.eApprovalMode=XWORK_APPROVAL_AUTO;
 xwork_error E={0};xwork_agent* Agent=Ok&&S?xworkAgentCreate(&AC,&E):NULL;xllm_executor Executor={0};Ok=Ok&&Agent;
 QaFlag(R,"rejects_unpaired_callbacks",Agent&&!xworkAgentSetProcessOwner(Agent,QaAcquire,NULL,NULL,&E));Ok=Ok&&xworkAgentSetProcessOwner(Agent,QaAcquire,QaRelease,NULL,&E)&&xworkExecutorBind(&Executor,Agent,&E);QaFlag(R,"agent_initialized",Ok);
 xllm_executor_result Out={0};bool Ran=Ok&&QaTool(&Executor,"exec","{\"argv\":[\"cmd.exe\",\"/D\",\"/C\",\"exit 0\"],\"timeout_ms\":5000}",&Out);QaFlag(R,"exec_releases_after_synchronous_tree",Ran&&Out.bSuccess&&QaAcquires==1&&QaReleases==1);
 Ran=Ok&&QaTool(&Executor,"exec","{\"argv\":[\"missing-directory-fixture-program.exe\"],\"timeout_ms\":500}",&Out);QaFlag(R,"failed_exec_releases_owner",Ran&&!Out.bSuccess&&QaAcquires==2&&QaReleases==2);
 Ran=Ok&&QaTool(&Executor,"spawn","{\"argv\":[\"missing-directory-fixture-program.exe\"]}",&Out);QaFlag(R,"failed_spawn_releases_owner",Ran&&!Out.bSuccess&&QaAcquires==3&&QaReleases==3);
 QaDeny=true;Ran=Ok&&QaTool(&Executor,"exec","{\"argv\":[\"cmd.exe\",\"/C\",\"exit 0\"]}",&Out);QaFlag(R,"resource_rejection_starts_no_process",Ran&&!Out.bSuccess&&QaAcquires==4&&QaReleases==3);QaDeny=false;
 Ran=Ok&&QaTool(&Executor,"spawn","{\"argv\":[\"cmd.exe\",\"/D\",\"/C\",\"ping -n 20 127.0.0.1 >nul\"]}",&Out);uint64 Task=0;if(Ran&&Out.sContent){cstr At=strchr(Out.sContent,'{');xvalue* Spawn=At?xrtJsonParse(xrtStrView(At)):NULL;SdkUInt(SdkGet(Spawn,"task_id"),&Task);xrtValueRelease(Spawn);}bool Held=Ran&&Out.bSuccess&&Task&&QaAcquires==5&&QaReleases==3;
 Held=Held&&MdoExtensionsSetEnabled("external-tools","fixture-tool",false,Error)&&MdoCliReload()&&MdoExtensionsInUse("external-tools","fixture-tool");xvalue* Removed=NULL;Held=Held&&!MdoExtensionsRemove("external-tools","fixture-tool",&Removed,Error);xrtValueRelease(Removed);QaFlag(R,"spawn_owns_retired_directory",Held);
 char Args[128];snprintf(Args,sizeof(Args),"{\"task_id\":%llu,\"mode\":\"kill\",\"release\":false}",(unsigned long long)Task);bool Stopped=Task&&QaTool(&Executor,"stop",Args,&Out)&&Out.bSuccess;
 snprintf(Args,sizeof(Args),"{\"task_ids\":[%llu],\"timeout_ms\":5000}",(unsigned long long)Task);Stopped=Stopped&&QaTool(&Executor,"wait",Args,&Out)&&Out.bSuccess;QaFlag(R,"stopped_and_released_task_returns_directory",Stopped&&QaReleases==4&&!MdoExtensionsInUse("external-tools","fixture-tool"));
 bool Began=Agent&&xworkAgentRunBegin(Agent,&E);QaFlag(R,"callbacks_cannot_change_during_run",Began&&!xworkAgentSetProcessOwner(Agent,NULL,NULL,NULL,&E));if(Began)xworkAgentRunEnd(Agent);
 if(Executor.pUserData)xworkExecutorUnbind(&Executor);if(Agent)xworkAgentDestroy(Agent);if(S)xllmSessionDestroy(S);SdkPut(R,"error",E.sMessage);MdoCliUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/agent-process-ownership-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);directory=fixture/'external-tools/fixture-tool';directory.mkdir(parents=True)
 (directory/'extension.json').write_text(json.dumps(dict(kind='external-tools',id='external-tool:qa.fixture-unused',name='Fixture',alias='fixture-tool',entry='tool.json',platforms=['windows-x86_64'])),encoding='utf-8');(directory/'tool.json').write_text(json.dumps(dict(program='program.bin',prompt='Not executed',command='fixture-unused')),encoding='utf-8');(directory/'program.bin').write_bytes(b'Inert directory fixture')
 try:
  result=product(a,fixture,CONTRACT,out/'result.json');assert len(result)>=8 and all(v is True for k,v in result.items() if k!='error'),result;print(json.dumps(result,ensure_ascii=False))
 finally:
  if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)
if __name__=='__main__':main()


