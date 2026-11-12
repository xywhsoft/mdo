"""Private C snapshots and XRT templates against the in-place product."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from test_api_runtime import ROOT, free_port
from test_extension_settings_runtime import FORM

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
 xrtValueRelease(Pinned);xrtValueRelease(Public);xrtValueRelease(Full);
 printf("settings_native_checks=%u\nsettings_native_failed=%u\nprobe_done=1\n",Checks,Failed);fflush(stdout);
}
'''


def run(host):
    app = ROOT / "app"
    with tempfile.TemporaryDirectory(prefix="settings-native-", dir=ROOT / ".build") as raw:
        base = Path(raw); home = base / "home"; root = home / "skills/qa-settings"; root.mkdir(parents=True)
        (root / "extension.json").write_text(json.dumps(dict(kind="skills", id="skill:qa.settings", name="Fixture", alias="qa-settings", entry="SKILL.md", platforms=["any"], settings=dict(form="settings.form.json", templates=["role.md"], environment={"SERVICE_API_KEY": "api_key"}))), encoding="utf-8")
        (root / "settings.form.json").write_text(json.dumps(FORM), encoding="utf-8")
        (root / "SKILL.md").write_text("---\nname: Fixture\ndescription: Offline fixture\n---\nHello", encoding="utf-8")
        (root / "role.md").write_text("Name: {{$settings.character_name}}", encoding="utf-8")
        (home / "data/extensions").mkdir(parents=True)
        (home / "data/extensions/state.json").write_text(json.dumps({"skills:qa-settings": dict(kind="skills", id="qa-settings", logical_id="skill:qa.settings", source="manual", enabled=False)}))
        probe = app / (".qa-" + base.name + ".c"); config = probe.with_suffix(".json")
        probe.write_text(PROBE, encoding="utf-8")
        config.write_text(json.dumps(dict(engine=dict(workers=1), services=[dict(enabled=True, name="settings-test", **{"class": "http"}, ip="127.0.0.1", port=free_port(), host_default=dict(enabled=True, path="web", devlang="c", devfile=probe.name))])), encoding="utf-8")
        process = None
        try:
            log = base / "runtime.log"
            with log.open("wb") as output:
                process = subprocess.Popen([str(host), str(config)], cwd=app, env={**os.environ, "MDO_HOME": str(home), "USERPROFILE": str(base), "USE_WEBVIEW": "0"}, stdout=output, stderr=output, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
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
