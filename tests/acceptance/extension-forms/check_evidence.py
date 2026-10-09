"""Check actual form observations, native writes, continuation and clean pack."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import runpy
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def read(name):
    return json.loads((HERE / name).read_text(encoding="utf-8"))


def fields(name):
    return {item["name"]: item for item in read(name)["fields"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--running-copy", type=Path)
    args = parser.parse_args()
    for name in ("subagent-missing.json", "subagent-whitespace.json"):
        ui = read(name); values = fields(name)
        assert ui["status"] == "请填写说明。" and ui["focus"] == "description"
        assert values["description"]["invalid"] == "true" and values["description"]["required"]
        assert values["extension_id"]["value"] == "qa-form-helper"
        assert values["name"]["value"] == "QA form helper"
        assert values["prompt"]["value"] == "Inspect files and return a concise result."
        assert not ui["errorToasts"]
    assert not any("/extensions/" in row["path"] for row in read("invalid-subagent-writes.json"))
    before = read("before-mcp-save-writes.json")
    assert len([row for row in before if "/extensions/" in row["path"]]) == 4
    checks = {"command-reserved": "extension_id", "command-missing": "prompt",
        "mcp-program-missing": "program", "mcp-json": "arguments", "mcp-array": "arguments",
        "mcp-http": "endpoint", "mcp-headers": "headers", "mcp-credential": "headers"}
    for name, focus in checks.items():
        ui = read(name + ".json")
        assert ui["focus"] == focus and ui["status"] and not ui["errorToasts"]
        assert fields(name + ".json")[focus]["invalid"] == "true"
        assert "qa-sensitive-placeholder" not in ui["status"]
    source = read("mcp-source-json.json")
    assert source["status"] == "源文件不是有效的 JSON，请检查括号、引号和逗号。"
    assert "qa-sensitive-placeholder" in source["source"] and not source["errorToasts"]
    assert read("source-focus.json") == {"focusClass": "extension-source", "sourceInvalid": "true", "sourceVisible": True}
    ready = fields("mcp-ready.json")
    assert ready["arguments"]["disabled"] and not ready["arguments"]["visible"]
    assert ready["arguments"]["value"] == "[1]" and not ready["endpoint"]["disabled"]
    for name in ("agent-ready.json", "skill-ready.json"):
        assert fields(name)["prompt"]["value"] == "" and not fields(name)["prompt"]["required"]
    rows = [json.loads(line) for line in (HERE / "writes.jsonl").read_text(encoding="utf-8").splitlines()]
    extension_rows = [row for row in rows if "/extensions/" in row["path"]]
    expected = {"subagents/qa-form-helper": 1, "agents/qa-empty-prompt": 1,
        "skills/qa-empty-skill": 1, "commands/qa-review": 1, "mcp/qa-form-server": 3}
    assert Counter(row["path"].split("/extensions/")[1] for row in extension_rows) == expected
    assert all(row["method"] == "PUT" and row["status"] == 200 for row in extension_rows)
    for name in ("subagent", "agent", "skill", "command", "mcp", "mcp-stdio"):
        data = read(name + ".json")["data"]
        assert data["valid"] and data["external"] and not data["error"]
    subagent = read("subagent.json")["data"]["content"]
    assert 'tools: ["read"]' in subagent and "read_only: true" in subagent
    assert read("agent.json")["data"]["content"].endswith("---\n")
    for name, transport in (("mcp", "streamable-http"), ("mcp-stdio", "stdio")):
        data = read(name + ".json")["data"]
        assert not data["enabled"] and not data["connected"]
        assert json.loads(data["content"])["transport"]["type"] == transport
    assert read("followup-before.json")["draft"] == "EXTENSION_FORM_FOLLOWUP"
    ui = read("followup-final.json")
    assert ui["dialogs"] == 0 and ui["draft"] == "" and not ui["errorToasts"]
    assert ui["timeline"].count("FIXTURE_OK: EXTENSION_FORM_FOLLOWUP") == 1
    assert read("calls.json") == {"EXTENSION_FORM_FOLLOWUP": 1}
    assert read("requests.json") == [{"prompt": "EXTENSION_FORM_FOLLOWUP", "image_parts": 0, "user_messages": 1, "tool_results": 0}]
    events = read("followup-events.json")
    assert sum(event["kind"] == "agent_done" and event["success"] for event in events) == 1
    assert not any(event["kind"] == "error" for event in events)
    receipt = read("build.json"); runtime = read("runtime.json")
    assert not receipt["mdo_worktree_dirty"] and receipt["mdo_commit"].startswith("b2029627")
    assert runtime["passed"] and runtime["packed_sha256"] == receipt["sha256"]
    assert runtime["final_model_errors"] == 0 and sum(runtime["model_requests"].values()) == 16
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
        for name in ("web/js/features/settings/extension-panels.js", "web/js/features/settings/extension-validation.js",
                "web/css/extensions.css", "web/lang/zh-CN.json", "web/lang/en-US.json", "web/lang/ru-RU.json"):
            expected_source = (ROOT / "app" / name).read_bytes()
            assert entries[name] == expected_source
            sources[name] = hashlib.sha256(expected_source).hexdigest()
        assert not any("manual_extension_form_qa" in name for name in entries)
    report = {"passed": True, "mdo_commit": receipt["mdo_commit"], "sha256": receipt["sha256"],
        "bytes": receipt["bytes"], "embedded_sources": sources, "successful_extension_writes": 7,
        "page_model_requests": 1, "native_extension_model_requests": 16, "final_model_errors": 0,
        "external_provider_requests": 0, "android_device_tested": False, "daily_app_overwritten": False}
    (HERE / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS required fields, bounded format errors, real writes, preserved draft and clean pack")


if __name__ == "__main__": main()
