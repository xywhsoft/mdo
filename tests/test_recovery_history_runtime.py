"""Serial native read/recovery/clear/truncate probe; no provider or load test."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_interrupt_runtime import ROOT, free_port, request, start_host, stop_host, until


def prepare_fixture(base: Path):
    site = base / "site"; home = base / "home"; workspace = base / "workspace"
    workspace.mkdir(); (workspace / "input.txt").write_text("One safe read", encoding="utf-8")
    shutil.copytree(ROOT / "app", site)
    shutil.copy2(ROOT / "tests/fixtures/recovery-history.c", site / "recovery-history.c")
    # Exercise a filesystem without a stable file ID in the copied reader.
    # No product switch, persistence change or real disk error is introduced.
    projection = site / "src/sessions/conversation_page.inc.c"
    text = projection.read_text(encoding="utf-8")
    needle = "    if (!Reader || !MdoViewEpoch(Reader, Page->Epoch)) goto io;"
    assert text.count(needle) == 1
    text = text.replace(needle, "    if (Reader && g_RecoveryHistoryNoIdentity) Reader->Info.Available &= ~XFILE_INFO_IDENTITY;\n" + needle)
    projection.write_text(text, encoding="utf-8")
    (site / "probe.c").write_text(
        "static int g_RecoveryHistoryNoIdentity;\n"
        "#define RECOVERY_HISTORY_WORKSPACE " + json.dumps(str(workspace.resolve())) + "\n"
        "#define ServiceInit RecoveryHistoryProductInit\n"
        "#define ServiceUnit RecoveryHistoryProductUnit\n"
        "#define RequestProc RecoveryHistoryProductRequest\n"
        '#include "generated/mdo_unity.c"\n'
        "#undef ServiceInit\n#undef ServiceUnit\n#undef RequestProc\n"
        '#include "recovery-history.c"\n', encoding="utf-8")
    port = free_port(); config = site / "xs.json"
    config.write_text(json.dumps({"engine": {"workers": 1}, "services": [{"enabled": True,
        "class": "http", "name": "recovery-history", "ip": "127.0.0.1", "port": port,
        "host_default": {"enabled": True, "name": "mdo", "path": "web", "devlang": "c", "devfile": "probe.c"}}]}), encoding="utf-8")
    return site, home, port, config


def run(host: Path, output: Path | None = None) -> None:
    with tempfile.TemporaryDirectory(prefix="recovery-history-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site, home, port, config = prepare_fixture(base)
        process = None; log = base / "xs.log"; samples = []; resumes = 0
        recovery_path = "/api/v1/projects/default/sessions/history-recovery/recovery"
        def inspect():
            status, reply = request(port, "GET", recovery_path)
            assert status == 200, reply
            return reply["data"]
        def resume(data):
            nonlocal resumes
            resumes += 1
            status, reply = request(port, "POST", recovery_path.removesuffix("recovery") + "resume", {
                "recovery_token": data["recovery_token"],
                "client_resume_id": f"{resumes:032x}",
                "decisions": [{"tool_call_id": data["items"][0]["tool_call_id"], "action": "retry"}]})
            assert status == 202, reply
            run_id = reply["data"]["id"]
            def completed():
                status, reply = request(port, "GET", f"/api/v1/runs/{run_id}")
                assert status == 200, reply
                return reply["data"] if reply["data"]["terminal"] else None
            result = until(completed)
            assert result["result"] == "ok", result
            assert inspect()["resume_required"] is False
        try:
            process = start_host(host, config, home, os.environ.copy(), log, port)
            for action in (None, "POST", "DELETE"):
                if action:
                    status, reply = request(port, action, "/__probe/history")
                    assert status == 200 and reply["data"]["ok"], reply
                data = inspect(); samples.append(data)
                assert data["automatic_resume"] and data["resume_required"], data
                assert data["items"][0]["turn"] > 0, data
                assert len(data["history_epoch"]) == 64, data
                assert inspect()["history_epoch"] == data["history_epoch"]
                if action == "DELETE":
                    # Native admission must reject the old token, not merely
                    # rely on the page to discard its older reviewed choices.
                    folder = home / "sessions/default/history-recovery"
                    files = lambda: {p.name: p.read_bytes() for p in folder.iterdir()
                                     if p.name in ("snapshot.json", "journal.jsonl", "meta.json", "ui-events.jsonl")}
                    before = files()
                    status, reply = request(port, "POST", recovery_path.removesuffix("recovery") + "resume", {
                        "recovery_token": samples[-2]["recovery_token"],
                        "decisions": [{"tool_call_id": data["items"][0]["tool_call_id"], "action": "retry"}]})
                    assert status == 409 and reply["error"]["code"] == "recovery_state_conflict", reply
                    assert files() == before
                resume(data)
            assert len({data["history_epoch"] for data in samples}) == 3
            assert samples[1]["items"][0]["turn"] == samples[2]["items"][0]["turn"] == 1
            assert samples[1]["recovery_token"] != samples[2]["recovery_token"]
            # The real frontend admission logic consumes the native replies.
            js = ('import {createAutomaticRecovery} from "./app/web/js/features/chat/automatic-recovery.js";'
                'const a=createAutomaticRecovery(), s=' + json.dumps(samples) + ';'
                'for(const d of s){if(!a.claim(d,true)||a.claim({...d,recovery_token:"changed"},true))throw Error("recovery identity reused");}')
            subprocess.run(["node", "--input-type=module", "-e", js], cwd=ROOT, check=True)
            status, reply = request(port, "PATCH", "/__probe/history")
            assert status == 200 and reply["data"]["ok"], reply
            stopped = inspect()
            assert stopped["resume_required"] and not stopped["automatic_resume"]
            assert len(stopped["history_epoch"]) == 64
            status, reply = request(port, "POST", recovery_path.removesuffix("recovery") + "abandon", {
                "revision": stopped["revision"], "last_sequence": stopped["last_sequence"]})
            assert status == 200, reply
            # The same pending read must be automatic with a stable file ID,
            # and manual without one, even though its retry is still safe.
            status, reply = request(port, "POST", "/__probe/history")
            assert status == 200 and reply["data"]["ok"], reply
            eligible = inspect()
            assert eligible["automatic_resume"] and eligible["resume_required"]
            status, reply = request(port, "PUT", "/__probe/history")
            assert status == 200 and reply["data"]["reads"] == 3, reply
            unsupported = inspect()
            identity = lambda data: [{k: v for k, v in item.items() if k != "catalog_generation"}
                                     for item in data["items"]]
            assert unsupported["resume_required"] and identity(unsupported) == identity(eligible)
            assert not unsupported["automatic_resume"] and len(unsupported["history_epoch"]) == 64
            resume(unsupported)  # Manual recovery remains available.
            status, reply = request(port, "GET", "/__probe/history")
            assert status == 200 and reply["data"]["reads"] == 4, reply
            evidence = {"histories": samples, "stopped": stopped,
                "stable_identity_control": eligible, "unsupported_identity": unsupported, "completed_reads": 4,
                "stale_history_token_status": 409, "stale_history_token_changed_ledger": False,
                "unsupported_identity_manual_resume": True,
                "fixture": "serial native model/event callbacks; no model network requests"}
            if output:
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        except BaseException:
            print(log.read_text(encoding="utf-8", errors="replace") if log.exists() else "no host log")
            raise
        finally: stop_host(process)
    print("PASS automatic read recovery across clear/truncate with reused turn; one read each; user stop remains manual")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    run(args.host.resolve(), args.output)
