"""Bounded global/project memory persistence and audit probe."""

from __future__ import annotations

import argparse
import hashlib
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

PROBE_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/memory/manager.c"

static void PrintSnapshot(const char *label, MdoMemoryScope scope,
    const char *project) {
    xwork_error error;
    MdoMemorySnapshot *snapshot = MdoMemorySnapshotCreate(scope, project, &error);
    MdoMemoryEntryInfo info;
    printf("%s=ok:%d revision:%llu count:%zu generation:%llu code:%d\n",
        label, snapshot != NULL ? 1 : 0,
        (unsigned long long)MdoMemorySnapshotRevision(snapshot),
        MdoMemorySnapshotCount(snapshot),
        (unsigned long long)MdoMemorySnapshotGeneration(snapshot),
        (int)error.eCode);
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (MdoMemorySnapshotAt(snapshot, 0u, &info))
        printf("memory_item=id:%s title:%s content:%s tags:%zu pinned:%d entry_revision:%llu\n",
            info.Id, info.Title, info.Content, info.TagCount,
            info.Pinned ? 1 : 0, (unsigned long long)info.Revision);
    MdoMemorySnapshotRelease(snapshot);
}

void ServiceInit(XS_HostInfo *host) {
    static const char *tags[] = {"preference", "editor"};
    static const char invalid[] = "{}";
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    MdoMemoryWriteOptions write;
    MdoMemoryRemoveOptions remove;
    xwork_error error;
    bool result;
    (void)host;

    if (!MdoHomeInit() || !MdoConfigInit()) {
        printf("init_error=pre-runtime\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoMemoryManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    PrintSnapshot("global_empty", MDO_MEMORY_GLOBAL, NULL);

    MdoMemoryWriteOptionsInit(&write);
    write.Scope = MDO_MEMORY_GLOBAL;
    write.Id = "editor-style";
    write.Title = "Editor preference";
    write.Content = "Prefer compact diffs and explicit validation.";
    write.Tags = tags;
    write.TagCount = 2u;
    write.Pinned = true;
    write.Actor = "runtime-probe";
    write.SessionId = "session-a";
    write.Reason = "user preference";
    if (!MdoMemoryUpsert(&write, &error)) {
        printf("write_error=%s\n", error.sMessage); goto done;
    }
    PrintSnapshot("global_written", MDO_MEMORY_GLOBAL, NULL);

    write.ExpectedRevision = 0u;
    result = MdoMemoryUpsert(&write, &error);
    printf("stale_write=%d code:%d\n", result ? 1 : 0, (int)error.eCode);
    write.ExpectedRevision = UINT64_MAX;
    write.Id = "credential";
    write.Title = "Unsafe";
    write.Content = "password: should-not-persist";
    result = MdoMemoryUpsert(&write, &error);
    printf("sensitive_write=%d code:%d\n", result ? 1 : 0, (int)error.eCode);

    MdoMemoryWriteOptionsInit(&write);
    write.Scope = MDO_MEMORY_PROJECT;
    write.ProjectId = "project-alpha";
    write.Id = "build-command";
    write.Title = "Build";
    write.Content = "Use the locked xserver checkout for builds.";
    write.Actor = "runtime-probe";
    if (!MdoMemoryUpsert(&write, &error)) goto done;
    PrintSnapshot("project_written", MDO_MEMORY_PROJECT, "project-alpha");

    MdoMemoryManagerUnit();
    if (!MdoMemoryManagerInit(runtime)) goto done;
    PrintSnapshot("project_recovered", MDO_MEMORY_PROJECT, "project-alpha");

    MdoMemoryRemoveOptionsInit(&remove);
    remove.Scope = MDO_MEMORY_GLOBAL;
    remove.Id = "editor-style";
    remove.ExpectedRevision = 1u;
    remove.Actor = "runtime-probe";
    remove.Reason = "preference removed";
    if (!MdoMemoryRemove(&remove, &error)) goto done;
    PrintSnapshot("global_removed", MDO_MEMORY_GLOBAL, NULL);

    if (!MdoHomeAtomicWrite("memory/projects/broken.json", invalid,
            sizeof(invalid) - 1u, false)) goto done;
    PrintSnapshot("broken_project", MDO_MEMORY_PROJECT, "broken");
    PrintSnapshot("project_intact", MDO_MEMORY_PROJECT, "project-alpha");
    printf("probe_done=1\n");
done:
    MdoMemoryManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoConfigUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in (
        "web",
        "default-home/config",
        "src/storage",
        "src/config",
        "src/memory",
        "include/mdo",
    ):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "src/storage/home.c",
        "src/config/config.c",
        "src/memory/manager.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    for name in ("home.h", "config.h", "memory.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name, site / "include/mdo" / name)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({
        "services": [{
            "enabled": True,
            "class": "http",
            "name": "memory-probe",
            "ip": "127.0.0.1",
            "port": port,
            "host_default": {
                "enabled": True,
                "name": "probe",
                "path": "web",
                "devlang": "c",
                "devfile": "probe.c",
            },
        }],
    }), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path) -> str:
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)],
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
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    if not host.is_file():
        print(f"missing xs host: {host}", file=sys.stderr)
        return 2
    (ROOT / ".build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="memory-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        home = base / "home"
        write_site(site)
        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "write_error=" not in output, output
        assert "global_empty=ok:1 revision:0 count:0 generation:1 code:0" in output, output
        assert "global_written=ok:1 revision:1 count:1 generation:2 code:0" in output, output
        assert "memory_item=id:editor-style title:Editor preference" in output, output
        assert "tags:2 pinned:1 entry_revision:1" in output, output
        assert "stale_write=0 code:7" in output, output
        assert "sensitive_write=0 code:1" in output, output
        assert "project_written=ok:1 revision:1 count:1 generation:3 code:0" in output, output
        assert "project_recovered=ok:1 revision:1 count:1 generation:1 code:0" in output, output
        assert "global_removed=ok:1 revision:2 count:0 generation:2 code:0" in output, output
        assert "broken_project=ok:0 revision:0 count:0 generation:0" in output, output
        assert "project_intact=ok:1 revision:1 count:1 generation:2 code:0" in output, output
        assert "probe_done=1" in output, output

        global_store = json.loads((home / "memory/global.json").read_text(encoding="utf-8"))
        project_store = json.loads((home / "memory/projects/project-alpha.json").read_text(encoding="utf-8"))
        assert global_store["schema_version"] == 1 and global_store["revision"] == 2
        assert global_store["entries"] == []
        assert project_store["revision"] == 1
        assert project_store["entries"][0]["id"] == "build-command"
        audit_lines = (home / "memory/audit.jsonl").read_text(encoding="utf-8").splitlines()
        audit = [json.loads(line) for line in audit_lines]
        assert len(audit) == 3
        assert [item["operation"] for item in audit] == ["create", "create", "remove"]
        assert all(item["phase"] == "prepared" for item in audit)
        assert all("content" not in item for item in audit)
        assert audit[0]["content_sha256"] == hashlib.sha256(
            b"Prefer compact diffs and explicit validation.").hexdigest()
        assert audit[2]["content_sha256"] == ""
        serialized = "\n".join(audit_lines).lower()
        assert "password" not in serialized and "should-not-persist" not in serialized
    print("memory runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
