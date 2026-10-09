"""Observe real packed editor writes without changing requests or responses.

Uses a separate Home and the loopback model fixture. Mutation records retain
only method, path and HTTP status; source, credentials and tokens are omitted.
"""
import json
import time

import manual_timeline_history_qa as fixture


class FormProxy(fixture.Proxy):
    def send_response(self, code, message=None):
        self.response_status = code
        super().send_response(code, message)

    def forward(self):
        started = time.monotonic()
        try:
            return super().forward()
        finally:
            if self.command in ("POST", "PUT", "PATCH", "DELETE"):
                record = {"method": self.command, "path": self.path,
                    "status": getattr(self, "response_status", None), "started": started}
                with self.lock:
                    with (self.root / "writes.jsonl").open("a", encoding="utf-8") as output:
                        output.write(json.dumps(record) + "\n")

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = forward


if __name__ == "__main__":
    fixture.Proxy = FormProxy
    fixture.main()
