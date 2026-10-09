"""Bounded catalog/read faults around an unchanged disposable packed app.

control.json selects one exact GET path, a phase name, and at most six failed
responses or one held native response. release-<phase> releases a held body.
Writes and model requests always pass through without retry or modification.
"""
import http.client
import json
import time
from urllib.parse import urlsplit

import manual_timeline_history_qa as fixture


class ExtensionProxy(fixture.Proxy):
    counts = {}

    def record(self, value):
        with self.lock:
            with (self.root / "read-records.jsonl").open("a", encoding="utf-8") as output:
                output.write(json.dumps(value) + "\n")

    def forward(self):
        control_path = self.root / "control.json"
        control = json.loads(control_path.read_text(encoding="utf-8")) if control_path.exists() else {}
        if self.command != "GET" or urlsplit(self.path).path != control.get("path"):
            return super().forward()
        phase = control["phase"]
        with self.lock:
            count = self.counts[phase] = self.counts.get(phase, 0) + 1
        record = {"phase": phase, "path": self.path, "attempt": count, "started": time.monotonic()}
        failed = count <= min(max(int(control.get("failures", 0)), 0), 6)
        if failed:
            raw = json.dumps({"ok": False, "error": {"code": "fixture_unavailable",
                "message": "Temporary extension read unavailable"}}).encode()
            self.send_response(503); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)
            record["status"] = 503; self.record(record); return
        if not control.get("hold") or count != 1:
            record["forwarded"] = True; self.record(record); return super().forward()
        connection = http.client.HTTPConnection("127.0.0.1", self.upstream, timeout=10)
        try:
            connection.request("GET", self.path)
            response = connection.getresponse(); raw = response.read()
            record.update(status=response.status, response=json.loads(raw), held=True)
            (self.root / f"held-{phase}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
            deadline = time.monotonic() + 45
            while not self.quitting.is_set() and not (self.root / f"release-{phase}").exists() and time.monotonic() < deadline:
                time.sleep(.05)
            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() not in ("content-length", "transfer-encoding", "connection"):
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)
            record["sent"] = True
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            record["peer_closed"] = True
        finally:
            connection.close(); self.record(record)

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = forward


if __name__ == "__main__":
    fixture.Proxy = ExtensionProxy
    fixture.main()
