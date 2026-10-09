"""Check actual page observations, retained native events and clean-pack sources."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ("web/js/features/chat/timeline.js", "web/js/features/chat/conversation-history.js")
DRAFT = "保留这份草稿：继续检查 🌱"


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8"))


def candidate(exe):
    spec = importlib.util.spec_from_file_location("pack_reader",
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
    assert not any("__probe/history" in value.decode("utf-8", errors="ignore")
                   for name, value in entries.items() if name.endswith(".c"))
    record = {key: receipt[key] for key in ("mdo_commit", "xs_commit", "sha256", "bytes",
        "mdo_worktree_dirty", "packaged_app_sha256")}
    record.update(sources=sources, passed=True, external_provider_tested=False,
        android_device_tested=False, daily_app_overwritten=False)
    (HERE / "candidate.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.exe: candidate(args.exe.resolve())
    pack = read("candidate.json"); assert pack["passed"]
    ui = read("packed-ui.json")
    assert ui["packed_sha256"] == pack["sha256"]
    assert ui["unchanged_packed_application"] and ui["explicit_resume_clicks"] == 2
    assert ui["external_provider_requests"] == 0
    for name in ("firstFailure", "secondFailure"):
        state = ui[name]
        assert len(state["errors"]) == 1 and "模型额度不足" in state["errors"][0]
        assert "北京时间 00:00" in state["errors"][0]
        assert state["draft"] == DRAFT and not state["publicRecovery"]
    assert ui["secondFailure"]["collapsedProcesses"] == [True]
    for name in ("success", "reload"):
        state = ui[name]
        assert state["errors"] == state["publicRecovery"] == []
        assert state["draft"] == DRAFT and state["reply"] == "FIXTURE_OK: quota"
        assert state["collapsedProcesses"] == [True]
    assert not ui["success"]["recoveryVisible"]
    assert len(ui["details"]["errors"]) == 2 and ui["details"]["processOpen"]
    assert ui["details"]["publicErrors"] == 0
    assert ui["search"] == {"results": 2, "status": "2 处 · 已加载记录", "draft": DRAFT}
    assert ui["followup"]["errors"] == [] and ui["followup"]["draft"] == ""
    assert ui["followup"]["users"] == ["quota", DRAFT]
    assert ui["followup"]["replies"] == ["FIXTURE_OK: quota", "FIXTURE_OK: " + DRAFT]
    calls = read("packed-model-calls.json")
    assert calls == {"quota": 3, DRAFT: 1}
    requests = read("packed-model-requests.json")
    assert [item["prompt"] for item in requests] == ["quota"] * 3 + [DRAFT]
    assert [item["user_messages"] for item in requests] == [1, 1, 1, 2]
    exported = (HERE / "export.md").read_text(encoding="utf-8")
    assert exported.count("## 模型额度不足") == 2
    assert "FIXTURE_OK: quota" in exported
    source = read("source-ui.json")
    assert all(source["frontend_sha256"][name] == pack["sources"][name]["sha256"] for name in FILES)
    assert source["source_fixture"] and source["model_network_requests"] == 0
    assert source["completed_reads"] == 1
    assert source["recovered"]["publicErrors"] == source["recovered"]["recoveryLabels"] == []
    assert source["recovered"]["collapsedProcesses"] == [True]
    assert source["recovered"]["reply"] == "Recovered read complete"
    assert source["expanded"]["processOpen"] and source["expanded"]["publicErrors"] == 0
    assert "session event persistence failed" in source["expanded"]["errorText"]
    for name in ("source", "packed-failed", "packed-recovered", "packed-followup"):
        assert read(name + "-events.json")["ok"]
    # Re-project the actual native events, independently of the recorded DOM.
    javascript = """
import assert from 'node:assert/strict';
import {eventsToTimeline} from './app/web/js/features/chat/timeline.js';
import {conversationGroups} from './app/web/js/features/chat/conversation-history.js';
let raw=''; for await (const part of process.stdin) raw+=part;
const traces=JSON.parse(raw);
for (const [name, expected] of Object.entries({source:0,'packed-failed':1,'packed-recovered':0,'packed-followup':0})) {
  const events=traces[name].data.items;
  const before=structuredClone(events);
  const items=eventsToTimeline(events);
  const groups=conversationGroups(items);
  assert.equal(groups.flatMap(g=>g.entries).filter(i=>i.kind==='error').length,expected,name);
  assert.equal(items.filter(i=>i.kind==='error').length,name==='source'?1:2,name);
  assert.deepEqual(events,before);
}
"""
    traces = {name: read(name + "-events.json")
              for name in ("source", "packed-failed", "packed-recovered", "packed-followup")}
    assert all(e["model_error_kind"] == "daily_token_limit"
        for e in traces["packed-recovered"]["data"]["items"] if e["kind"] == "error")
    assert traces["packed-followup"]["data"]["items"][:11] == traces["packed-recovered"]["data"]["items"]
    subprocess.run([shutil.which("node"), "--input-type=module", "-e", javascript],
        input=json.dumps(traces), text=True, encoding="utf-8", cwd=ROOT, check=True)
    print("PASS native/DOM projections, one current final error, recovery, draft, search, actual export, follow-up and two clean-pack sources")


if __name__ == "__main__": main()
