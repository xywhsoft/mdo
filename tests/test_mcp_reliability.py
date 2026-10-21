"""Real native MCP admission and capability regressions with offline peers."""
import argparse
import json
import socket
import sys
import tempfile
from pathlib import Path

from test_mcp_runtime import run_probe
import test_mcp_runtime as runtime

ROOT = Path(__file__).resolve().parents[1]

PROBE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>
#include <xllm-executor.h>
#include <xllm-time.h>
static xwork_mcp_client* client;
static int first_ok;
static int32 Worker(void* data) {
    xwork_tool_output output; xwork_error error; (void)data;
    xworkToolOutputInit(&output);
    first_ok=xworkMcpClientCallTool(client,"echo","{}",NULL,XLLM_DEADLINE_NEVER,&output,&error)==XWORK_RESULT_OK && output.bSuccess;
    xworkToolOutputUnit(&output);
    return 0;
}
static xwork_mcp_client* Open(const char* mode) {
    xwork_mcp_stdio_config config; xwork_error error;
    const char* args[]={SCRIPT,mode};
    xworkMcpStdioConfigInit(&config); config.sServerName="fixture";
    config.sProgram=PYTHON; config.psArguments=args; config.iArgumentCount=2;
    config.uStartupTimeoutMs=3000; config.uRequestTimeoutMs=3000;
    xwork_mcp_client* result=xworkMcpClientCreate(&config,&error);
    if(!result || !xworkMcpClientConnect(result,&error)) {
        printf("open_error=%s\n",error.sMessage); if(result)xworkMcpClientDestroy(result);return NULL;
    }
    return result;
}
void ServiceInit(XS_HostInfo* host) {
    xwork_tool_output output; xwork_error error; xwork_mcp_info info;
    xthread* worker; xcancel* cancel; xwork_result cancelled,timed,second;
    (void)host;
    client=Open("resources");printf("resources_connect=%d\n",client!=NULL);
    if(client)xworkMcpClientDestroy(client);
    client=Open("normal");if(!client)goto done;
    worker=xrtThreadCreate(Worker,NULL,0); if(!worker)goto done;
    xrtSleep(100);
    cancel=xrtCancelCreate(); xrtCancelRequest(cancel);xworkToolOutputInit(&output);
    cancelled=xworkMcpClientCallTool(client,"echo","{}",cancel,XLLM_DEADLINE_NEVER,&output,&error);
    xrtCancelDestroy(cancel);
    timed=xworkMcpClientCallTool(client,"echo","{}",NULL,xllmClockUs()+20000u,&output,&error);
    second=xworkMcpClientCallTool(client,"echo","{}",NULL,XLLM_DEADLINE_NEVER,&output,&error);
    xrtThreadWait(worker);xrtThreadDestroy(worker);
    xworkMcpClientGetInfo(client,&info);
    printf("admission=first:%d second:%d cancelled:%d timeout:%d requests:%llu\n",
        first_ok,second==XWORK_RESULT_OK && output.bSuccess,
        cancelled==XWORK_RESULT_CANCELLED,timed==XWORK_RESULT_TIMEOUT,
        (unsigned long long)info.uRequestsCompleted);
    xworkToolOutputUnit(&output);xworkMcpClientDestroy(client);
done: printf("probe_done=1\n");
}
void ServiceUnit(XS_HostInfo* host){(void)host;}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mcp-reliability-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        (base / "web").mkdir()
        (base / "web/index.html").write_text("offline")
        source = "#define PYTHON " + json.dumps(sys.executable) + "\n#define SCRIPT " + json.dumps(str(ROOT / "tests/fixtures/mcp-reliability.py")) + "\n" + PROBE
        (base / "probe.c").write_text(source, encoding="utf-8")
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        (base / "xs.json").write_text(json.dumps({"services": [{"enabled": True,
            "class": "http", "name": "probe", "ip": "127.0.0.1", "port": port,
            "host_default": {"enabled": True,"name":"probe","path":"web",
                             "devlang":"c","devfile":"probe.c"}}]}))
        output = run_probe(args.host.resolve(), base, base / "home")
        assert "resources_connect=1" in output, output
        assert "admission=first:1 second:1 cancelled:1 timeout:1 requests:3" in output, output
        # A single bad or duplicate definition must not hide the valid echo tool.
        runtime.MOCK_SERVER = runtime.MOCK_SERVER.replace(
            '    print(json.dumps',
            '    if method == "tools/list":\n'
            '        result["tools"] += [{"name":"bad","inputSchema":False}, '
            '{"name":"array","inputSchema":{"type":"array"}}, result["tools"][0]]\n'
            '    print(json.dumps')
        site = base / "isolation"
        runtime.write_site(site, base)
        output = run_probe(args.host.resolve(), site, base / "isolation-home")
        assert "ready_status=state:1 enabled:1 connected:1 discovered:1 tools:1 requests:2" in output, output
        assert "probe_done=1" in output and "manager_init_failed=" not in output, output
    print("MCP reliability: PASS (capabilities; concurrent calls; cancellation; deadline; invalid-tool isolation)")


if __name__ == "__main__":
    main()
