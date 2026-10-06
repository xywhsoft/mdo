"""Bounded MDO-4 Skill catalog probe through the real xs/TCC runtime."""

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


from runtime_sources import copy_app_source


ROOT = Path(__file__).resolve().parent.parent

EXTERNAL_V1 = '''---
name: "External Explorer"
description: 'External reference fixture'
version: 2.0.0
tools: [ls, "grep"]
mcp: []
permissions: [workspace.read]
scripts: [scripts/run.txt]
templates: [templates/stale.txt]
assets: []
---
external-body-v1
'''

EXTERNAL_V2 = EXTERNAL_V1.replace("2.0.0", "2.1.0").replace(
    "external-body-v1", "external-body-v2")

INVALID_EXTERNAL = '''---
name: Broken
description: Must not reveal the built-in Skill.
name: duplicate-name
---
broken
'''

MISSING_RESOURCE = '''---
name: Missing Resource
description: Declares a file that does not exist.
scripts: [scripts/missing.txt]
---
missing-resource
'''


def c_literal(value: str) -> str:
    return json.dumps(value)


PROBE_SOURCE = rf'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/skills/manager.c"

static const char sExternalV1[] = {c_literal(EXTERNAL_V1)};
static const char sExternalV2[] = {c_literal(EXTERNAL_V2)};
static const char sInvalid[] = {c_literal(INVALID_EXTERNAL)};
static const char sMissing[] = {c_literal(MISSING_RESOURCE)};

static void PrintError(const char *label) {{
    const xerror *error = xrtGetError();
    printf("%s=%s system:%d\n", label,
        error != NULL ? xrtErrorMessage(error) : "missing-error",
        error != NULL ? (int)xrtErrorSystemCode(error) : 0);
    xrtClearError();
}}

static void PrintFirst(MdoSkillCatalog *catalog, const char *label) {{
    MdoSkillInfo info;
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    if (MdoSkillCatalogAt(catalog, 0u, &info))
        printf("%s=id:%s external:%d trust:%d version:%s resources:%zu tools:%zu tokens:%zu\n",
            label, info.Id, info.External ? 1 : 0, (int)info.Trust,
            info.Version != NULL ? info.Version : "null", info.ResourceCount,
            info.RequiredToolCount, info.EstimatedTokens);
    else printf("%s=none\n", label);
}}

static void PrintBody(MdoSkillCatalog *catalog, const char *label) {{
    MdoSkillContent content;
    memset(&content, 0, sizeof(content));
    content.Size = sizeof(content);
    if (MdoSkillCatalogLoadBody(catalog, "project-explorer", &content)) {{
        printf("%s=external:%d trust:%d text:%s\n", label,
            content.External ? 1 : 0, (int)content.Trust, content.Text);
        MdoSkillContentUnit(&content);
    }} else PrintError(label);
}}

static void PrintResource(MdoSkillCatalog *catalog, const char *path,
    const char *label) {{
    MdoSkillResourceContent content;
    memset(&content, 0, sizeof(content));
    content.Size = sizeof(content);
    if (MdoSkillCatalogLoadResource(catalog, "project-explorer", path,
            4096u, &content)) {{
        printf("%s=kind:%d external:%d text:%s\n", label,
            (int)content.Kind, content.External ? 1 : 0,
            (const char*)content.Data);
        MdoSkillResourceContentUnit(&content);
    }} else PrintError(label);
}}

static void PrintDiagnostics(const char *label) {{
    MdoSkillDiagnostics *diagnostics = MdoSkillDiagnosticsSnapshot();
    MdoSkillDiagnosticInfo info;
    memset(&info, 0, sizeof(info));
    info.Size = sizeof(info);
    printf("%s_count=%zu\n", label, MdoSkillDiagnosticsCount(diagnostics));
    if (MdoSkillDiagnosticsAt(diagnostics, 0u, &info))
        printf("%s=stage:%d id:%s message:%s\n", label, (int)info.Stage,
            info.SkillId != NULL ? info.SkillId : "null",
            info.Message != NULL ? info.Message : "null");
    MdoSkillDiagnosticsRelease(diagnostics);
}}

