"""Check native history, page observations, and optional clean candidate bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DRAFT = "历史切换后继续：保留输入 🌱"


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8"))


def data(name):
    reply = read(name)
    assert reply["ok"] is True
    return reply["data"]


def events(reply):
    return json.loads(reply["items_json"])


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--running-copy", type=Path)
    args = parser.parse_args()
    short = data("short-turns.json")
    short_ui = read("short-ui.json")
    assert short["has_more"] is False and len(short["items"]) == 2
    assert short_ui["olderHidden"] is True and short_ui["items"] == 4
    assert short_ui["nav"] == ["short-one", "short-two"]
    assert all(item["answer"] in short_ui["timeline"] for item in short["items"])
    assert short_ui["toasts"] == []

    before = data("long-before.json")
    before_ui = read("long-ui-before.json")
    held = read("held-body.json")
    old = held["response"]["data"]
    cleared = data("cleared-conversation.json")
    assert before["has_more"] is True and before_ui["olderHidden"] is False
    assert before_ui["draft"] == DRAFT and before_ui["turns"] == ["16"]
    assert held["status"] == 200 and held["model_requests"] == 0
    assert "before=16" in held["path"]
    assert old["epoch"] == before["epoch"] != cleared["epoch"]
    assert old["next_before"] == 11 and any(e["text"] == "history-one" for e in events(old))
    assert (HERE / "held-send.txt").read_text(encoding="utf-8") == "peer closed"
    assert events(cleared)[0]["kind"] == "history_truncated"
    for name in ("cleared-ui.json", "late-ui.json"):
        ui = read(name)
        assert ui["draft"] == DRAFT and ui["items"] == 1
        assert ui["olderHidden"] is True and ui["toasts"] == [] and not ui["historyError"]
        assert all(text not in ui["timeline"] for text in ("short-one", "short-two", "history-one", "history-two"))

    followup = data("followup-conversation.json")
    complete = data("followup-full.json")
    ui = read("followup-ui.json")
    assert followup["epoch"] == complete["epoch"] == cleared["epoch"]
    assert complete["has_more"] is False
    items = events(complete)
    assert [e["event_id"] for e in items] == list(range(21, 27))
    assert [e["text"] for e in items if e["kind"] == "agent_start"] == [DRAFT]
    assert sum(e["kind"] == "agent_done" and e["success"] for e in items) == 1
    assert not any(e["kind"] == "error" for e in items)
    assert ui["items"] == 3 and ui["draft"] == "" and ui["sendEnabled"]
    assert ui["olderHidden"] is True and not ui["toasts"] and not ui["historyError"]
    assert ui["timeline"].count("FIXTURE_OK: " + DRAFT) == 1
    assert all(text not in ui["timeline"] for text in ("short-one", "short-two", "history-one", "history-two"))
    calls = read("calls.json")
    requests = read("requests.json")
    assert calls == {name: 1 for name in ("short-one", "short-two", "history-one", "history-two", DRAFT)}
    assert [r["user_messages"] for r in requests] == [1, 2, 3, 4, 1]
    assert [r["prompt"] for r in requests] == list(calls)

    assert read("cancel-jump.json") == {"jumps": 1, "indexes": 0, "toasts": []}
    assert read("cancel-index.json") == {"jumps": 1, "indexes": 1, "toasts": []}
    assert read("final-denial.json") == {"jumps": 2, "indexes": 1, "toasts": ["Expected history denial"]}
    receipt = read("build.json")
    assert receipt["mdo_worktree_dirty"] is False
    assert receipt["mdo_commit"].startswith("95066ef9")
    sources = {}
    if args.exe:
        raw = args.exe.read_bytes()
        assert sha(raw) == receipt["sha256"] and len(raw) == receipt["bytes"]
        if args.running_copy:
            assert args.running_copy.read_bytes() == raw
        helpers = runpy.run_path(str(HERE.parent / "decision-write-recovery/check_candidate.py"))
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "app.xrtpack"
            helpers["extract_pack"](args.exe, archive)
            pack = archive.read_bytes()
            entries = helpers["files_from_pack"](pack)
        for name in ("web/js/features/chat/timeline-store.js", "web/js/features/chat/conversation-navigation.js"):
            expected = (ROOT / "app" / name).read_bytes()
            assert entries[name] == expected
            sources[name] = sha(expected)
        assert not any("__fixture" in name or "navigation-cancel-browser" in name for name in entries)
    report = {"passed": True, "mdo_commit": receipt["mdo_commit"], "sha256": receipt["sha256"],
              "bytes": receipt["bytes"], "model_requests": len(requests), "external_provider_requests": 0,
              "old_epoch": before["epoch"], "new_epoch": cleared["epoch"],
              "late_response_peer_closed": True, "native_final_errors": 0,
              "navigation_component_final_denials": 1, "embedded_sources": sources,
              "packed_full_page": True, "android_device_tested": False, "daily_app_overwritten": False}
    (HERE / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS native old/new history, page observations, five model requests, and cancellation handlers")


if __name__ == "__main__":
    main()
