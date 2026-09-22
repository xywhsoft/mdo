"""Bounded schedule persistence, claim, completion, and recovery probe."""

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
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

#include "src/storage/home.c"
#include "src/config/config.c"
#include "src/schedules/manager.c"

static void PrintCatalog(const char *label) {
    xwork_error error;
    MdoScheduleCatalog *catalog = MdoScheduleCatalogSnapshot(&error);
    MdoScheduleInfo info;
    printf("%s=count:%zu diagnostics:%zu generation:%llu enabled:%d code:%d\n",
        label, MdoScheduleCatalogCount(catalog),
        MdoScheduleCatalogDiagnosticCount(catalog),
        (unsigned long long)MdoScheduleCatalogGeneration(catalog),
        MdoScheduleManagerEnabled() ? 1 : 0, (int)error.eCode);
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (MdoScheduleCatalogAt(catalog, 0u, &info))
        printf("schedule=id:%s revision:%llu runtime:%llu next:%lld claims:%llu active:%zu runnable:%d\n",
            info.Id, (unsigned long long)info.Revision,
            (unsigned long long)info.RuntimeGeneration,
            (long long)info.NextOccurrenceAt,
            (unsigned long long)info.ClaimCount, info.ActiveRuns,
            info.Runnable ? 1 : 0);
    MdoScheduleCatalogRelease(catalog);
}

void ServiceInit(XS_HostInfo *host) {
    const int64 start = 1700000000000000LL;
    xwork_runtime_config runtime_config;
    xwork_runtime *runtime = NULL;
    MdoScheduleCreateOptions options;
    MdoScheduleInfo info;
    MdoScheduleClaim claim;
    xwork_error error;
    char long_label[300];
    static const char invalid[] = "{}";
    bool result;
    (void)host;

    if (!MdoHomeInit() || !MdoConfigInit()) {
        printf("init_error=pre-runtime\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoScheduleManagerInit(runtime)) {
        printf("init_error=runtime\n"); goto done;
    }
    PrintCatalog("catalog_empty");
    if (getenv("MDO_SCHEDULE_EMPTY_ONLY") != NULL) {
        printf("probe_done=1\n"); goto done;
    }

    MdoScheduleCreateOptionsInit(&options);
    options.Id = "daily-review";
    options.Label = "Daily review";
    options.Notify = "desktop";
    options.ProjectId = "project-alpha";
    options.AgentId = "reviewer";
    options.ModelId = "ling-3.0-tiny";
    options.Protocol = MDO_MODEL_PROTOCOL_OPENAI_RESPONSES;
    options.ReasoningEffort = "medium";
    options.MaxOutputTokens = 2048u;
    options.WorkspaceRoot = "D:/work/project-alpha";
    options.Input = "review the private release notes";
    options.StartAt = start;
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoScheduleCreate(&options, &info, &error)) {
        printf("create_error=%s\n", error.sMessage); goto done;
    }
    printf("created=id:%s revision:%llu next:%lld protocol:%d\n", info.Id,
        (unsigned long long)info.Revision, (long long)info.NextOccurrenceAt,
        (int)info.Protocol);

    memset(long_label, 'x', sizeof(long_label)); long_label[sizeof(long_label)-1u] = '\0';
    options.Id = "truncated-label"; options.Label = long_label;
    result = MdoScheduleCreate(&options, NULL, &error);
    printf("overlong_create=%d code:%d\n", result ? 1 : 0, (int)error.eCode);

    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    result = MdoScheduleSetEnabled("daily-review", 0u, false, &info, &error);
    printf("stale_update=%d code:%d\n", result ? 1 : 0, (int)error.eCode);
    if (!MdoScheduleSetEnabled("daily-review", 1u, false, &info, &error) ||
        !MdoScheduleSetEnabled("daily-review", 2u, true, &info, &error)) {
        printf("enable_error=%s\n", error.sMessage); goto done;
    }
    printf("enabled=revision:%llu value:%d runnable:%d\n",
        (unsigned long long)info.Revision, info.Enabled ? 1 : 0,
        info.Runnable ? 1 : 0);

    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start - 1, &claim, &error)) {
        printf("early_claim_error=%s\n", error.sMessage); goto done;
    }
    printf("early_claim=claimed:%d wake:%lld\n", claim.Claimed ? 1 : 0,
        (long long)claim.NextWakeAt);
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start, &claim, &error) || !claim.Claimed) {
        printf("claim_error=%s\n", error.sMessage); goto done;
    }
    printf("claimed=id:%s task:%llu occurrence:%lld revision:%llu agent:%s model:%s input:%s\n",
        claim.ScheduleId, (unsigned long long)claim.TaskId,
        (long long)claim.OccurrenceAt,
        (unsigned long long)claim.DefinitionRevision, claim.AgentId,
        claim.ModelId, claim.Input);
    if (!MdoScheduleFinishTask(claim.TaskId, XWORK_RESULT_OK,
            "review completed", &error)) {
        printf("finish_error=%s\n", error.sMessage); goto done;
    }
    printf("finished=1\n");
    PrintCatalog("catalog_claimed");

    MdoScheduleManagerUnit();
    xworkRuntimeRelease(runtime); runtime = NULL;
    if (!MdoHomeAtomicWrite("schedules/broken.json", invalid,
            sizeof(invalid) - 1u, false)) {
        printf("corrupt_error=write\n"); goto done;
    }
    xworkRuntimeConfigInit(&runtime_config);
    runtime = xworkRuntimeCreate(&runtime_config, &error);
    if (runtime == NULL || !MdoScheduleManagerInit(runtime)) {
        printf("recover_error=%s\n", error.sMessage); goto done;
    }
    PrintCatalog("catalog_recovered");
    MdoScheduleClaimInit(&claim);
    if (!MdoScheduleClaimDue(start + 1000000LL, &claim, &error)) {
        printf("exhausted_error=%s\n", error.sMessage); goto done;
    }
    printf("exhausted=claimed:%d wake:%lld\n", claim.Claimed ? 1 : 0,
        (long long)claim.NextWakeAt);
    printf("probe_done=1\n");