void ServiceInit(XS_HostInfo *host) {{
    MdoSkillCatalog *builtin = NULL;
    MdoSkillCatalog *external_v1 = NULL;
    MdoSkillCatalog *external_v2 = NULL;
    MdoSkillCatalog *catalog = NULL;
    MdoSkillResourceContent undeclared;
    (void)host;
    if (!MdoHomeInit() || !MdoSkillManagerInit()) {{
        PrintError("skill_init_error"); goto done;
    }}
    builtin = MdoSkillCatalogSnapshot();
    printf("initial_generation=%llu count=%zu\n",
        (unsigned long long)MdoSkillManagerGeneration(),
        MdoSkillCatalogCount(builtin));
    PrintFirst(builtin, "initial_skill");
    PrintBody(builtin, "builtin_body");
    PrintResource(builtin, "templates/report.md", "builtin_template");

    if (!MdoHomeAtomicWrite("skills/project-explorer/scripts/run.txt",
            "script-v1", 9u, false) ||
        !MdoHomeAtomicWrite("skills/project-explorer/templates/stale.txt",
            "stale-v1", 8u, false) ||
        !MdoHomeAtomicWrite("skills/project-explorer/SKILL.md", sExternalV1,
            strlen(sExternalV1), false) || !MdoSkillManagerReload()) {{
        PrintError("external_v1_error"); goto done;
    }}
    external_v1 = MdoSkillCatalogSnapshot();
    printf("external_v1_generation=%llu count=%zu old_count=%zu\n",
        (unsigned long long)MdoSkillManagerGeneration(),
        MdoSkillCatalogCount(external_v1), MdoSkillCatalogCount(builtin));
    PrintFirst(external_v1, "external_v1_skill");
    PrintBody(external_v1, "external_v1_body");
    PrintResource(external_v1, "scripts/run.txt", "external_v1_script");
    memset(&undeclared, 0, sizeof(undeclared));
    undeclared.Size = sizeof(undeclared);
    printf("undeclared_resource=%d\n",
        MdoSkillCatalogLoadResource(external_v1, "project-explorer",
            "assets/not-declared.txt", 4096u, &undeclared) ? 1 : 0);
    if (xrtGetError() != NULL) PrintError("undeclared_error");

    if (!MdoHomeAtomicWrite("skills/project-explorer/scripts/run.txt",
            "script-v2", 9u, false) ||
        !MdoHomeAtomicWrite("skills/project-explorer/templates/stale.txt",
            "stale-v2", 8u, false) ||
        !MdoHomeAtomicWrite("skills/project-explorer/SKILL.md", sExternalV2,
            strlen(sExternalV2), false) || !MdoSkillManagerReload()) {{
        PrintError("external_v2_error"); goto done;
    }}
    external_v2 = MdoSkillCatalogSnapshot();
    PrintBody(external_v2, "external_v2_body");
    PrintResource(external_v2, "scripts/run.txt", "external_v2_script");
    PrintResource(external_v2, "templates/stale.txt", "external_v2_template");
    PrintBody(external_v1, "pinned_v1_body");
    PrintResource(external_v1, "scripts/run.txt", "pinned_v1_script");
    PrintResource(external_v1, "templates/stale.txt", "stale_v1_template");

    if (!MdoHomeAtomicWrite("skills/project-explorer/SKILL.md", sInvalid,
            strlen(sInvalid), false) || !MdoSkillManagerReload()) {{
        PrintError("invalid_publish_error"); goto done;
    }}
    catalog = MdoSkillCatalogSnapshot();
    printf("invalid_generation=%llu count=%zu\n",
        (unsigned long long)MdoSkillManagerGeneration(),
        MdoSkillCatalogCount(catalog));
    PrintDiagnostics("invalid_diagnostic");
    MdoSkillCatalogRelease(catalog); catalog = NULL;

    if (!MdoHomeAtomicWrite("skills/project-explorer/SKILL.md", sMissing,
            strlen(sMissing), false) || !MdoSkillManagerReload()) {{
        PrintError("missing_publish_error"); goto done;
    }}
    catalog = MdoSkillCatalogSnapshot();
    printf("missing_generation=%llu count=%zu\n",
        (unsigned long long)MdoSkillManagerGeneration(),
        MdoSkillCatalogCount(catalog));
    PrintDiagnostics("missing_diagnostic");
done:
    MdoSkillCatalogRelease(catalog);
    MdoSkillCatalogRelease(external_v2);
    MdoSkillCatalogRelease(external_v1);
    MdoSkillCatalogRelease(builtin);
    MdoSkillManagerUnit();
    MdoHomeUnit();
    printf("probe_done=1\n");
}}

