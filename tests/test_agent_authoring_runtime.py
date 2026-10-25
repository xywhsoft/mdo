"""Exercise role authoring through the real TCC parser and module catalog."""
from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

import test_agent_runtime as fixture

CHECKS = r'''
static void AuthoringChecks(void) {
    MdoAgentFile F; char E[1024]; unsigned Passed=0,Total=0;
#define CHECK(expression) do { ++Total; if(expression) ++Passed; else printf("authoring_failure=%u:%s\n",Total,E); } while(0)
    const char* Simple="---\nname: Reviewer\nmode: both\nextends: mdo.general\n---\nFind defects.";
    CHECK(MdoAgentFileParse("reviewer",Simple,false,&F,E,sizeof(E)));
    CHECK((F.Agent.Flags & (MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT))==(MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT));
    CHECK(F.Agent.ConfigurationJson && strstr(F.Agent.ConfigurationJson,"mdo.general"));
    MdoAgentFileUnit(&F);
    const char* Json="{\"schema\":\"mdo.agent.v1\",\"name\":\"JSON\",\"prompt\":\"json-body\",\"code\":true,\"tools\":[],\"runtime\":{\"max_turns\":9,\"context_tokens\":100000},\"context\":{\"memory.global\":false},\"require_verification_after_write\":true}";
    CHECK(MdoAgentFileParse("json",Json,false,&F,E,sizeof(E)));
    CHECK(F.UseCode && F.Agent.MaxTurns==9 && F.Agent.ContextWindowTokens==100000);
    CHECK((F.Agent.Flags&MDO_AGENT_EXPLICIT_TOOLS) && (F.Agent.Flags&MDO_AGENT_REQUIRE_VERIFICATION_AFTER_WRITE));
    MdoAgentFileUnit(&F);
    const char* Parent="---\nname: Base\ndescription: Base description\ntools: [read]\nruntime: {\"max_turns\":17}\ncontext: {\"memory.global\":false,\"knowledge\":false}\n---\nbase-body";
    CHECK(MdoHomeAtomicWrite("agents/base.md",Parent,strlen(Parent),false));
    const char* Child="---\nextends: agent.base\nname: Derived\ncontext: {\"knowledge\":true}\n---\nchild-body";
    CHECK(MdoAgentFileParse("derived",Child,false,&F,E,sizeof(E)));
    CHECK(strstr(F.Prompt,"base-body") && strstr(F.Prompt,"child-body") && F.Agent.MaxTurns==17 && F.Agent.ToolCount==1);
    xvalue* Config=xrtJsonParse(xrtStrView(F.ConfigurationJson));
    const xvalue* Context=MdoAgentConfigGet(Config,"context");
    CHECK(!MdoAgentConfigBool(Context,"memory.global",true) && MdoAgentConfigBool(Context,"knowledge",false));
    xrtValueRelease(Config);MdoAgentFileUnit(&F);
    const char* Cycle="---\nextends: agent.cycle\n---\ncycle";
    CHECK(MdoHomeAtomicWrite("agents/cycle.md",Cycle,strlen(Cycle),false));
    CHECK(!MdoAgentFileParse("cycle",Cycle,true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"runtime\":{\"max_turns\":-1}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"mode\":\"invalid\"}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"context\":{\"missing\":true}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"context\":{\"custom.test\":{\"file\":\"../secret\"}}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"read_only\":\"perhaps\"}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"schema\": 1}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"runtime\": {\"max_model_calls\": -1}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"runtime\": {\"invented\": 1}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"history\": {\"mode\": \"unsafe\"}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"history\": {\"include_summary\": \"yes\"}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"model_options\": {\"temperature\": 3}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"model_options\": {\"extra_body\": {\"messages\": []}}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"model_options\": {\"tool_choice\": 3}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"context\": {\"system\": {\"enabled\": \"yes\"}}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"tools\": [\"read*unsafe\"]}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"delegation\": {\"context\": \"everything\"}}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    CHECK(!MdoAgentFileParse("bad","{\"name\": \"bad\\u0000name\"}",true,&F,E,sizeof(E)));MdoAgentFileUnit(&F);
    printf("authoring_checks=%u/%u\n",Passed,Total);
#undef CHECK
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=fixture.ROOT / ".build/host/xs.exe")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="agent-authoring-", dir=fixture.ROOT / ".build") as raw:
        site = Path(raw) / "site"
        fixture.write_site(site)
        source = (site / "probe.c").read_text(encoding="utf-8")
        source = source.replace("void ServiceInit(", CHECKS + "\nvoid ServiceInit(")
        source = source.replace("xworkRuntimeConfigInit(&runtime_config);", "AuthoringChecks();\n    xworkRuntimeConfigInit(&runtime_config);")
        (site / "probe.c").write_text(source, encoding="utf-8")
        output = fixture.run_probe(args.host.resolve(), site, Path(raw) / "home")
        assert "authoring_checks=29/29" in output and "authoring_failure=" not in output, output
        assert "probe_done=1" in output, output
    print("Agent authoring runtime: 29 checks PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
