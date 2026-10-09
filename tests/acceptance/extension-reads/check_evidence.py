"""Independently check real read faults, UI observations and the clean pack."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8"))


def records(name):
    return [json.loads(line) for line in (HERE / name).read_text(encoding="utf-8").splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--running-copy", type=Path)
    args = parser.parse_args()
    baseline_candidate = read("baseline-candidate.json")
    assert baseline_candidate["mdo_commit"].startswith("95066ef9")
    assert not baseline_candidate["mdo_worktree_dirty"]
    baseline = read("baseline-error.json")
    assert baseline["errors"] == ["Temporary extension read unavailable"]
    baseline_late = read("baseline-late.json")
    assert not baseline_late["url"].startswith("#/settings")
    assert len(baseline_late["dialogs"]) == 1
    old_records = records("baseline-read-records.jsonl")
    assert [r["status"] for r in old_records if r["phase"] == "ready-catalog"] == [503]
    assert next(r for r in old_records if r["phase"] == "late-editor")["sent"] is True

    phases = defaultdict(list)
    for record in records("read-records.jsonl"):
        phases[record["phase"]].append(record)
    for phase in ("catalog-retry", "editor-retry", "picker-retry"):
        rows = sorted(phases[phase], key=lambda row: row["attempt"])
        assert [r["attempt"] for r in rows] == [1, 2, 3]
        assert [r.get("status") for r in rows] == [503, 503, None]
        assert rows[-1]["forwarded"]
        assert rows[1]["started"] - rows[0]["started"] >= .49
        assert rows[2]["started"] - rows[1]["started"] >= .99
    for name in ("retrying-ui.json", "recovered-ui.json"):
        ui = read(name); assert ui["errors"] == [] and ui["toasts"] == []
    assert read("recovered-ui.json")["names"] == ["Default"]
    editor = read("editor-recovered-ui.json")
    assert editor == {"dialogTitle": "编辑 Agent", "id": "default", "toasts": []}
    picker = read("picker-ui.json")
    assert picker["selected"] == ["read"] and picker["toolCount"] >= 20
    assert picker["statuses"] == ["", ""] and picker["toasts"] == []
    saved = read("subagent.json")["data"]
    assert saved["valid"] is True and saved["enabled"] is True
    assert 'tools: ["read"]' in saved["content"] and "read_only: true" in saved["content"]

    rows = sorted(phases["catalog-final"], key=lambda row: row["attempt"])
    assert [r["attempt"] for r in rows] == list(range(1, 8))
    assert all(r["status"] == 503 for r in rows[:6]) and rows[-1]["forwarded"]
    assert rows[5]["started"] - rows[0]["started"] >= 15.49
    assert read("final-ui.json") == {"errors": ["Temporary extension read unavailable"],
        "errorToasts": [], "dialogs": 0}
    assert read("final-recovered-ui.json") == {"errors": [], "errorToasts": []}
    for phase in ("late-editor", "new-intent-fast"):
        assert len(phases[phase]) == 1 and phases[phase][0]["peer_closed"] is True
        held = read(f"held-{phase}.json")
        assert held["status"] == 200 and held["response"]["data"]["id"] == "default"
    late = read("late-ui.json")
    assert late["dialogs"] == 0 and late["toasts"] == [] and late["draft"] == "EXTENSION_READ_FOLLOWUP"
    new = read("new-intent-ui.json")
    assert new == {"title": "新建 Agent", "id": "qa-new-intent", "idDisabled": False,
        "prompt": "Keep the new editor and its instructions.", "errorToasts": []}
    assert read("new-agent.json")["data"]["content"].endswith(new["prompt"])

    ui = read("followup-ui.json")
    assert ui["dialogs"] == 0 and ui["draft"] == "" and not ui["errorToasts"]
    assert ui["timeline"].count("FIXTURE_OK: EXTENSION_READ_FOLLOWUP") == 1
    assert read("calls.json") == {"EXTENSION_READ_FOLLOWUP": 1}
    assert read("requests.json") == [{"prompt": "EXTENSION_READ_FOLLOWUP",
        "image_parts": 0, "user_messages": 1, "tool_results": 0}]
    events = read("followup-events.json")
    assert [e["text"] for e in events if e["kind"] == "agent_start"] == ["EXTENSION_READ_FOLLOWUP"]
    assert sum(e["kind"] == "agent_done" and e["success"] for e in events) == 1
    assert not any(e["kind"] == "error" for e in events)

    receipt = read("build.json")
    runtime = read("runtime.json")
    assert not receipt["mdo_worktree_dirty"] and receipt["mdo_commit"].startswith("b242ca66")
    assert runtime["passed"] and runtime["packed_sha256"] == receipt["sha256"]
    assert runtime["final_model_errors"] == 0 and sum(runtime["model_requests"].values()) == 16
    assert runtime["mcp_2025_methods"] == ["initialize", "notifications/initialized", "tools/list", "tools/call"]
    sources = {}
    if args.exe:
        raw = args.exe.read_bytes()
        assert len(raw) == receipt["bytes"] and hashlib.sha256(raw).hexdigest() == receipt["sha256"]
        if args.running_copy: assert args.running_copy.read_bytes() == raw
        helpers = runpy.run_path(str(HERE.parent / "decision-write-recovery/check_candidate.py"))
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "app.xrtpack"
            helpers["extract_pack"](args.exe, archive)
            entries = helpers["files_from_pack"](archive.read_bytes())
        for name in ("web/js/app.js", "web/js/features/settings/extension-panels.js", "web/js/features/settings/extension-reads.js"):
            expected = (ROOT / "app" / name).read_bytes(); assert entries[name] == expected
            sources[name] = hashlib.sha256(expected).hexdigest()
        assert not any("manual_extension_read_qa" in name or "control.json" == name for name in entries)
    report = {"passed": True, "mdo_commit": receipt["mdo_commit"], "sha256": receipt["sha256"],
        "baseline_sha256": baseline_candidate["sha256"],
        "bytes": receipt["bytes"], "embedded_sources": sources, "page_model_requests": 1,
        "native_extension_model_requests": 16, "final_model_errors": 0,
        "catalog_final_notices": 1, "external_provider_requests": 0,
        "android_device_tested": False, "daily_app_overwritten": False}
    (HERE / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS baseline reproduction, bounded real read faults, page intent and native extension execution")


if __name__ == "__main__": main()
