"""Race a real packed Markdown export with bounded, standard-API history edits.

Reuse the loopback-only export fixture's isolated Home, model and cleanup.
Arm /__qa/arm/replace (one clear) or /__qa/arm/repeat (two clears).
Each replacement is an actual clear + completed run, never edited event files.
"""
import hashlib
import json
import sys
import time
from urllib.parse import parse_qs, urlsplit

import manual_session_export_recovery_qa as fixture
from test_api_runtime import request
from test_image_runtime import wait_run


class SnapshotModel(fixture.TextModel):
    calls = []

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        text = next(row["content"] for row in reversed(body["messages"]) if row["role"] == "user")
        self.calls.append(text)
        if text.startswith("SNAPSHOT_INPUT_"):
            content = text.replace("INPUT", "REPLY") + "\n" + "Replacement original line.\n" * 400
        else:
            content = fixture.LONG_REPLY if text == "EXPORT DOWNLOAD QA" else "CONTINUED — " + text
        raw = json.dumps({"id": "snapshot-fixture", "model": body["model"],
            "choices": [{"index": 0, "message": {"role": "assistant", "content": content},
                "finish_reason": "stop"}], "usage": {"prompt_tokens": 10,
                "completion_tokens": 20, "total_tokens": 30}}, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


class SnapshotProxy(fixture.Proxy):
    def proxy(self):
        if self.command == "GET" and self.path in ("/__qa/arm/replace", "/__qa/arm/repeat"):
            mode = self.path.rsplit("/", 1)[-1]
            with self.server.lock:
                phase = {"mode": mode, "started_at": time.monotonic(), "reads": [],
                    "left": 0, "full_left": 0, "replacements_left": 1 if mode == "replace" else 2,
                    "mutations": []}
                self.server.export_phase = phase
                self.server.exports.append(phase)
            return self.reply(200, phase)
        parsed = urlsplit(self.path)
        params = parse_qs(parsed.query)
        listing = self.command == "GET" and parsed.path == self.server.text_path and \
            params.get("after") == ["0"] and "full_text" not in params
        with self.server.lock:
            phase = self.server.export_phase
            replace = bool(listing and phase and phase.get("replacements_left", 0))
            if replace:
                phase["replacements_left"] -= 1
        if not replace:
            return super().proxy()
        # Retain the original snapshot response, then replace its journal before
        # handing it to the browser. The subsequent full read must honor its epoch.
        port = self.server.native
        status, _, raw = request(port, "GET", self.path)
        assert status == 200, raw
        old = json.loads(raw)["data"]
        session_path = self.server.text_path.removesuffix("/events")
        status, headers, detail = request(port, "GET", session_path)
        assert status == 200, detail
        status, _, clear = request(port, "POST", session_path + "/clear",
            headers={"If-Match": headers["etag"]})
        assert status == 200, clear
        version = len(SnapshotModel.calls)
        prompt = "SNAPSHOT_INPUT_" + str(version)
        status, _, run = request(port, "POST", session_path + "/runs",
            body=json.dumps({"prompt": prompt}).encode(), headers={"Content-Type": "application/json"})
        assert status == 202, run
        run_id = json.loads(run)["data"]["id"]
        assert wait_run(port, run_id)["state"] == "succeeded"
        status, _, latest = request(port, "GET", self.server.text_path + "?after=0&limit=32")
        assert status == 200, latest
        new = json.loads(latest)["data"]
        assert old["epoch"] != new["epoch"]
        phase["mutations"].append({"prompt": prompt, "old_epoch": old["epoch"],
            "new_epoch": new["epoch"], "run_id": run_id, "clear_status": 200})
        phase["reads"].append({"path": self.path, "status": 200,
            "wall_at": time.time(), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "old_snapshot": True, "epoch": old["epoch"]})
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = proxy


if __name__ == "__main__":
    fixture.Proxy = SnapshotProxy
    fixture.TextModel = SnapshotModel
    if "--text-recovery" not in sys.argv:
        sys.argv.append("--text-recovery")
    fixture.main()
