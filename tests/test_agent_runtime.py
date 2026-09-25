"""Bounded MDO-6D Agent session probe through the real xs/TCC runtime."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

SKILL_V1 = '''---
name: Probe Skill
description: Bounded Agent composition fixture.
version: 1.0.0
tools: [grep]
mcp: []
permissions: [workspace.read]
scripts: []
templates: []
assets: []
---
probe-skill-v1
'''

SKILL_V2 = SKILL_V1.replace("1.0.0", "2.0.0").replace(
    "probe-skill-v1", "probe-skill-v2"
)

MODULE_V1 = r'''
#include "mdo/module.h"

static const mdo_host_services_v1 *g_Host;
static const char *MainTools[] = {"read", "ls", "glob", "grep", "agent",
    "memory_search", "memory_write", "memory_delete"};
static const char *ChildTools[] = {"read", "grep"};
static const char *Skills[] = {"probe-skill"};

static mdo_result AgentAcquire(void *data, const mdo_host_services_v1 *host,
    char *error, size_t error_capacity) {
    (void)error; (void)error_capacity; g_Host = host;
    if (host && host->Core && host->Core->Log)
        host->Core->Log(host->Core->Context, MDO_LOG_INFO, (const char*)data);
    return MDO_RESULT_OK;
}

static void AgentRelease(void *data) {
    (void)data;
    if (g_Host && g_Host->Core && g_Host->Core->Log)
        g_Host->Core->Log(g_Host->Core->Context, MDO_LOG_INFO,
            "probe-agent-release-v1");
}

static const mdo_agent_v1 Main = {
    .Size = sizeof(mdo_agent_v1), .AbiVersion = MDO_MODULE_ABI_VERSION,
    .Id = "probe.main", .Name = "Probe Main",
    .Description = "Bounded main Agent fixture.",
    .SystemPrompt = "probe-system-v1", .PermissionProfile = "read-only",
    .Tools = MainTools, .ToolCount = 8u,
    .Skills = Skills, .SkillCount = 1u,
    .MaxOutputTokens = 4096u, .MaxTurns = 4u,
    .TimeoutMilliseconds = 5000u, .MaxFinalBytes = 4096u,
    .AllowedEffects = MDO_TOOL_EFFECT_READ | MDO_TOOL_EFFECT_WORKSPACE_WRITE |
        MDO_TOOL_EFFECT_AGENT_DELEGATION,
    .MaxDepth = 2u, .Flags = MDO_AGENT_MAIN | MDO_AGENT_ALLOW_DELEGATION,
    .UserData = (void*)"probe-main-acquire-v1",
    .Acquire = AgentAcquire, .Release = AgentRelease
};

static const mdo_agent_v1 Child = {
    .Size = sizeof(mdo_agent_v1), .AbiVersion = MDO_MODULE_ABI_VERSION,
    .Id = "probe.child", .Name = "Probe Child",
    .Description = "Bounded read-only child fixture.",
    .SystemPrompt = "probe-child-v1", .PermissionProfile = "read-only",
    .Tools = ChildTools, .ToolCount = 2u,
    .Skills = Skills, .SkillCount = 1u,
    .MaxOutputTokens = 2048u, .MaxTurns = 2u,
    .TimeoutMilliseconds = 3000u, .MaxFinalBytes = 2048u,
    .AllowedEffects = MDO_TOOL_EFFECT_READ, .MaxDepth = 1u,
    .Flags = MDO_AGENT_SUBAGENT | MDO_AGENT_READ_ONLY,
    .UserData = (void*)"probe-child-acquire-v1",
    .Acquire = AgentAcquire, .Release = AgentRelease
};

static const mdo_agent_v1 MissingSkillTool = {
    .Size = sizeof(mdo_agent_v1), .AbiVersion = MDO_MODULE_ABI_VERSION,
    .Id = "probe.missing-tool", .Name = "Probe Missing Tool",
    .Description = "Skill policy rejection fixture.",
    .SystemPrompt = "probe-missing-tool", .PermissionProfile = "read-only",
    .Skills = Skills, .SkillCount = 1u,
    .MaxOutputTokens = 2048u, .MaxTurns = 2u,
    .TimeoutMilliseconds = 3000u, .MaxFinalBytes = 2048u,
    .AllowedEffects = 0u, .MaxDepth = 1u, .Flags = MDO_AGENT_MAIN
};

static mdo_result Register(const mdo_host_services_v1 *host,
    const mdo_registrar_v1 *registrar, void **data,
    char *error, size_t error_capacity) {
    mdo_result result; (void)data; g_Host = host;
    result = registrar->AddAgent(registrar->Context, &Main, error, error_capacity);
    if (result != MDO_RESULT_OK) return result;
    result = registrar->AddAgent(registrar->Context, &Child, error, error_capacity);
    if (result != MDO_RESULT_OK) return result;
    return registrar->AddAgent(registrar->Context, &MissingSkillTool,
        error, error_capacity);
}

static const mdo_module_v1 Module = {
    MDO_V1_HEADER(mdo_module_v1), "probe.agents", "Probe Agents",
    "Agent session fixture.", "1.0.0", MDO_CAPABILITY_LOG,
    0, 0, Register, 0
};

MDO_EXPORT const mdo_module_v1 *mdoModuleEntry(void) { return &Module; }
'''.strip()

MODULE_V2 = (
    MODULE_V1.replace("probe-system-v1", "probe-system-v2")
    .replace("probe-child-v1", "probe-child-v2")
    .replace("acquire-v1", "acquire-v2")
    .replace("release-v1", "release-v2")
    .replace('"1.0.0"', '"2.0.0"')
)

MODULE_V1_NO_MEMORY = (
    MODULE_V1.replace(
        ',\n    "memory_search", "memory_write", "memory_delete"', ""
    )
    .replace(".Tools = MainTools, .ToolCount = 8u,",
             ".Tools = MainTools, .ToolCount = 5u,")
    .replace(
        "MDO_TOOL_EFFECT_READ | MDO_TOOL_EFFECT_WORKSPACE_WRITE |\n"
        "        MDO_TOOL_EFFECT_AGENT_DELEGATION",
        "MDO_TOOL_EFFECT_READ | MDO_TOOL_EFFECT_AGENT_DELEGATION",
    )
)
MODULE_V2_NO_MEMORY = (
    MODULE_V2.replace(
        ',\n    "memory_search", "memory_write", "memory_delete"', ""
    )
    .replace(".Tools = MainTools, .ToolCount = 8u,",
             ".Tools = MainTools, .ToolCount = 5u,")
    .replace(
        "MDO_TOOL_EFFECT_READ | MDO_TOOL_EFFECT_WORKSPACE_WRITE |\n"
        "        MDO_TOOL_EFFECT_AGENT_DELEGATION",
        "MDO_TOOL_EFFECT_READ | MDO_TOOL_EFFECT_AGENT_DELEGATION",
    )
)


def c_literal(value: str) -> str:
    return json.dumps(value)


PROBE_SOURCE = rf'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/security/secrets.c"
#include "src/models/catalog.c"
#include "src/skills/manager.c"
#include "src/memory/manager.c"
#include "src/modules/manager.c"
#include "src/agents/runtime.c"
#include "src/asks/manager.c"

xwork_runtime *MdoBootstrapRuntime(void) {{ return NULL; }}

static const char sSkillV1[] = {c_literal(SKILL_V1)};
static const char sSkillV2[] = {c_literal(SKILL_V2)};
static const char sModuleV1[] = {c_literal(MODULE_V1)};
static const char sModuleV2[] = {c_literal(MODULE_V2)};

typedef struct ProbeOwner {{
    unsigned Refs;
    unsigned Retains;
    unsigned Releases;
    unsigned Calls;
    bool SawModel;
    bool SawReasoning;
    bool SawSystemV1;
    bool SawSkillV1;
    bool SawSkillV2;
    bool SawMemory;
    bool SawInstructionsV1;
}} ProbeOwner;

static bool OwnerRetain(void *data) {{
    ProbeOwner *owner = (ProbeOwner*)data;
    if (owner == NULL || owner->Refs == 0u) return false;
    ++owner->Refs; ++owner->Retains; return true;
}}

static void OwnerRelease(void *data) {{
    ProbeOwner *owner = (ProbeOwner*)data;
    if (owner == NULL || owner->Refs == 0u) return;
    --owner->Refs; ++owner->Releases;
}}

static xllm_response *Response(const char *text) {{
    size_t text_size = strlen(text) + 1u;
    xllm_response *response = (xllm_response*)calloc(1u, sizeof(*response));
    if (response == NULL) return NULL;
    response->sContent = (char*)malloc(text_size);
    response->sModel = (char*)malloc(17u);
    response->sRequestId = (char*)malloc(14u);
    response->sFinishReason = (char*)malloc(5u);
    if (response->sContent != NULL) memcpy(response->sContent, text, text_size);
    if (response->sModel != NULL) memcpy(response->sModel, "probe-wire-model", 17u);
    if (response->sRequestId != NULL) memcpy(response->sRequestId, "probe-request", 14u);
    if (response->sFinishReason != NULL) memcpy(response->sFinishReason, "stop", 5u);
    response->eFinish = XLLM_FINISH_STOP;
    if (response->sContent == NULL || response->sModel == NULL ||
        response->sRequestId == NULL || response->sFinishReason == NULL) {{
        xllmResponseDestroy(response); return NULL;
    }}
    response->uHttpStatus = 200u;
    return response;
}}

static xllm_result Complete(void *data, const xllm_request *request,
    const xllm_stream_callbacks *callbacks, xllm_response **response,
    xllm_error *error) {{
    ProbeOwner *owner = (ProbeOwner*)data;
    size_t i;
    (void)callbacks; (void)error;
    ++owner->Calls;
    owner->SawModel = request->sModel != NULL &&
        strcmp(request->sModel, "ling-3.0-tiny") == 0;
    owner->SawReasoning = request->sReasoningEffort != NULL &&
        strcmp(request->sReasoningEffort, "medium") == 0;
    for (i = 0u; i < request->iMessageCount; ++i) {{
        const char *text = request->pMessages[i].sContent;
        if (text == NULL) continue;
        if (strstr(text, "probe-system-v1") != NULL) owner->SawSystemV1 = true;
        if (strstr(text, "probe-skill-v1") != NULL) owner->SawSkillV1 = true;
        if (strstr(text, "probe-skill-v2") != NULL) owner->SawSkillV2 = true;
        if (strstr(text, "agent-memory-probe") != NULL &&
            strstr(text, "untrusted reference data") != NULL)
            owner->SawMemory = true;
        if (strstr(text, "<user_instructions>\nprobe-custom-v1\n</user_instructions>") != NULL)
            owner->SawInstructionsV1 = true;
    }}
    *response = Response("agent-runtime-ok");
    return *response != NULL ? XLLM_RESULT_OK : XLLM_RESULT_ERROR;
}}

static void PrintRuntimeError(const char *label, const xwork_error *error) {{
    printf("%s=code:%d message:%s\n", label,
        error != NULL ? (int)error->eCode : -1,
        error != NULL && error->sMessage[0] != '\0' ? error->sMessage : "none");
}}

static bool AgentHasTool(const MdoAgentSession *session, const char *name) {{
    xwork_tool_catalog *catalog;
    xwork_tool_info info;
    size_t i;
    if (session == NULL || session->Agent == NULL || name == NULL) return false;
    catalog = xworkAgentToolCatalogSnapshot(session->Agent);
    if (catalog == NULL) return false;
    for (i = 0u; i < xworkToolCatalogCount(catalog); ++i) {{
        memset(&info, 0, sizeof(info));
        if (xworkToolCatalogToolAt(catalog, i, &info) &&
            info.sName != NULL && strcmp(info.sName, name) == 0) {{
            xworkToolCatalogRelease(catalog);
            return true;
        }}
    }}
    xworkToolCatalogRelease(catalog);
    return false;
}}

void ServiceInit(XS_HostInfo *host) {{
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    xwork_error error;
    MdoAgentSessionOptions options;
    MdoAgentSessionInfo session_info;
    MdoAgentRunOptions run_options;
    MdoAgentRunInfo run_info;
    MdoAgentSession *session = NULL;
    MdoAgentSession *invalid = NULL;
    MdoAgentRun *run = NULL;
    MdoMemoryWriteOptions memory_write;
    xwork_run_result result;
    xwork_result run_result;
    ProbeOwner owner;
    (void)host;
    memset(&owner, 0, sizeof(owner)); owner.Refs = 1u;
    if (!MdoHomeInit() ||
        !MdoHomeAtomicWrite("skills/probe-skill/SKILL.md", sSkillV1,
            strlen(sSkillV1), false) ||
        !MdoHomeAtomicWrite("modules/agents/probe.c", sModuleV1,
            strlen(sModuleV1), false) ||
        !MdoConfigInit() || !MdoModelManagerInit() || !MdoSkillManagerInit()) {{
        printf("init_error=pre-runtime\n"); goto done;
    }}
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoMemoryManagerInit(runtime) ||
        !MdoModuleManagerInit(runtime)) {{
        PrintRuntimeError("init_error", &error); goto done;
    }}
    MdoMemoryWriteOptionsInit(&memory_write);
    memory_write.Scope = MDO_MEMORY_PROJECT;
    memory_write.ProjectId = "project-alpha";
    memory_write.Id = "agent-memory-probe";
    memory_write.Title = "Agent memory";
    memory_write.Content = "agent-memory-probe";
    memory_write.Actor = "agent-runtime-probe";
    if (!MdoMemoryUpsert(&memory_write, &error)) {{
        PrintRuntimeError("memory_error", &error); goto done;
    }}
    MdoAgentSessionOptionsInit(&options);
    options.AgentId = "probe.main";
    options.ProjectId = "project-alpha";
    options.ProductSessionId = "agent-runtime";
    options.WorkspaceRoot = ".";
    options.OnModelComplete = Complete;
    options.ModelUserData = &owner;
    options.OwnerUserData = &owner;
    options.OnOwnerRetain = OwnerRetain;
    options.OnOwnerRelease = OwnerRelease;
    session = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    if (session == NULL) {{ PrintRuntimeError("session_error", &error); goto done; }}
    memset(&session_info, 0, sizeof(session_info));
    session_info.Size = sizeof(session_info);
    if (!MdoAgentSessionGetInfo(session, &session_info)) goto done;
    printf("session=agent:%s module:%s model:%s provider:%s wire:%s reasoning:%s permission:%s protocol:%d output:%u tools:%zu skills:%zu subagents:%zu generations:%llu/%llu/%llu/%llu\n",
        session_info.AgentId, session_info.ModuleId, session_info.ModelId,
        session_info.ProviderId, session_info.WireModel,
        session_info.ReasoningEffort, session_info.PermissionProfile,
        (int)session_info.Protocol, session_info.MaxOutputTokens,
        session_info.ToolCount, session_info.SkillCount, session_info.SubagentCount,
        (unsigned long long)session_info.ModelGeneration,
        (unsigned long long)session_info.ModuleGeneration,
        (unsigned long long)session_info.SkillGeneration,
        (unsigned long long)session_info.MemoryGeneration);
    printf("owner_after_create=refs:%u retains:%u releases:%u\n",
        owner.Refs, owner.Retains, owner.Releases);
    printf("memory_tools=search:%d write:%d delete:%d\n",
        AgentHasTool(session, "memory_search") ? 1 : 0,
        AgentHasTool(session, "memory_write") ? 1 : 0,
        AgentHasTool(session, "memory_delete") ? 1 : 0);

    if (!MdoHomeAtomicWrite("skills/probe-skill/SKILL.md", sSkillV2,
            strlen(sSkillV2), false) || !MdoSkillManagerReload() ||
        !MdoHomeAtomicWrite("modules/agents/probe.c", sModuleV2,
            strlen(sModuleV2), false) || !MdoModuleManagerReload()) {{
        printf("reload_error=1\n"); goto done;
    }}
    printf("reloaded=modules:%llu skills:%llu pinned:%llu/%llu\n",
        (unsigned long long)MdoModuleManagerGeneration(),
        (unsigned long long)MdoSkillManagerGeneration(),
        (unsigned long long)session_info.ModuleGeneration,
        (unsigned long long)session_info.SkillGeneration);

    MdoAgentSessionOptionsInit(&options);
    options.AgentId = "probe.main";
    options.ReasoningEffort = "unsupported";
    options.OnModelComplete = Complete;
    options.ModelUserData = &owner;
    invalid = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    printf("invalid_reasoning=%d code:%d\n", invalid != NULL ? 1 : 0,
        (int)error.eCode);
    MdoAgentSessionRelease(invalid); invalid = NULL;

    MdoAgentSessionOptionsInit(&options);
    options.AgentId = "probe.missing-tool";
    options.OnModelComplete = Complete;
    options.ModelUserData = &owner;
    invalid = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    printf("invalid_skill_tools=%d code:%d message:%s\n",
        invalid != NULL ? 1 : 0, (int)error.eCode, error.sMessage);
    MdoAgentSessionRelease(invalid); invalid = NULL;

    MdoAgentRunOptionsInit(&run_options);
    run_options.Prompt = "execute the bounded probe";
    run = MdoAgentRunCreate(session, &run_options, &error);
    if (run == NULL) {{ PrintRuntimeError("run_create_error", &error); goto done; }}
    MdoAgentSessionRelease(session); session = NULL;
    printf("owner_after_session_release=refs:%u releases:%u\n",
        owner.Refs, owner.Releases);
    if (!MdoAgentRunStart(run, &error)) {{
        PrintRuntimeError("run_start_error", &error); goto done;
    }}
    memset(&result, 0, sizeof(result));
    run_result = MdoAgentRunWait(run, xrtDeadlineAfter(UINT64_C(5000000)),
        &result, &error);
    printf("run=result:%d text:%s turns:%llu calls:%llu\n", (int)run_result,
        result.sFinalText != NULL ? result.sFinalText : "null",
        (unsigned long long)result.uAgentTurns,
        (unsigned long long)result.uModelCalls);
    memset(&run_info, 0, sizeof(run_info)); run_info.Size = sizeof(run_info);
    if (MdoAgentRunGetInfo(run, &run_info))
        printf("run_info=agent:%s model:%s reasoning:%s state:%d result:%d generations:%llu/%llu/%llu/%llu\n",
            run_info.AgentId, run_info.ModelId, run_info.ReasoningEffort,
            (int)run_info.Run.eState, (int)run_info.Run.eResult,
            (unsigned long long)run_info.ModelGeneration,
            (unsigned long long)run_info.ModuleGeneration,
            (unsigned long long)run_info.SkillGeneration,
            (unsigned long long)run_info.MemoryGeneration);
    printf("callback=calls:%u model:%d reasoning:%d system_v1:%d skill_v1:%d skill_v2:%d memory:%d instructions_v1:%d\n",
        owner.Calls, owner.SawModel ? 1 : 0, owner.SawReasoning ? 1 : 0,
        owner.SawSystemV1 ? 1 : 0, owner.SawSkillV1 ? 1 : 0,
        owner.SawSkillV2 ? 1 : 0, owner.SawMemory ? 1 : 0,
        owner.SawInstructionsV1 ? 1 : 0);
    xworkRunResultUnit(&result);
    MdoAgentRunDestroy(run); run = NULL;
    printf("owner_after_run_destroy=refs:%u retains:%u releases:%u\n",
        owner.Refs, owner.Retains, owner.Releases);

    MdoAgentSessionOptionsInit(&options);
    options.AgentId = "probe.main";
    options.ProjectId = "project-alpha";
    options.WorkspaceRoot = ".";
    options.SessionPath = "prompt-session.snapshot";
    options.JournalPath = "prompt-session.journal";
    options.OnModelComplete = Complete;
    options.ModelUserData = &owner;
    session = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    if (session == NULL) {{ PrintRuntimeError("prompt_create_error", &error); goto done; }}
    printf("prompt_initial=%d\n", strstr(session->SystemPrompt,
        "probe-custom-v1") != NULL ? 1 : 0);
    {{
        xllm_error model_error;
        if (!xllmSessionSetSystemPrompt(session->Owner->LlmSession,
                session->SystemPrompt, &model_error) ||
            !MdoAgentSessionCheckpoint(session, &error)) {{
            PrintRuntimeError("prompt_checkpoint_error", &error); goto done;
        }}
    }}
    MdoAgentSessionRelease(session); session = NULL;
    if (!MdoConfigImport(MDO_CONFIG_SETTINGS,
            xrtStrView("{{\"schema_version\":1,\"patch\":{{\"agent\":{{\"user_instructions\":\"probe-custom-v2\"}}}}}}"))) {{
        printf("prompt_config_error=1\n"); goto done;
    }}
    options.Recover = true;
    session = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    if (session == NULL) {{ PrintRuntimeError("prompt_recover_error", &error); goto done; }}
    printf("prompt_recovered=old:%d new:%d\n",
        strstr(session->SystemPrompt, "probe-custom-v1") != NULL ? 1 : 0,
        strstr(session->SystemPrompt, "probe-custom-v2") != NULL ? 1 : 0);
    if (!MdoAgentSessionClear(session, &error)) {{
        PrintRuntimeError("prompt_clear_error", &error); goto done;
    }}
    printf("prompt_after_clear=old:%d new:%d\n",
        strstr(xllmSessionGetSystemPrompt(session->Owner->LlmSession),
            "probe-custom-v1") != NULL ? 1 : 0,
        strstr(xllmSessionGetSystemPrompt(session->Owner->LlmSession),
            "probe-custom-v2") != NULL ? 1 : 0);
    if (!MdoAgentSessionTruncateAfter(session, 0u, &error)) {{
        PrintRuntimeError("prompt_truncate_error", &error); goto done;
    }}
    printf("prompt_after_truncate=old:%d new:%d\n",
        strstr(xllmSessionGetSystemPrompt(session->Owner->LlmSession),
            "probe-custom-v1") != NULL ? 1 : 0,
        strstr(xllmSessionGetSystemPrompt(session->Owner->LlmSession),
            "probe-custom-v2") != NULL ? 1 : 0);
    MdoAgentSessionRelease(session); session = NULL;
    options.Recover = false;
    options.SessionPath = NULL;
    options.JournalPath = NULL;
    session = MdoAgentSessionCreateWithRuntime(runtime, &options, &error);
    if (session == NULL) {{ PrintRuntimeError("prompt_new_error", &error); goto done; }}
    printf("prompt_new=old:%d new:%d\n",
        strstr(session->SystemPrompt, "probe-custom-v1") != NULL ? 1 : 0,
        strstr(session->SystemPrompt, "probe-custom-v2") != NULL ? 1 : 0);
    MdoAgentSessionRelease(session); session = NULL;
done:
    MdoAgentRunDestroy(run);
    MdoAgentSessionRelease(invalid);
    MdoAgentSessionRelease(session);
    MdoModuleManagerUnit();
    MdoMemoryManagerUnit();
    MdoSkillManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoModelManagerUnit();
    MdoConfigUnit();
    MdoHomeUnit();
    printf("probe_done=1\n");
}}

void ServiceUnit(XS_HostInfo *host) {{ (void)host; }}
'''


def write_site(site: Path, memory_enabled: bool = True) -> None:
    for relative in (
        "web",
        "default-home/config",
        "default-home/modules/tools",
        "default-home/modules/agents",
        "default-home/modules/subagents",
        "default-home/skills/project-explorer/templates",
        "generated/module-sdk/mdo",
        "src/storage",
        "src/config",
        "src/security",
        "src/models",
        "src/skills",
        "src/memory",
        "src/modules",
        "src/agents",
        "src/asks",
        "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "default-home/modules/tools/builtin_echo.c",
        "default-home/modules/agents/builtin_default.c",
        "default-home/skills/project-explorer/SKILL.md",
        "default-home/skills/project-explorer/templates/report.md",
        "src/storage/home.c",
        "src/config/config.c",
        "src/security/secrets.c",
        "src/models/catalog.c",
        "src/skills/manager.c",
        "src/memory/manager.c",
        "src/modules/manager.c",
        "src/agents/runtime.c",
        "src/asks/manager.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    for header in (ROOT / "app/include/mdo").glob("*.h"):
        shutil.copy2(header, site / "include/mdo" / header.name)
    shutil.copy2(
        ROOT / "app/src/memory/internal.h", site / "src/memory/internal.h"
    )
    defaults_path = site / "default-home/config/defaults.json"
    defaults = json.loads(defaults_path.read_text(encoding="utf-8"))
    defaults["settings"]["agent"]["user_instructions"] = "probe-custom-v1"
    if not memory_enabled:
        defaults["settings"]["agent"]["memory"] = False
    defaults_path.write_text(json.dumps(defaults), encoding="utf-8")
    shutil.copy2(
        ROOT / "include/mdo/module.h",
        site / "generated/module-sdk/mdo/module.h",
    )
    probe_source = PROBE_SOURCE
    if not memory_enabled:
        probe_source = probe_source.replace(
            c_literal(MODULE_V1), c_literal(MODULE_V1_NO_MEMORY)
        ).replace(c_literal(MODULE_V2), c_literal(MODULE_V2_NO_MEMORY))
    (site / "probe.c").write_text(probe_source, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(
        json.dumps(
            {
                "services": [
                    {
                        "enabled": True,
                        "class": "http",
                        "name": "agent-probe",
                        "ip": "127.0.0.1",
                        "port": port,
                        "host_default": {
                            "enabled": True,
                            "name": "probe",
                            "path": "web",
                            "devlang": "c",
                            "devfile": "probe.c",
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )


def run_probe(host: Path, site: Path, home: Path) -> str:
    command = [str(host), "xs.json", "--", "--home", str(home)]
    process = subprocess.Popen(
        command,
        cwd=site,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
    )
    lines: list[str] = []
    done = threading.Event()
    assert process.stdout is not None

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.append(line)
            if "probe_done=1" in line:
                done.set()

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    done.wait(timeout=20.0)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
    reader.join(timeout=3.0)
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host",
        type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"),
    )
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="agent-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)
        output = run_probe(host, site, base / "home")
        assert "init_error=" not in output, output
        assert "session_error=" not in output, output
        assert "reload_error=" not in output, output
        assert "run_create_error=" not in output, output
        assert "run_start_error=" not in output, output
        assert "session=agent:probe.main module:probe.agents model:ling-3.0-tiny" in output, output
        assert "wire:ling-3.0-tiny reasoning:medium permission:read-only" in output, output
        assert "output:4096" in output, output
        assert "skills:1 subagents:1 generations:1/1/1/2" in output, output
        assert "owner_after_create=refs:2 retains:1 releases:0" in output, output
        assert "memory_tools=search:1 write:0 delete:0" in output, output
        assert "reloaded=modules:2 skills:2 pinned:1/1" in output, output
        assert "invalid_reasoning=0" in output, output
        assert "invalid_skill_tools=0" in output, output
        assert "selected Skill requires a tool unavailable to its Agent" in output, output
        assert "owner_after_session_release=refs:2 releases:0" in output, output
        assert "run=result:0 text:agent-runtime-ok" in output, output
        assert "run_info=agent:probe.main model:ling-3.0-tiny reasoning:medium state:2 result:0 generations:1/1/1/2" in output, output
        assert "callback=calls:1 model:1 reasoning:1 system_v1:1 skill_v1:1 skill_v2:0 memory:1 instructions_v1:1" in output, output
        assert "prompt_initial=1" in output, output
        assert "prompt_recovered=old:1 new:0" in output, output
        assert "prompt_after_clear=old:1 new:0" in output, output
        assert "prompt_after_truncate=old:1 new:0" in output, output
        assert "prompt_new=old:0 new:1" in output, output
        assert "probe-agent-release-v1" in output, output
        assert "owner_after_run_destroy=refs:1 retains:1 releases:1" in output, output
        assert "probe_done=1" in output, output
        disabled_site = base / "site-memory-disabled"
        write_site(disabled_site, memory_enabled=False)
        disabled = run_probe(host, disabled_site, base / "home-memory-disabled")
        assert "init_error=" not in disabled, disabled
        assert "generations:1/1/1/0" in disabled, disabled
        assert "callback=calls:1 model:1 reasoning:1 system_v1:1 skill_v1:1 skill_v2:0 memory:0 instructions_v1:1" in disabled, disabled
        assert "prompt_recovered=old:1 new:0" in disabled, disabled
        assert "prompt_after_clear=old:1 new:0" in disabled, disabled
        assert "prompt_after_truncate=old:1 new:0" in disabled, disabled
        assert "prompt_new=old:0 new:1" in disabled, disabled
        assert "probe_done=1" in disabled, disabled
    print("agent runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