done:
    MdoScheduleManagerUnit();
    xworkRuntimeRelease(runtime);
    MdoConfigUnit();
    MdoHomeUnit();
}

void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def write_site(site: Path) -> None:
    for relative in ("web", "default-home/config", "src/storage", "src/config",
                     "src/schedules", "include/mdo"):
        (site / relative).mkdir(parents=True, exist_ok=True)
    (site / "web/index.html").write_text("probe", encoding="utf-8")
    for relative in (
        "default-home/config/defaults.json",
        "src/storage/home.c",
        "src/config/config.c",
        "src/schedules/manager.c",
    ):
        shutil.copy2(ROOT / "app" / relative, site / relative)
    for name in ("home.h", "config.h", "models.h", "schedules.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name, site / "include/mdo" / name)
    (site / "probe.c").write_text(PROBE_SOURCE, encoding="utf-8")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    (site / "xs.json").write_text(json.dumps({"services": [{
        "enabled": True, "class": "http", "name": "schedule-probe",
        "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "probe", "path": "web",
                         "devlang": "c", "devfile": "probe.c"},
    }]}), encoding="utf-8")


def run_probe(host: Path, site: Path, home: Path, empty_only: bool = False) -> str:
    env = os.environ.copy()
    if empty_only:
        env["MDO_SCHEDULE_EMPTY_ONLY"] = "1"
    process = subprocess.Popen(
        [str(host), "xs.json", "--", "--home", str(home)], cwd=site,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        encoding="utf-8", errors="replace", env=env,
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
    with tempfile.TemporaryDirectory(prefix="schedule-runtime-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        empty_home = base / "empty-home"
        home = base / "home"
        write_site(site)
        empty_output = run_probe(host, site, empty_home, empty_only=True)
        assert "catalog_empty=count:0 diagnostics:0 generation:1 enabled:1 code:0" in empty_output, empty_output
        assert "probe_done=1" in empty_output, empty_output
        assert not empty_home.exists(), list(empty_home.rglob("*")) if empty_home.exists() else ""

        output = run_probe(host, site, home)
        assert "init_error=" not in output, output
        assert "create_error=" not in output, output
        assert "enable_error=" not in output, output
        assert "claim_error=" not in output, output
        assert "finish_error=" not in output, output
        assert "recover_error=" not in output, output
        assert "catalog_empty=count:0 diagnostics:0 generation:1 enabled:1 code:0" in output, output
        assert "created=id:daily-review revision:1 next:1700000000000000 protocol:2" in output, output
        assert "overlong_create=0 code:1" in output, output
        assert "stale_update=0 code:7" in output, output
        assert "enabled=revision:3 value:1 runnable:1" in output, output
        assert "early_claim=claimed:0 wake:1700000000000000" in output, output
        assert "claimed=id:daily-review" in output and "occurrence:1700000000000000 revision:4" in output, output
        assert "agent:reviewer model:ling-3.0-tiny input:review the private release notes" in output, output
        assert "finished=1" in output, output
        assert "catalog_claimed=count:1 diagnostics:0" in output, output
        assert "revision:4" in output and "next:0 claims:1 active:0 runnable:1" in output, output
        assert "catalog_recovered=count:1 diagnostics:1 generation:1 enabled:1 code:0" in output, output
        assert "exhausted=claimed:0 wake:0" in output, output
        assert "probe_done=1" in output, output

        definition = json.loads((home / "schedules/daily-review.json").read_text(
            encoding="utf-8"))
        assert definition["schema_version"] == 1
        assert definition["revision"] == 4
        assert definition["claim_count"] == 1
        assert definition["next_occurrence_at_us"] == 0
        assert definition["protocol"] == "openai-responses"
        assert definition["input"] == "review the private release notes"
        history = [json.loads(line) for line in
                   (home / "schedules/history/daily-review.jsonl").read_text(
                       encoding="utf-8").splitlines()]
        assert len(history) == 1 and history[0]["text"] == "review completed"
        audit_lines = (home / "schedules/audit.jsonl").read_text(
            encoding="utf-8").splitlines()
        audit = [json.loads(line) for line in audit_lines]
        assert [item["operation"] for item in audit] == [
            "create", "set-enabled", "set-enabled", "claim"]
        assert all(item["phase"] == "prepared" for item in audit)
        assert all("input" not in item for item in audit)
        assert audit[0]["input_sha256"] == hashlib.sha256(
            b"review the private release notes").hexdigest()
        assert "review the private release notes" not in "\n".join(audit_lines)
    print("schedule runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