void ServiceUnit(XS_HostInfo *host) {{ (void)host; }}
'''


def write_site(site: Path) -> None:
    for relative in (
        "web", "default-home/config", "default-home/skills",
        "src/storage", "src/skills", "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    shutil.copy2(ROOT / "app/default-home/config/defaults.json",
                 site / "default-home/config/defaults.json")
    shutil.copytree(ROOT / "app/default-home/skills",
                    site / "default-home/skills", dirs_exist_ok=True)
    for relative in (
        "src/storage/home.c", "src/storage/home_import.inc.c", "src/storage/home_purge.inc.c", "src/storage/home_restore.inc.c", "src/skills/manager.c",
        "include/mdo/home.h", "include/mdo/home_import.h", "include/mdo/home_purge.h", "include/mdo/home_restore.h", "include/mdo/session_file_policy.h", "include/mdo/skills.h",
    ):
        copy_app_source(relative, site)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True, "class": "http", "name": "skill-probe",
            "ip": "127.0.0.1", "port": port,
            "host_default": {
                "enabled": True, "name": "probe", "path": "web",
                "devlang": "c", "devfile": "probe.c",
            },
        }],
    }), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path) -> str:
    command = [str(host), "xs.json", "--", "--home", str(home)]
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        command, cwd=site, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
        creationflags=creationflags,
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
    done.wait(timeout=15.0)
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
    default = ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs")
    parser.add_argument("--host", type=Path, default=default)
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="skill-runtime-",
                                     dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        write_site(site)
        output = run_probe(host, site, base / "home")
        assert "skill_init_error=" not in output, output
        assert "initial_generation=1 count=1" in output, output
        assert "initial_skill=id:project-explorer external:0 trust:0" in output, output
        assert "builtin_body=external:0 trust:0" in output, output
        assert "# Project Explorer" in output, output
        assert "builtin_template=kind:2 external:0" in output, output
        assert "external_v1_generation=2 count=1 old_count=1" in output, output
        assert "external_v1_skill=id:project-explorer external:1 trust:1 version:2.0.0" in output, output
        assert "external_v1_body=external:1 trust:1 text:external-body-v1" in output, output
        assert "external_v1_script=kind:1 external:1 text:script-v1" in output, output
        assert "undeclared_resource=0" in output, output
        assert "external_v2_body=external:1 trust:1 text:external-body-v2" in output, output
        assert "external_v2_script=kind:1 external:1 text:script-v2" in output, output
        assert "external_v2_template=kind:2 external:1 text:stale-v2" in output, output
        assert "pinned_v1_body=external:1 trust:1 text:external-body-v1" in output, output
        assert "pinned_v1_script=kind:1 external:1 text:script-v1" in output, output
        assert "stale_v1_template=Skill resource changed; reload the Skill catalog" in output, output
        assert "invalid_generation=4 count=0" in output, output
        assert "invalid_diagnostic_count=1" in output, output
        assert "invalid_diagnostic=stage:3 id:project-explorer" in output, output
        assert "missing_generation=5 count=0" in output, output
        assert "missing_diagnostic_count=1" in output, output
        assert "missing_diagnostic=stage:4 id:project-explorer" in output, output
        assert "probe_done=1" in output, output
    print("skill runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
