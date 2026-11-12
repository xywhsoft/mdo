"""Private C snapshots and XRT templates against the in-place product."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import time
from test_api_runtime import ROOT, free_port
from test_extension_settings_runtime import FORM
from test_conversation_groups_runtime import prepare_history_home

PROBE = r'''
#define ServiceInit ProductServiceInit
#include "generated/mdo_unity.c"
#undef ServiceInit
static unsigned Failed,Checks;
#define CHECK(X) do{++Checks;if(!(X)){++Failed;printf("FAIL %u: %s\n",__LINE__,#X);}}while(0)
static xvalue* Save(cstr Name){
 char Why[512]={0};xvalue* Before=MdoExtensionSettingsRead("skills","qa-settings",Why),*Input=xrtValueObject(),*Out=NULL;
 CHECK(Before!=NULL);
 MdoEsPut(Input,"revision",MdoEsGet(Before,"revision"));MdoEsPut(Input,"schema_digest",MdoEsGet(Before,"schema_digest"));
 xvalue* Values=xrtJsonParse(XRT_STR_LITERAL("{\"endpoint\":\"https://example.invalid\"}"));MdoExtPut(Values,"character_name",Name);MdoEsPut(Input,"values",Values);
 xvalue* Secrets=xrtJsonParse(XRT_STR_LITERAL("{\"api_key\":\"private-fixture-key\"}"));MdoEsPut(Input,"secrets",Secrets);
 CHECK(MdoExtensionSettingsSave("skills","qa-settings",Input,false,&Out,Why));
 xrtValueRelease(Before);xrtValueRelease(Input);xrtValueRelease(Values);xrtValueRelease(Secrets);return Out;
}
static void Configure(cstr Kind,cstr Directory,cstr Values,cstr Secrets){
 char Why[512]={0};xvalue* Before=MdoExtensionSettingsRead(Kind,Directory,Why),*Input=xrtValueObject(),*Out=NULL;
 CHECK(Before!=NULL);MdoEsPut(Input,"revision",MdoEsGet(Before,"revision"));MdoEsPut(Input,"schema_digest",MdoEsGet(Before,"schema_digest"));
 xvalue* V=xrtJsonParse(xrtStrView(Values));MdoEsPut(Input,"values",V);xrtValueRelease(V);
 if(Secrets){V=xrtJsonParse(xrtStrView(Secrets));MdoEsPut(Input,"secrets",V);xrtValueRelease(V);}
 CHECK(MdoExtensionSettingsSave(Kind,Directory,Input,false,&Out,Why));
 if(!Out)printf("configuration_error=%s\n",Why);
 xrtValueRelease(Out);xrtValueRelease(Input);xrtValueRelease(Before);
}
typedef struct Round {unsigned Calls,Run;} Round;
static xllm_result Model(void* Data,const xllm_request* Q,const xllm_stream_callbacks* Callbacks,xllm_response** Out,xllm_error* Error){
 (void)Callbacks;Round* S=Data;unsigned Call=++S->Calls;bool Name=false,Ready=false,Fragment=false,Custom=false,Rules=false;
 for(size_t I=0;I<Q->iMessageCount;I++){
  for(size_t J=0;J<Q->pMessages[I].iPartCount;J++){cstr Part=Q->pMessages[I].pParts[J].sText;
   if(Part&&strstr(Part,S->Run==1?"Your name is Mira":"Your name is Nova"))Name=true;
   if(Part)CHECK(!strstr(Part,"image-fixture-secret"));}
  cstr Text=Q->pMessages[I].sContent;
  if(Text){if(strstr(Text,S->Run==1?"Your name is Mira":"Your name is Nova"))Name=true;
   if(strstr(Text,S->Run==1?"Fragment Mira":"Fragment Nova"))Fragment=true;
   if(strstr(Text,S->Run==1?"Custom Mira":"Custom Nova"))Custom=true;
   if(strstr(Text,"Project rules fixture"))Rules=true;
   if(Q->pMessages[I].eRole==XLLM_ROLE_TOOL&&strstr(Text,S->Run==1?"configuration is ready":"disabled or incomplete"))Ready=true;
   CHECK(!strstr(Text,"image-fixture-secret"));}
 }
 CHECK(Name);CHECK(Fragment);CHECK(Custom);CHECK(Rules);if(Call>1)CHECK(Ready);
 if(S->Run==1&&Call==2){Configure("tools","image-api","{\"enabled\":false}",NULL);Configure("agents","roleplay","{\"character_name\":\"Nova\"}",NULL);Configure("plugins","prompt-plugin","{\"character_name\":\"Nova\"}",NULL);}
 bool Tool=Call<(S->Run==1?3u:2u);xllm_response* R=calloc(1,sizeof(*R));
 R->sContent=strdup(Tool?"":"finished");R->sModel=strdup("fixture");R->uHttpStatus=200;
 R->sFinishReason=strdup(Tool?"tool_calls":"stop");R->eFinish=Tool?XLLM_FINISH_TOOL_CALLS:XLLM_FINISH_STOP;
 if(Tool){R->iToolCallCount=1;R->pToolCalls=calloc(1,sizeof(*R->pToolCalls));char Id[32];snprintf(Id,sizeof(Id),"settings-%u-%u",S->Run,Call);
  R->pToolCalls[0].sId=strdup(Id);R->pToolCalls[0].sName=strdup("example_image_configuration");R->pToolCalls[0].sArgumentsJson=strdup("{}");}
 *Out=R;xllmErrorInit(Error);return XLLM_RESULT_OK;
}
static void ActualAgent(){
 Configure("tools","image-api","{\"api_url\":\"https://example.invalid\",\"model\":\"fixture\",\"enabled\":true}","{\"api_key\":\"image-fixture-secret\"}");
 Round State={0};MdoAgentSessionOptions O;MdoAgentSessionOptionsInit(&O);char* Workspace=xrtEnvGet("MDO_SETTINGS_WORKSPACE");
 O.WorkspaceRoot=Workspace;O.AgentId="agent.configurable-role";O.ModelId="history-fixture";O.ProjectId="default";O.PermissionProfile="full-access";
 O.OnModelComplete=Model;O.ModelUserData=&State;xwork_error E;
 MdoAgentSession* Session=MdoAgentSessionCreateWithRuntime(MdoBootstrapRuntime(),&O,&E);CHECK(Session!=NULL);
 if(!Session){printf("agent_error=%s\n",E.sMessage);xrtFree(Workspace);return;}
 for(unsigned N=1;N<=2;N++){
  State.Run=N;State.Calls=0;MdoAgentRunOptions Options;MdoAgentRunOptionsInit(&Options);Options.Prompt="Check the private image configuration";
  MdoAgentRun* Active=MdoAgentRunCreate(Session,&Options,&E);CHECK(Active&&MdoAgentRunStart(Active,&E));xwork_run_result Result={0};
  xwork_result Code=MdoAgentRunWait(Active,MdoDeadlineAfterUs(15000000),&Result,&E);
  CHECK(Code==XWORK_RESULT_OK&&State.Calls==(N==1?3u:2u));if(Code!=XWORK_RESULT_OK)printf("run_error=%s\n",E.sMessage);
  xworkRunResultUnit(&Result);MdoAgentRunDestroy(Active);
 }
 MdoAgentSessionRelease(Session);xrtFree(Workspace);
}
static void McpEnvironment(){
 Configure("mcp","qa-settings","{\"endpoint\":\"https://example.invalid\"}","{\"api_key\":\"mcp-fixture-secret\"}");
 char Why[512]={0};MdoMcpSource Source={0};Source.Directory=MdoExtensionsRead("mcp","qa-settings",Why);
 MdoMcpServerEntry Entry={0};Entry.Id=xrtStrDup("qa-settings");Entry.Transport=MDO_MCP_TRANSPORT_STDIO;Entry.Enabled=true;
 char* Before=xrtEnvGet("SERVICE_API_KEY");CHECK(McpDirectoryBindRuntime(&Entry,&Source));bool Found=false;
 for(size_t I=0;I<Entry.EnvironmentCount;I++)if(!strcmp(Entry.Environment[I].Name,"SERVICE_API_KEY"))Found=!strcmp(Entry.Environment[I].Value,"mcp-fixture-secret");
 CHECK(Found);char* After=xrtEnvGet("SERVICE_API_KEY");CHECK((!Before&&!After)||(Before&&After&&!strcmp(Before,After)));
 char* Json=xrtJsonStringify(Source.Directory,false,NULL);CHECK(Json&&!strstr(Json,"mcp-fixture-secret"));
 xrtFree(Json);xrtFree(Before);xrtFree(After);xrtValueRelease(Source.Directory);MdoMcpServerUnit(&Entry);
}
void ServiceInit(XS_HostInfo* Host){
 CHECK(MdoBootstrapInit(Host));char Why[512]={0};xvalue* Result=Save("Mira");xrtValueRelease(Result);
 xvalue* Public=MdoExtensionSettingsSnapshot("skill:qa.settings",false,Why);
 xvalue* Full=MdoExtensionSettingsSnapshot("skill:qa.settings",true,Why);
 CHECK(Public&&Full&&!MdoEsGet(Public,"api_key")&&!strcmp(MdoEsText(Full,"api_key"),"private-fixture-key"));
 char* Text=MdoExtensionSettingsRender("skill:qa.settings","role.md","Name: {{$settings.character_name}}",Full,Why);
 CHECK(Text&&!strcmp(Text,"Name: Mira"));xrtFree(Text);
 Text=MdoExtensionSettingsRender("skill:qa.settings","role.md","{{$settings.api_key}}",Full,Why);CHECK(!Text&&strstr(Why,"extension_template_invalid"));
 Text=MdoExtensionSettingsRender("skill:qa.settings","other.md","{{$settings.api_key}}",Full,Why);CHECK(Text&&!strcmp(Text,"{{$settings.api_key}}"));xrtFree(Text);
 Text=MdoExtensionSettingsRender("skill:other","role.md","x",Full,Why);CHECK(!Text);
 xvalue* Env=MdoExtensionSettingsEnvironment(Full,Why);CHECK(Env&&!strcmp(MdoEsText(Env,"SERVICE_API_KEY"),"private-fixture-key"));xrtValueRelease(Env);
 /* Invocation identity is host-bound; parameters cannot choose a neighbor. */
 mdo_sdk_context_v1 C;MdoApplicationInvocation Invocation={0};MdoApplicationContext(&C,&Invocation,"fixture","","","","","",0);
 C.ExtensionKind="skills";C.DirectoryId="qa-settings";C.ExtensionSettings=Full;
 mdo_error_v1 E={0};Result=NULL;CHECK(MdoApplicationCall(&C,"extension.settings.current",NULL,&Result,&E)==MDO_RESULT_OK);
 CHECK(Result&&!strcmp(MdoEsText(Result,"api_key"),"private-fixture-key"));xrtValueRelease(Result);Result=NULL;
 xvalue* Cross=xrtJsonParse(XRT_STR_LITERAL("{\"extension_id\":\"skill:other\"}"));
 CHECK(MdoApplicationCall(&C,"extension.settings.current",Cross,&Result,&E)!=MDO_RESULT_OK&&!Result);xrtValueRelease(Cross);
 C.ExtensionSettings=NULL;CHECK(MdoApplicationCall(&C,"extension.settings.current",NULL,&Result,&E)==MDO_RESULT_OK);
 CHECK(Result&&!MdoEsGet(Result,"api_key"));xrtValueRelease(Result);
 unsigned Marker; xwork_agent* Identity=(xwork_agent*)&Marker;CHECK(MdoModuleAgentBindContext(Identity,"default","fixture"));
 xvalue* Pinned=MdoModuleAgentSettings(Identity,0,1,"skill:qa.settings",Why);CHECK(Pinned!=NULL);
 Result=Save("Nova");xrtValueRelease(Result);
 xvalue* Same=MdoModuleAgentSettings(Identity,0,1,"skill:qa.settings",Why);CHECK(Same&&!strcmp(MdoEsText(Same,"character_name"),"Mira"));
 MdoExtPut(Same,"character_name","mutation");xrtValueRelease(Same);
 Same=MdoModuleAgentSettings(Identity,0,1,"skill:qa.settings",Why);CHECK(Same&&!strcmp(MdoEsText(Same,"character_name"),"Mira"));xrtValueRelease(Same);
 MdoModuleAgentSettingsBegin(Identity);Same=MdoModuleAgentSettings(Identity,0,2,"skill:qa.settings",Why);CHECK(Same&&!strcmp(MdoEsText(Same,"character_name"),"Nova"));
 xrtValueRelease(Same);MdoModuleAgentUnbindContext(Identity);
 Result=Save("{{$settings.api_key}}");xrtValueRelease(Result);
 xvalue* Once=MdoExtensionSettingsSnapshot("skill:qa.settings",false,Why);
 Text=MdoExtensionSettingsRender("skill:qa.settings","role.md","{{$settings.character_name}}",Once,Why);
 CHECK(Text&&!strcmp(Text,"{{$settings.api_key}}"));xrtFree(Text);xrtValueRelease(Once);
 Result=MdoExtensionSettingsRead("skills","qa-settings",Why);char* Json=xrtJsonStringify(Result,false,NULL);CHECK(Json&&!strstr(Json,"private-fixture-key")&&!strstr(Json,"vault:"));xrtFree(Json);xrtValueRelease(Result);
 xrtValueRelease(Pinned);xrtValueRelease(Public);
 /* Native dispatch contexts must be rebound to their owner, never inherit
  * the caller's private dictionary. */
 MdoModuleGeneration Owner={0};Owner.Id="owner";Owner.Directory=MdoExtensionsRead("plugins","prompt-plugin",Why);
 Owner.Application.ExtensionKind="plugins";Owner.Application.DirectoryId="prompt-plugin";
 Owner.ExtensionSettings=MdoExtensionSettingsSnapshot("plugin:example.configurable-prompt",true,Why);
 mdo_sdk_context_v1 Own;C.ExtensionSettings=Full;C.ExtensionKind="skills";
 xvalue* OwnSettings=MdoModulesApplicationOwner(&Own,&C,&Owner);
 CHECK(OwnSettings&&!strcmp(Own.ExtensionKind,"plugins")&&!strcmp(Own.DirectoryId,"prompt-plugin"));
 CHECK(!MdoEsGet(Own.ExtensionSettings,"api_key")&&!strcmp(MdoEsText(Own.ExtensionSettings,"character_name"),"Mira"));
 xrtValueRelease(OwnSettings);xrtValueRelease(Owner.ExtensionSettings);xrtValueRelease(Owner.Directory);xrtValueRelease(Full);
 ActualAgent();McpEnvironment();
 printf("settings_native_checks=%u\nsettings_native_failed=%u\nprobe_done=1\n",Checks,Failed);fflush(stdout);
}
'''


def run(host):
    app = ROOT / "app"
    with tempfile.TemporaryDirectory(prefix="settings-native-", dir=ROOT / ".build") as raw:
        base = Path(raw); home = base / "home"; root = home / "skills/qa-settings"; root.mkdir(parents=True)
        prepare_history_home(home)
        for source, kind in [("roleplay", "agents"), ("image-api", "tools"), ("prompt-plugin", "plugins")]:
            shutil.copytree(ROOT / "examples/extensions/settings" / source, home / kind / source)
        role=home / "agents/roleplay/agent.md"
        role.write_text(role.read_text(encoding="utf-8").replace("tools: inherit", 'tools: inherit\ncontext: {"plugins.instructions":{"include":["plugin:example.configurable-prompt"]},"custom.config":{"file":"custom.md"},"project_rules":{"enabled":true,"required":true}}'), encoding="utf-8")
        (role.parent / "custom.md").write_text("Custom {{$settings.character_name}}",encoding="utf-8")
        manifest=json.loads((role.parent / "extension.json").read_text(encoding="utf-8"));manifest["settings"]["templates"].append("custom.md")
        (role.parent / "extension.json").write_text(json.dumps(manifest),encoding="utf-8")
        (home / "config/settings.json").write_text(json.dumps({"schema_version": 1, "patch": {"agent": {"memory": False, "web_search": False}}}), encoding="utf-8")
        workspace=base / "workspace";workspace.mkdir();(workspace / "AGENTS.md").write_text("Project rules fixture",encoding="utf-8")
        (root / "extension.json").write_text(json.dumps(dict(kind="skills", id="skill:qa.settings", name="Fixture", alias="qa-settings", entry="SKILL.md", platforms=["any"], settings=dict(form="settings.form.json", templates=["role.md"], environment={"SERVICE_API_KEY": "api_key"}))), encoding="utf-8")
        (root / "settings.form.json").write_text(json.dumps(FORM), encoding="utf-8")
        (root / "SKILL.md").write_text("---\nname: Fixture\ndescription: Offline fixture\n---\nHello", encoding="utf-8")
        (root / "role.md").write_text("Name: {{$settings.character_name}}", encoding="utf-8")
        mcp=home / "mcp/qa-settings";shutil.copytree(root,mcp)
        manifest=json.loads((mcp / "extension.json").read_text(encoding="utf-8"));manifest.update(kind="mcp",id="mcp:qa.settings")
        (mcp / "extension.json").write_text(json.dumps(manifest),encoding="utf-8")
        (home / "data/extensions").mkdir(parents=True)
        (home / "data/extensions/state.json").write_text(json.dumps({"skills:qa-settings": dict(kind="skills", id="qa-settings", logical_id="skill:qa.settings", source="manual", enabled=False)}))
        probe = app / (".qa-" + base.name + ".c"); config = probe.with_suffix(".json")
        probe.write_text(PROBE, encoding="utf-8")
        config.write_text(json.dumps(dict(engine=dict(workers=1), services=[dict(enabled=True, name="settings-test", **{"class": "http"}, ip="127.0.0.1", port=free_port(), host_default=dict(enabled=True, path="web", devlang="c", devfile=probe.name))])), encoding="utf-8")
        process = None
        try:
            log = base / "runtime.log"
            with log.open("wb") as output:
                process = subprocess.Popen([str(host), str(config)], cwd=app, env={**os.environ, "MDO_HOME": str(home), "USERPROFILE": str(base), "USE_WEBVIEW": "0", "MDO_SETTINGS_WORKSPACE": str(workspace)}, stdout=output, stderr=output, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            deadline = time.monotonic() + 60
            while process.poll() is None and time.monotonic() < deadline:
                if "probe_done=1" in log.read_text(encoding="utf-8", errors="replace"): break
                time.sleep(.1)
            text = log.read_text(encoding="utf-8", errors="replace")
            assert "settings_native_failed=0" in text and "probe_done=1" in text, text[-6500:]
            print("\n".join(line for line in text.splitlines() if line.startswith("settings_native_")))
        finally:
            if process and process.poll() is None: process.terminate(); process.wait(timeout=10)
            probe.unlink(missing_ok=True); config.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host.resolve())
