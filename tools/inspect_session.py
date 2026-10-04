#!/usr/bin/env python3
"""Read a portable mdo session without starting mdo or changing its files.

The UI journal is an event replay, not a complete network recording. Context
recovery still belongs to xllm-session; this tool only inventories its files.
Uses the Python standard library so it also works outside the build machine.
"""

from __future__ import annotations

import argparse
from collections import Counter, deque
from datetime import datetime, timezone
import json
from pathlib import Path
import sys


# Stable xwork event codes used by mdo UI schemas 1 through 5. Preserve unknown
# codes rather than interpreting a newer schema with an older vocabulary.
KINDS = (
    "agent_start", "model_start", "model_text_delta", "model_reasoning_delta",
    "model_done", "tool_start", "tool_done", "compaction_start",
    "compaction_rejected", "compaction_done", "agent_done", "error",
    "artifact_created", "task_updated", "recovery_required",
    "recovery_resolved", "history_truncated",
)
MAX_RECORD_BYTES = 256 * 1024
MAX_META_BYTES = 64 * 1024
MAX_WARNINGS = 100


def integer(value: object, default: int = 0) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) else default


def kind_name(event: dict) -> str:
    kind = event.get("kind")
    if isinstance(kind, str):
        return kind
    if (integer(event.get("schema_version")) in range(1, 6)
            and isinstance(kind, int) and not isinstance(kind, bool)
            and 0 <= kind < len(KINDS)):
        return KINDS[kind]
    return f"unknown:{kind}"


def inspect_session(directory: Path, limit: int = 50) -> dict:
    """Stream bounded records; the report refers only to retained events."""
    if not directory.is_dir():
        raise ValueError("session directory does not exist")
    if not 1 <= limit <= 1000:
        raise ValueError("limit must be between 1 and 1000")
    warnings: list[str] = []
    warning_count = 0

    def warn(message: str) -> None:
        nonlocal warning_count
        warning_count += 1
        if len(warnings) < MAX_WARNINGS:
            warnings.append(message)

    metadata = {}
    meta = directory / "meta.json"
    if meta.is_file():
        if meta.stat().st_size > MAX_META_BYTES:
            warn("meta.json exceeds the size limit; skipped")
        else:
            try:
                value = json.loads(meta.read_text(encoding="utf-8"))
                if not isinstance(value, dict):
                    raise ValueError("expected an object")
                # Only display descriptive fields, never arbitrary config.
                metadata = {key: value[key] for key in (
                    "id", "project_id", "title", "agent_id", "model_id",
                    "protocol", "reasoning_effort", "workspace_root", "status",
                ) if key in value}
            except (UnicodeError, ValueError) as error:
                warn(f"meta.json is invalid: {error}")

    files = {}
    for name in ("meta.json", "snapshot.json", "journal.jsonl", "ui-events.jsonl"):
        path = directory / name
        files[name] = path.stat().st_size if path.is_file() else None
    if files["snapshot.json"] is None or files["journal.jsonl"] is None:
        warn("context files are incomplete; this report cannot recover model context")

    counts: Counter[str] = Counter()
    tools: dict[str, dict] = {}
    starts: dict[tuple, int] = {}
    recent: deque = deque(maxlen=limit)
    input_tokens = output_tokens = 0
    truncated = records = 0
    first_id = last_id = None
    journal = directory / "ui-events.jsonl"
    with journal.open("rb") as stream:
        line_number = 0
        while raw := stream.readline(MAX_RECORD_BYTES + 1):
            line_number += 1
            if len(raw) > MAX_RECORD_BYTES:
                while not raw.endswith(b"\n"):
                    raw = stream.readline(MAX_RECORD_BYTES + 1)
                    if not raw:
                        break
                warn(f"line {line_number}: oversized record skipped")
                continue
            if not raw.endswith(b"\n"):
                warn(f"line {line_number}: unfinished tail skipped (file may still be active)")
                break
            if not raw.strip():
                continue
            try:
                event = json.loads(raw)
                if not isinstance(event, dict):
                    raise ValueError("expected an object")
            except (UnicodeError, ValueError) as error:
                warn(f"line {line_number}: invalid record skipped: {error}")
                continue
            kind = kind_name(event)
            records += 1
            counts[kind] += 1
            event_id = integer(event.get("event_id"))
            if event_id > 0:
                if first_id is None:
                    first_id = event_id
                    if event_id > 1:
                        warn("earlier events are absent: retention, fork or history editing")
                elif event_id != last_id + 1:
                    warn(f"line {line_number}: event ID gap or reordered record")
                last_id = event_id
            if event.get("text_truncated"):
                truncated += 1
            if kind == "history_truncated":
                warn("history was explicitly cleared or truncated")
            if kind == "model_done":
                input_tokens += max(0, integer(event.get("input_tokens")))
                output_tokens += max(0, integer(event.get("output_tokens")))
            identity = tuple(str(event.get(key, "")) for key in (
                "session_id", "run_id", "agent_id", "agent_depth", "tool_call_id"))
            time_us = integer(event.get("occurred_at_us", event.get("time")))
            if kind == "agent_start":
                # Run IDs may be reused after restarting the host. Do not pair
                # an interrupted old call with a later run's completion.
                starts = {key: value for key, value in starts.items()
                          if key[:4] != identity[:4]}
            if kind == "tool_start":
                starts[identity] = time_us
            if kind == "tool_done":
                name = str(event.get("tool_name") or "unknown")
                tool = tools.setdefault(name, {"completed": 0, "failed": 0,
                    "paired": 0, "elapsed_ms": 0.0})
                tool["completed"] += 1
                if event.get("success") is False:
                    tool["failed"] += 1
                start = starts.pop(identity, None)
                if start is not None and start > 0 and time_us >= start:
                    tool["paired"] += 1
                    tool["elapsed_ms"] += (time_us - start) / 1000
            text = event.get("text")
            preview = " ".join(text.split())[:200] if isinstance(text, str) else ""
            recent.append({"event_id": event_id, "time_us": time_us, "kind": kind,
                "run_id": event.get("run_id"), "task_id": event.get("task_id"),
                "tool": event.get("tool_name"), "preview": preview,
                "text_truncated": bool(event.get("text_truncated"))})
    if truncated:
        warn(f"{truncated} records have truncated text; inspect referenced artifacts if needed")
    return {"session": metadata, "files": files, "retained_records": records,
        "first_event_id": first_id, "last_event_id": last_id,
        "event_counts": dict(sorted(counts.items())),
        "model_usage": {"input_tokens": input_tokens, "output_tokens": output_tokens},
        "tools": tools, "warnings": warnings, "warning_count": warning_count,
        "recent_events": list(recent)}


