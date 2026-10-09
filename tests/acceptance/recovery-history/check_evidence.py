"""Check serial native/UI observations and eleven exact clean-pack sources."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ("include/mdo/agents.h", "include/mdo/sessions.h", "src/agents/runtime.c",
         "src/api/recovery.c", "src/runs/manager.c", "src/sessions/conversation_page.inc.c",
         "web/js/app.js", "web/js/features/approvals/recovery-decisions.js",
         "web/js/features/chat/automatic-recovery.js", "web/js/features/chat/recovery-dock.js",
         "web/js/state/recovery.js")


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8"))


def candidate(exe: Path):
    spec = importlib.util.spec_from_file_location("recovery_pack_reader",
        HERE.parent / "decision-write-recovery/check_candidate.py")
    reader = importlib.util.module_from_spec(spec); spec.loader.exec_module(reader)
    receipt = json.loads(exe.with_name("build.json").read_text(encoding="utf-8"))
    raw = exe.read_bytes()
    assert receipt["sha256"] == hashlib.sha256(raw).hexdigest()
    assert receipt["bytes"] == len(raw) and not receipt["mdo_worktree_dirty"]
    assert receipt["xs_commit"] == json.loads((ROOT / "deps.lock").read_text(encoding="utf-8"))["xserver"]["commit"]
    with tempfile.TemporaryDirectory() as directory:
        archive = Path(directory) / "app.xrtpack"; reader.extract_pack(exe, archive)
        entries = reader.files_from_pack(archive.read_bytes())
    sources = {}
    for name in FILES:
        expected = (ROOT / "app" / name).read_bytes()
        assert entries[name] == expected, name
        sources[name] = {"sha256": hashlib.sha256(expected).hexdigest(), "matches": True}
    assert not any("recovery-history.c" in name or "__probe" in name for name in entries)
    value = {key: receipt[key] for key in ("mdo_commit", "xs_commit", "sha256", "bytes",
        "mdo_worktree_dirty", "packaged_app_sha256")}
    value.update(sources=sources, passed=True, ui_is_source_fixture=True,
        daily_app_overwritten=False, android_device_tested=False,
        source_fixture_model_network_requests=0, external_provider_tested=False)
    (HERE / "candidate.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.exe: candidate(args.exe.resolve())
    native = read("native.json"); histories = native["histories"]
    assert len(histories) == 3 and len({d["history_epoch"] for d in histories}) == 3
    assert histories[1]["items"][0]["turn"] == histories[2]["items"][0]["turn"] == 1
    assert histories[1]["recovery_token"] != histories[2]["recovery_token"]
    assert all(d["automatic_resume"] for d in histories)
    assert native["completed_reads"] == 4 and native["stale_history_token_status"] == 409
    assert native["stale_history_token_changed_ledger"] is False
    assert native["stopped"]["automatic_resume"] is False
    assert native["stable_identity_control"]["automatic_resume"] is True
    assert native["unsupported_identity"]["automatic_resume"] is False
    assert native["unsupported_identity_manual_resume"] is True
    ui = read("ui.json")
    for key, reads in (("first_completed", 1), ("clear_completed", 2), ("truncate_completed", 3), ("manual_completed", 4)):
        assert ui[key]["reads"] == reads and ui[key]["ok"]
    assert ui["after_truncate_ui"] == {"draft": "下一步继续：保留我的输入 🌱",
        "recovery_visible": False, "reply_complete": True, "unrelated_purge_error": False}
    assert ui["stopped_automatic_reads"] == 0 and ui["manual_continue_clicks"] == 1
    assert ui["copied_source_fixture"] and ui["provider_requests"] == 0
    pack = read("candidate.json"); assert pack["passed"]
    for name in ("packed-recovery.json", "packed-edit.json", "packed-compaction.json"):
        record = read(name)
        assert record["passed"] and record["packed_sha256"] == pack["sha256"], name
    print("PASS native/UI recovery, stale-token refusal, user stop, eleven packed sources, and three clean runtime probes")


if __name__ == "__main__": main()
