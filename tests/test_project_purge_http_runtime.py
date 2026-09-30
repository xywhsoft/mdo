"""Bounded project purge HTTP contract and advisory-reference scope.

Production routes in copied xs/TCC applications, with faults only in private
fixtures. No external model calls, user-data deletion, stress or high load.
"""
from __future__ import annotations

import argparse
import http.client
import json
import os
import tempfile
import time
from pathlib import Path

from test_api_runtime import request
from test_project_purge_receipts_runtime import ROOT, ID, running

PURGE = "/api/v1/projects/purge-probe/purge"
RESULT = "/api/v1/project-purges/" + ID
PREVIEW = "/api/v1/projects/purge-probe/purge-preview"


def send(probe, method: str, path: str, payload=None, expected=200, headers=None):
    status, response_headers, body = request(probe.port, method, path,
        body=json.dumps(payload).encode() if payload is not None else None,
        headers={"Content-Type": "application/json", **(headers or {})})
    assert status == expected, (method, path, status, body)
    document = json.loads(body) if method != "HEAD" else None
    if document is not None:
        assert document["request_id"] == response_headers["x-request-id"]
        assert document["ok"] is (status < 400), document
    assert response_headers["cache-control"] == "no-store"
    return response_headers, document


def params(probe):
    origin = probe.origin()
    return {"purge_request_id": ID, "created_at": origin["created_at"]}, {
        "If-Match": f'"mdo-project-purge-probe-{origin["revision"]}"'}


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="purge-http-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        with running(host, base / "normal") as probe:
            send(probe, "GET", RESULT, expected=404)
            send(probe, "HEAD", RESULT, expected=404)
            for path, allow in ((PURGE, "POST, OPTIONS"), (RESULT, "GET, HEAD, OPTIONS")):
                headers, _ = send(probe, "OPTIONS", path)
                assert headers["allow"] == allow
                send(probe, "PUT", path, {}, 405)
            send(probe, "GET", "/api/v1/project-purges/" + "A" * 32, expected=400)
            send(probe, "POST", PURGE, {}, 428)
            headers = {"If-Match": '"mdo-project-purge-probe-1"'}
            _, missing = send(probe, "POST", PURGE,
                {"purge_request_id": ID, "created_at": 1}, 404, headers)
            assert missing["error"]["details"]["accepted"] is False
            assert not probe.home.exists()

            targets = probe.seed()
            before = probe.files()
            headers, preview = send(probe, "GET", PREVIEW)
            preview = preview["data"]
            assert preview["selection_reference_present"] and preview["global_draft_reference_present"]
            paths = [target["path"] for target in preview["targets"]]
            assert paths == sorted(targets), preview
            nodes = [node for relative in paths for node in
                ([probe.home / relative] + list((probe.home / relative).rglob("*")))]
            assert preview["file_count"] == sum(node.is_file() for node in nodes)
            assert preview["directory_count"] == sum(node.is_dir() for node in nodes)
            assert preview["total_bytes"] == sum(node.stat().st_size for node in nodes if node.is_file())
            assert preview["created_at"] == probe.origin()["created_at"] and preview["advisory"]
            send(probe, "HEAD", PREVIEW)
            assert probe.files() == before
            body, headers = params(probe)
            # Closed body, strict ETag and creation preconditions have no effects.
            for bad, status in (({}, 422), ({**body, "purge_request_id": "A" * 32}, 422),
                    ({**body, "created_at": True}, 422), ({**body, "created_at": 1.5}, 422),
                    ({**body, "path": "sessions/other"}, 422),
                    ({**body, "created_at": body["created_at"] + 1}, 412)):
                send(probe, "POST", PURGE, bad, status, headers)
            for tag, status in (('W/"mdo-project-purge-probe-1"', 400),
                    ('"mdo-project-purge-other-1"', 412), ('"mdo-project-purge-probe-2"', 412)):
                send(probe, "POST", PURGE, body, status, {"If-Match": tag})
            duplicate = http.client.HTTPConnection("127.0.0.1", probe.port, timeout=5)
            encoded = json.dumps(body).encode()
            duplicate.putrequest("POST", PURGE)
            duplicate.putheader("Content-Type", "application/json")
            duplicate.putheader("Content-Length", str(len(encoded)))
            duplicate.putheader("If-Match", headers["If-Match"])
            duplicate.putheader("If-Match", headers["If-Match"])
            duplicate.endheaders(encoded)
            response = duplicate.getresponse()
            assert response.status == 400, response.read()
            response.read(); duplicate.close()
            send(probe, "GET", RESULT, expected=404)
            assert probe.files() == before
            probe.control("hold-session")
            # Preview needs a shared lease and remains readable with an idle
            # retained session; execution must acquire exclusion and refuse.
            _, busy_preview = send(probe, "GET", PREVIEW)
            assert busy_preview["data"]["targets"] == preview["targets"]
            _, refused = send(probe, "POST", PURGE, body, 409, headers)
            assert refused["error"]["code"] == "project_busy"
            assert refused["error"]["details"]["accepted"] is False
            probe.control("release-session")
            assert probe.files() == before
            # Send the full request and close without consuming any response.
            # A durable result, rather than a guessed retry/new ID, settles it.
            lost = http.client.HTTPConnection("127.0.0.1", probe.port, timeout=5)
            _, current, _ = request(probe.port, "GET", "/api/v1/bootstrap")
            lost.request("POST", PURGE, json.dumps(body),
                {"Content-Type": "application/json", **headers,
                 "X-Mdo-Write-Token": current["x-mdo-write-token"]})
            lost.close()
            deadline = time.monotonic() + 3
            while request(probe.port, "GET", RESULT)[0] != 200:
                assert time.monotonic() < deadline, "disconnected purge did not publish a result"
                time.sleep(0.01)
            _, receipt = send(probe, "GET", RESULT)
            receipt = receipt["data"]
            assert receipt["outcome"] == "committed" and receipt["committed"] and receipt["accepted"]
            assert receipt["selection_removed"] and receipt["global_draft_removed"]
            assert receipt["target_count"] == preview["target_count"] == len(targets)
            assert receipt["file_count"] == preview["file_count"] and receipt["total_bytes"] == preview["total_bytes"]
            assert probe.published() == probe.expected_after(before, targets)
            assert (probe.base / "workspace/user-file.txt").read_bytes() == b"outside Home; never remove\n"
            stable, state = probe.files(), probe.control("state")
            _, replay = send(probe, "POST", PURGE, body, headers=headers)
            assert replay["data"] == {**receipt, "replayed": True}, replay
            assert probe.files() == stable and probe.control("state") == state
            send(probe, "HEAD", RESULT)
            probe.stop(); probe.start()
            _, recovered = send(probe, "GET", RESULT)
            assert recovered["data"] == receipt and probe.files() == stable
            probe.api("POST", "/api/v1/projects", {"id": "purge-probe", "name": "Recreated"}, 201)
            stable = probe.files()
            send(probe, "POST", PURGE, body, headers=headers)
            _, conflict = send(probe, "POST", PURGE, params(probe)[0], 409, headers)
            assert conflict["error"]["code"] == "purge_request_conflict"
            assert not conflict["error"]["details"]["committed"] and probe.files() == stable
            send(probe, "POST", "/api/v1/projects/purge-other/purge", body, 409,
                {"If-Match": '"mdo-project-purge-other-1"'})
            assert probe.files() == stable
            probe.record().write_bytes(b"{bad receipt}")
            damaged = probe.files()
            send(probe, "GET", RESULT, expected=503)
            _, invalid = send(probe, "POST", PURGE, body, 503, headers)
            assert invalid["error"]["details"]["accepted"] is None
            assert invalid["error"]["details"]["outcome"] == "unknown" and probe.files() == damaged

        for fault, committed, pending in (("fault-rollback", False, False),
                ("fault-unresolved", False, True), ("fault-cache", True, False),
                ("fault-cleanup", True, False), ("unpublished", True, True)):
            with running(host, base / fault) as probe:
                targets = probe.seed()
                before = probe.files()
                body, headers = params(probe)
                if fault == "unpublished": probe.fault(1)
                else: probe.control(fault)
                _, outcome = send(probe, "POST", PURGE, body, 503 if fault != "fault-rollback" else 409, headers)
                facts = outcome["error"]["details"]
                assert facts["committed"] is committed and facts["accepted"] is True
                assert facts["restart_required"] is (fault != "fault-rollback")
                assert facts["outcome"] == ("pending" if pending else "committed" if committed else "aborted")
                stable = probe.files()
                _, query = send(probe, "GET", RESULT)
                assert query["data"]["committed"] is committed
                assert query["data"]["outcome"] == facts["outcome"]
                _, repeat = send(probe, "POST", PURGE, body, 503 if fault != "fault-rollback" else 409, headers)
                assert repeat["error"]["details"]["committed"] is committed
                assert probe.files() == stable
                if fault != "fault-rollback":
                    send(probe, "PUT", "/api/v1/projects/purge-other", {}, 503, {"If-Match": '"mdo-project-purge-other-1"'})
                    assert probe.files() == stable
                probe.stop(); probe.start()
                _, query = send(probe, "GET", RESULT)
                assert query["data"]["outcome"] == ("committed" if committed else "aborted")
                assert not query["data"]["restart_required"]
                _, repeat = send(probe, "POST", PURGE, body, 200 if committed else 409, headers)
                facts = repeat["data"] if committed else repeat["error"]["details"]
                assert facts["replayed"] and facts["committed"] is committed
                assert probe.published() == (probe.expected_after(before, targets) if committed else before)

        with running(host, base / "unassociated") as probe:
            targets = probe.seed(own_references=False)
            before = probe.files()
            _, preview = send(probe, "GET", PREVIEW)
            assert not preview["data"]["selection_reference_present"]
            assert not preview["data"]["global_draft_reference_present"]
            assert {target["path"] for target in preview["data"]["targets"]} == targets
            for relative in ("data/draft.json", "data/workspace-state.json"):
                file = probe.home / relative
                original = file.read_bytes()
                file.write_bytes(b"{broken reference}")
                damaged = probe.files()
                send(probe, "GET", PREVIEW, expected=503)
                body, headers = params(probe)
                send(probe, "POST", PURGE, body, 503, headers)
                send(probe, "GET", RESULT, expected=404)
                assert probe.files() == damaged
                file.write_bytes(original)
            assert probe.files() == before
            body, headers = params(probe)
            _, cleared = send(probe, "POST", PURGE, body, headers=headers)
            assert not cleared["data"]["selection_removed"] and not cleared["data"]["global_draft_removed"]
            assert probe.published() == probe.expected_after(before, targets)
    print("Project purge HTTP execution/query/reference preview runtime probe: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