def safe_text(value: object) -> str:
    # JSON escaping prevents log text from injecting terminal control sequences.
    return json.dumps(value, ensure_ascii=False)


def render_report(report: dict, events: bool = False) -> str:
    lines = ["mdo session report (retained records only)",
        f"Session: {safe_text(report['session'])}",
        f"Events: {report['retained_records']} ({report['first_event_id']}..{report['last_event_id']})",
        f"Model usage: {safe_text(report['model_usage'])}",
        f"Event counts: {safe_text(report['event_counts'])}",
        "Tools (elapsed time only for matched start/done pairs):"]
    for name, tool in report["tools"].items():
        lines.append(f"  {safe_text(name)}: {safe_text(tool)}")
    for message in report["warnings"]:
        lines.append(f"Warning: {safe_text(message)}")
    if report["warning_count"] > len(report["warnings"]):
        lines.append(f"Additional warnings: {report['warning_count'] - len(report['warnings'])}")
    if events:
        lines.append("Recent events:")
        for event in report["recent_events"]:
            try:
                time = datetime.fromtimestamp(event["time_us"] / 1_000_000,
                    timezone.utc).isoformat()
            except (OverflowError, OSError, ValueError):
                time = str(event["time_us"])
            lines.append(f"  #{event['event_id']} {time} {safe_text(event['kind'])} "
                         f"{safe_text(event['tool'] or '')} {safe_text(event['preview'])}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path, help="mdo-home/sessions/<project>/<session>")
    parser.add_argument("--events", action="store_true", help="include recent event previews")
    parser.add_argument("--limit", type=int, default=50, help="recent events to retain (1..1000)")
    parser.add_argument("--json", action="store_true", help="emit a structured JSON report")
    args = parser.parse_args()
    try:
        report = inspect_session(args.session, args.limit)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2) if args.json
          else render_report(report, args.events), end="\n" if args.json else "")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
