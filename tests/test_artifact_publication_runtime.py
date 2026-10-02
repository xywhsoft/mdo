"""Bounded native xwork artifact publication across real xs/TCC restarts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import tempfile
from pathlib import Path

from test_agent_runtime import run_probe
from test_interrupt_runtime import free_port


ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>

static unsigned g_ModelCalls;
static char* CopyText(const char* text)
{
    size_t size = strlen(text) + 1u;
    char* copy = (char*)malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

static xllm_result MockModel(void* data, const xllm_request* request,
    const xllm_stream_callbacks* callbacks, xllm_response** response, xllm_error* error)
{
    xllm_response* reply;
    (void)data; (void)request; (void)callbacks; (void)error;
    *response = NULL;
    if (++g_ModelCalls > 2u) return XLLM_RESULT_ERROR;
    reply = (xllm_response*)calloc(1u, sizeof(*reply));
    if (reply == NULL) return XLLM_RESULT_ERROR;
    reply->sContent = CopyText(g_ModelCalls == 1u ? "" : "done");
    reply->sModel = CopyText("local-probe");
    reply->sFinishReason = CopyText(g_ModelCalls == 1u ? "tool_calls" : "stop");
    reply->eFinish = g_ModelCalls == 1u ? XLLM_FINISH_TOOL_CALLS : XLLM_FINISH_STOP;
    if (!reply->sContent || !reply->sModel || !reply->sFinishReason) goto fail;
    if (g_ModelCalls == 1u) {
        reply->pToolCalls = (xllm_tool_call*)calloc(1u, sizeof(xllm_tool_call));
        if (reply->pToolCalls == NULL) goto fail;
        reply->iToolCallCount = 1u;
        reply->pToolCalls[0].sId = CopyText("probe-read");
        reply->pToolCalls[0].sName = CopyText("read");
        reply->pToolCalls[0].sArgumentsJson = CopyText(
            "{\"path\":\"input.txt\",\"start_line\":1,\"max_lines\":200}");
        if (!reply->pToolCalls[0].sId || !reply->pToolCalls[0].sName ||
            !reply->pToolCalls[0].sArgumentsJson) goto fail;
    }
    *response = reply;
    return XLLM_RESULT_OK;
fail:
    xllmResponseDestroy(reply);
    return XLLM_RESULT_ERROR;
}

void ServiceInit(XS_HostInfo* host)
{
    xwork_runtime_config runtime_config;
    xwork_agent_definition_config definition_config;
    xwork_agent_options options;
    xllm_session_config session_config;
    xwork_runtime* runtime = NULL;
    xwork_agent_definition* definition = NULL;
    xllm_session* session = NULL;
    xwork_agent* agent = NULL;
    xwork_run_config run_config;
    xwork_run* run = NULL;
    xwork_run_result result = {0};
    xwork_artifact_info info;
    xwork_artifact_chunk chunk;
    xwork_error error;
    bool ok = false;
    (void)host;
    xworkArtifactChunkInit(&chunk);
    xworkRuntimeConfigInit(&runtime_config);
    runtime_config.uMaxArtifacts = 1u;
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    xworkAgentDefinitionConfigInit(&definition_config);
    definition_config.sId = "probe.artifact-publication";
    definition_config.bAutoSaveSession = false;
    definition_config.iMaxInlineToolBytes = 256u;
    definition = xworkAgentDefinitionCreate(&definition_config, &error);
    xllmSessionConfigInit(&session_config);
    session = xllmSessionCreate(&session_config, NULL);
    xworkAgentOptionsInit(&options);
    options.pSession = session;
    options.sWorkspaceRoot = WORKSPACE;
    options.sArtifactDirectory = "artifacts";
    options.OnModelComplete = MockModel;
    if (runtime && definition && session)
        agent = xworkAgentCreateWithRuntime(runtime, definition, &options, &error);
    xworkRunConfigInit(&run_config);
    run_config.sPrompt = "Read input.txt";
    run = agent ? xworkRunCreate(agent, &run_config, &error) : NULL;
    if (!run || !xworkRunStart(run, &error) ||
        xworkRunWait(run, xrtDeadlineAfter(UINT64_C(5000000)), &result, &error) != XWORK_RESULT_OK ||
        g_ModelCalls != 2u) goto done;
    xworkArtifactInfoInit(&info);
    if (xworkRuntimeArtifactCount(runtime) != 1u ||
        !xworkRuntimeArtifactAt(runtime, 0u, &info) ||
        !xworkRuntimeReadArtifact(runtime, info.uArtifactId, 0u,
            XWORK_ARTIFACT_READ_MAX_BYTES, &chunk, &error) ||
        !chunk.bEof || chunk.iSize != info.uSizeBytes || chunk.iSize <= 256u) goto done;
    printf("probe_artifact=%s\nprobe_id=%llu\nprobe_sha256=%s\n", info.sPath,
        (unsigned long long)info.uArtifactId, info.sSha256);
    ok = true;
done:
    if (!ok) printf("probe_error=%d:%s\n", error.eCode, error.sMessage);
    xworkArtifactChunkUnit(&chunk);
    xworkRunResultUnit(&result);
    xworkRunDestroy(run);
    xworkAgentDestroy(agent);
    xllmSessionDestroy(session);
    xworkAgentDefinitionRelease(definition);
    xworkRuntimeRelease(runtime);
    printf("probe_ok=%d\nprobe_done=1\n", ok);
    fflush(stdout);
}
void ServiceUnit(XS_HostInfo* host) { (void)host; }
'''


def run(host: Path) -> None:
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="artifact-publication-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        workspace = base / "workspace"
        (site / "web").mkdir(parents=True)
        workspace.mkdir()
        (site / "web/index.html").write_text("artifact publication probe", encoding="utf-8")
        (site / "probe.c").write_text(
            "#define WORKSPACE " + json.dumps(str(workspace.resolve())) + "\n" + SOURCE,
            encoding="utf-8")
        (site / "xs.json").write_text(json.dumps({"engine": {"workers": 1},
            "services": [{"enabled": True, "class": "http", "name": "artifact-probe",
                "ip": "127.0.0.1", "port": free_port(), "host_default": {
                    "enabled": True, "name": "probe", "path": "web", "devlang": "c",
                    "devfile": "probe.c"}}]}), encoding="utf-8")
        saved: dict[Path, bytes] = {}
        for sequence, marker in enumerate(("first", "second", "restart"), 1):
            (workspace / "input.txt").write_text((marker + "-bounded-tool-output\n") * 100,
                                                  encoding="utf-8")
            output = run_probe(host, site, base / "unused-home")
            assert "probe_ok=1" in output, output
            match = re.search(r"^probe_artifact=(.+)$", output, re.MULTILINE)
            digest = re.search(r"^probe_sha256=([0-9a-f]{64})$", output, re.MULTILINE)
            identity = re.search(r"^probe_id=(\d+)$", output, re.MULTILINE)
            assert match and digest and identity, output
            relative = Path(match.group(1).strip())
            assert not relative.is_absolute() and ".." not in relative.parts, relative
            artifact = (workspace / relative).resolve()
            assert artifact.is_relative_to(workspace.resolve()) and artifact not in saved, output
            assert int(identity.group(1)) == sequence, output
            assert artifact.parent.name == "run-00000000000000000001", output
            payload = artifact.read_bytes()
            assert marker.encode() in payload and hashlib.sha256(payload).hexdigest() == digest.group(1)
            for previous, original in saved.items():
                assert previous.read_bytes() == original, previous
            saved[artifact] = payload
            assert not list((workspace / "artifacts").rglob(".xwork-*.tmp"))
        assert len(list((workspace / "artifacts").rglob("*.txt"))) == 3


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    run(args.host.resolve())
    print("artifact publication runtime probe: PASS")
