"""Bounded HTTP write admission, stale-page rejection and two controlled clients.

All Homes/data are synthetic. Holds are injected only into copied source.
No model calls, user Home, pressure or high-load runs.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import http.client
import json
from pathlib import Path
import shutil
import tempfile
import time

from test_api_runtime import request
from test_project_purge_http_runtime import params, PURGE, RESULT
from test_project_purge_receipts_runtime import ROOT, ID, ReceiptProbe

TOKEN = "X-Mdo-Write-Token"
INTENT = "/api/v1/project-purge-intent"
PREPARE = "/api/v1/projects/purge-probe/purge-intent"
OTHER_DRAFT = "/api/v1/projects/purge-other/draft"


class AdmissionProbe(ReceiptProbe):
    def __init__(self, host, base):
        super().__init__(host, base)
        config = json.loads(self.config.read_text(encoding="utf-8"))
        config.setdefault("engine", {})["workers"] = 2
        self.config.write_text(json.dumps(config), encoding="utf-8")
        shutil.copy2(ROOT / "tests/fixtures/write-admission.c", self.site / "src/bootstrap/write-admission-probe.c")
        changes = (
            ("src/bootstrap/service.c", "void ServiceInit(XS_HostInfo* pHost)",
             '#include "write-admission-probe.c"\nvoid ServiceInit(XS_HostInfo* pHost)'),
            ("src/api/router.c", '#include "write_admission.h"',
             '#include "write_admission.h"\nvoid MdoWriteFixtureDeferLeave(MdoApiContext*);'),
            ("src/api/router.c", "    MdoApiWriteLeave(Context);\n    return Ok;",
             "    MdoWriteFixtureDeferLeave(Context);\n    MdoApiWriteLeave(Context);\n    return Ok;"),
            ("src/bootstrap/service.c", "    MdoApiUnit();",
             "    MdoWriteFixtureUnit();\n    MdoApiUnit();"),
        )
        for relative, old, new in changes:
            path = self.site / relative
            text = path.read_text(encoding="utf-8")
            assert text.count(old) == 1, relative
            path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

    def token(self):
        status, headers, _ = request(self.port, "GET", INTENT, read_write_token=False)
        assert status == 200, status
        return headers["x-mdo-write-token"]

    def raw(self, method, path, payload=None, expected=200, headers=None):
        status, metadata, body = request(self.port, method, path,
            body=json.dumps(payload).encode() if payload is not None else None,
            headers={"Content-Type": "application/json", **(headers or {})}, read_write_token=False)
        document = json.loads(body) if method != "HEAD" else None
        assert status == expected, (method, path, status, body)
        return metadata, document

    def unchanged(self):
        return {key: value for key, value in self.files().items()
                if not key.startswith("data/write-admission-probe-")}


@contextmanager
def running(host, base):
    probe = AdmissionProbe(host, base)
    try:
        probe.start()
        yield probe
    except BaseException as error:
        log = probe.log_path.read_text(encoding="utf-8", errors="replace")
        raise RuntimeError(f"{error}\n--- xs ---\n{log[-5000:]}") from error
    finally:
        # Always release a copied hold before stopping its owned process.
        if probe.home.exists():
            path = probe.home / "data/write-admission-probe-release"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"release")
        probe.stop()


def held(probe, method, path, body, headers):
    # The actual handler finishes; its admission remains held on one fixture
    # thread so a second HTTP connection can reach the real gate.
    outcome = probe.raw(method, path, body, headers=headers)
    marker = probe.home / "data/write-admission-probe-paused"
    deadline = time.monotonic() + 3
    while not marker.exists():
        assert time.monotonic() < deadline, "admission hold not reached"
        time.sleep(0.01)
    return outcome


def release(probe):
    (probe.home / "data/write-admission-probe-release").write_bytes(b"release")
    deadline = time.monotonic() + 3
    while (probe.home / "data/write-admission-probe-paused").exists():
        assert time.monotonic() < deadline, "admission hold did not release"
        time.sleep(0.01)


def run_probe(host):
    with tempfile.TemporaryDirectory(prefix="write-admission-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        with running(host, base / "identity") as p:
            old = p.token()
            assert len(old.split("-")[0]) == 32 and old.endswith("-0") and not p.home.exists()
            for value, status, code in ((None, 428, "write_token_required"),
                    ("invalid", 412, "write_token_conflict"),
                    ("0" * 32 + "-0", 412, "write_token_conflict")):
                _, document = p.raw("PUT", "/api/v1/draft", {"revision": 0, "text": "stale"},
                    status, {TOKEN: value} if value is not None else {})
                assert document["error"]["code"] == code and not p.home.exists()
            duplicate = http.client.HTTPConnection("127.0.0.1", p.port, timeout=3)
            duplicate.putrequest("PUT", "/api/v1/draft")
            duplicate.putheader(TOKEN, old); duplicate.putheader(TOKEN, old)
            duplicate.putheader("Content-Length", "0"); duplicate.endheaders()
            response = duplicate.getresponse()
            assert response.status == 400 and json.loads(response.read())["error"]["code"] == "write_token_invalid"
            duplicate.close(); assert not p.home.exists()
            targets = p.seed(); before = p.unchanged()
            body, headers = params(p)
            p.raw("POST", PREPARE, body, headers={**headers, TOKEN: old})
            first, receipt = p.raw("POST", PURGE, body, headers={**headers, TOKEN: old})
            assert receipt["data"]["committed"]
            current = first["x-mdo-write-token"]
            assert current == old.removesuffix("0") + "1" and p.token() == current
            stable = p.unchanged()
            for path, payload in (("/api/v1/draft", {"revision": 0, "text": "old page", "new_task": None}),
                    ("/api/v1/projects/purge-probe/draft", {"revision": 0, "text": "resurrect"}),
                    ("/api/v1/projects", {"id": "purge-probe", "name": "late"}),
                    ("/api/v1/sessions", {"project_id": "purge-probe", "title": "late"}),
                    ("/api/v1/workspace-state", {"project_id": "purge-probe", "session_id": "a" * 32})):
                method = "POST" if path.endswith(("projects", "sessions")) else "PUT"
                p.raw(method, path, payload, 412, {TOKEN: old})
                assert p.unchanged() == stable, path
            # Raw image bytes are rejected before MIME/body/storage work too.
            status, _, _ = request(p.port, "POST", "/api/v1/projects/purge-probe/sessions/" + "a" * 32 + "/attachments",
                body=b"stale-image", headers={TOKEN: old, "Content-Type": "image/png"}, read_write_token=False)
            assert status == 412 and p.unchanged() == stable
            # Recovery remains replayable with old/missing tokens, even after ACK.
            replay, receipt = p.raw("POST", PURGE, body, headers=headers)
            assert receipt["data"]["replayed"] and replay["x-mdo-write-token"] == current
            p.raw("DELETE", INTENT, headers={"If-Match": '"mdo-purge-intent-' + ID + '"'})
            assert p.token() == current
            p.raw("POST", "/api/v1/projects", {"id": "purge-probe", "name": "fresh"}, 201, {TOKEN: current})
            rebuilt = p.unchanged()
            p.raw("PUT", "/api/v1/projects/purge-probe/draft", {"revision": 0, "text": "late original"}, 412, {TOKEN: old})
            assert p.unchanged() == rebuilt
            p.raw("PUT", "/api/v1/projects/purge-probe/draft", {"revision": 0, "text": "new draft"}, headers={TOKEN: current})
            p.stop(); p.start()
            restarted = p.token()
            assert restarted != current and restarted.endswith("-0")
            stable = p.unchanged()
            p.raw("PUT", OTHER_DRAFT, {"revision": 0, "text": "previous host"}, 412, {TOKEN: current})
            assert p.unchanged() == stable

        with running(host, base / "writer-first") as p:
            p.seed(); token = p.token()
            draft = p.api("GET", OTHER_DRAFT)
            body, headers = params(p)
            held(p, "PUT", OTHER_DRAFT,
                {"revision": draft["revision"], "text": "accepted other draft"},
                {TOKEN: token, "X-Fixture-Admission-Hold": "writer"})
            before = p.unchanged()
            _, refused = p.raw("POST", PURGE, body, 409, {**headers, TOKEN: token})
            assert refused["error"]["code"] == "write_admission_busy"
            assert p.token() == token and p.unchanged() == before
            p.raw("GET", RESULT, expected=404)
            release(p)
            assert p.api("GET", OTHER_DRAFT)["text"] == "accepted other draft"
            p.raw("POST", PURGE, body, headers={**headers, TOKEN: token})
            assert p.api("GET", OTHER_DRAFT)["text"] == "accepted other draft"

        with running(host, base / "purge-first") as p:
            p.seed(); token = p.token(); body, headers = params(p)
            _, outcome = held(p, "POST", PURGE, body,
                {**headers, TOKEN: token, "X-Fixture-Admission-Hold": "exclusive"})
            assert outcome["data"]["committed"]
            before = p.unchanged()
            draft = p.api("GET", OTHER_DRAFT)
            _, refused = p.raw("PUT", OTHER_DRAFT,
                {"revision": draft["revision"], "text": "not admitted"}, 409, {TOKEN: token})
            assert refused["error"]["code"] == "write_admission_busy" and p.unchanged() == before
            release(p)
            stable = p.unchanged()
            p.raw("PUT", OTHER_DRAFT, {"revision": draft["revision"], "text": "late"}, 412, {TOKEN: token})
            assert p.unchanged() == stable
    print("HTTP write admission/stale-page/restart/two-client probe: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
