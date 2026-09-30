"""Bounded cancellation/late-execution ordering and durable result recovery.

Production HTTP in copied xs/TCC apps; two native fixture threads control
acceptance order. Small synthetic Homes only, no model calls or load tests.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile
import threading
import time

from test_project_purge_http_runtime import send, params, PURGE, RESULT
from test_project_purge_receipts_runtime import ROOT, ID, CONTROL, running, COMMITTED, ROLLED_BACK
from test_project_purge_runtime import OK, ABORTED

CANCEL = "/api/v1/projects/purge-probe/purge-cancel"


def cancel(probe, expected=200):
    body, headers = params(probe)
    return send(probe, "POST", CANCEL, body, expected, headers)[1]


def assert_no_targets(data):
    assert data["accepted"] and data["outcome"] == "aborted" and not data["committed"], data
    for field in ("target_count", "file_count", "directory_count", "total_bytes", "schedule_count"):
        assert data[field] == 0, data
    assert not data["selection_removed"] and not data["global_draft_removed"], data


def interrupted_cancel(probe, fault, checkpoint):
    body, headers = params(probe)
    errors = []
    probe.fault(fault)

    def worker():
        try:
            send(probe, "POST", CANCEL, body, headers=headers)
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    deadline = time.monotonic() + 3
    marker = "purge_receipt_checkpoint=" + checkpoint
    while marker not in probe.log_path.read_text(encoding="utf-8", errors="replace"):
        assert time.monotonic() < deadline, marker
        time.sleep(0.01)
    probe.stop()
    thread.join(timeout=5)
    assert not thread.is_alive() and errors
    return body, headers


def run_probe(host: Path):
    with tempfile.TemporaryDirectory(prefix="purge-cancel-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        with running(host, base / "normal") as probe:
            headers, _ = send(probe, "OPTIONS", CANCEL)
            assert headers["allow"] == "POST, OPTIONS"
            send(probe, "GET", CANCEL, expected=405)
            body, headers = {"purge_request_id": ID, "created_at": 1}, {"If-Match": '"mdo-project-purge-probe-1"'}
            probe.api("POST", CONTROL + "cancel-invalid", {})
            send(probe, "POST", CANCEL, body, 503, headers)
            send(probe, "GET", RESULT, expected=404)
            assert not probe.home.exists()  # cancellation never materializes fallback
            probe.seed()
            before, state = probe.files(), probe.control("state")
            body, headers = params(probe)
            for payload, precondition, status in ((body, {}, 428),
                    (body, {"If-Match": 'W/"mdo-project-purge-probe-1"'}, 400),
                    (body, {"If-Match": '"mdo-project-purge-other-1"'}, 412),
                    (body, {"If-Match": '"mdo-project-purge-probe-18446744073709551615"'}, 422),
                    ({**body, "paths": ["../outside"]}, headers, 422),
                    ({**body, "created_at": 0}, headers, 422),
                    ({**body, "purge_request_id": "A" * 32}, headers, 422)):
                send(probe, "POST", CANCEL, payload, status, precondition)
                assert probe.files() == before
            data = cancel(probe)["data"]
            assert_no_targets(data)
            assert not data["replayed"] and not data["restart_required"]
            assert probe.published() == before and probe.control("state") == state
            stable = probe.files()
            assert cancel(probe)["data"] == {**data, "replayed": True}
            _, rejected = send(probe, "POST", PURGE, body, 409, headers)
            assert_no_targets(rejected["error"]["details"])
            assert rejected["error"]["code"] == "project_purge_aborted"
            assert probe.files() == stable and probe.control("state") == state
            for payload, etag in (({**body, "created_at": body["created_at"] + 1}, headers),
                    (body, {"If-Match": '"mdo-project-purge-probe-2"'})):
                _, conflict = send(probe, "POST", CANCEL, payload, 409, etag)
                assert conflict["error"]["code"] == "purge_request_conflict"
                assert not conflict["error"]["details"]["accepted"] and probe.files() == stable
            probe.stop(); probe.start()
            restored_state = probe.control("state")
            assert_no_targets(send(probe, "GET", RESULT)[1]["data"])
            assert cancel(probe)["data"] == {**data, "replayed": True}
            send(probe, "POST", PURGE, body, 409, headers)
            assert probe.files() == stable and probe.control("state") == restored_state
            # Zero statistics are ONLY an abort reservation. A forged commit
            # or nonzero bytes is damaged evidence, never a missing ID.
            original = probe.record().read_bytes()
            record = json.loads(original)
            for altered in ({**record, "outcome": "committed"}, {**record, "bytes": 1},
                            {**record, "directories": 1}):
                probe.record().write_text(json.dumps(altered), encoding="utf-8")
                damaged = probe.files()
                send(probe, "GET", RESULT, expected=503)
                result = cancel(probe, 503)["error"]["details"]
                assert result["accepted"] is None and result["outcome"] == "unknown", result
                assert probe.files() == damaged
                probe.record().write_bytes(original)
            # A new, separately reviewed ID may execute; the canceled ID stays
            # bound to its old incarnation even after same-name recreation.
            send(probe, "POST", PURGE, {**body, "purge_request_id": "e" * 32}, headers=headers)
            probe.api("POST", "/api/v1/projects", {"id": "purge-probe", "name": "Recreated"}, 201)
            stable = probe.files()
            send(probe, "POST", CANCEL, body, headers=headers)
            send(probe, "POST", PURGE, body, 409, headers)
            assert probe.files() == stable

        with running(host, base / "committed") as probe:
            targets = probe.seed()
            before = probe.files()
            body, headers = params(probe)
            data = send(probe, "POST", PURGE, body, headers=headers)[1]["data"]
            stable, state = probe.files(), probe.control("state")
            response = send(probe, "POST", CANCEL, body, headers=headers)[1]["data"]
            assert response == {**data, "replayed": True}, response
            assert response["committed"] and response["outcome"] == "committed"
            assert probe.published() == probe.expected_after(before, targets)
            assert probe.files() == stable and probe.control("state") == state

        with running(host, base / "accepted-execution") as probe:
            probe.seed()
            before = probe.files()
            body, headers = params(probe)
            probe.control("fault-unresolved")
            result = send(probe, "POST", PURGE, body, 503, headers)[1]["error"]["details"]
            assert result["outcome"] == "pending" and not result["committed"]
            stable = probe.files()
            cancelled = send(probe, "POST", CANCEL, body, 503, headers)[1]["error"]["details"]
            assert cancelled["accepted"] and cancelled["outcome"] == "pending" and cancelled["target_count"] > 0
            assert probe.files() == stable  # pending execution may NOT be overwritten by a cancel
            probe.stop(); probe.start()
            data = send(probe, "POST", CANCEL, body, headers=headers)[1]["data"]
            assert data["outcome"] == "aborted" and data["target_count"] > 0 and data["replayed"]
            assert probe.published() == before  # original rollback metadata preserved

        with running(host, base / "committed-frozen") as probe:
            probe.seed()
            body, headers = params(probe)
            probe.control("fault-cleanup")
            original = send(probe, "POST", PURGE, body, 503, headers)[1]["error"]["details"]
            assert original["committed"] and original["outcome"] == "committed"
            stable = probe.files()
            data = send(probe, "POST", CANCEL, body, 503, headers)[1]["error"]["details"]
            assert data == {**original, "replayed": True}, data
            assert probe.files() == stable

        for order in (1, 2):
            with running(host, base / f"order-{order}") as probe:
                targets = probe.seed()
                before, state = probe.files(), probe.control("state")
                result = probe.api("POST", CONTROL + "cancel-race", {"order": order})
                assert result["cancel_committed"] is (order == 2), result
                assert result["execution_committed"] is (order == 2), result
                assert result["cancel_replayed"] is (order == 2), result
                assert result["cancel_outcome"] == (COMMITTED if order == 2 else ROLLED_BACK), result
                assert (result["execution_status"] == OK) is (order == 2), result
                assert probe.published() == (probe.expected_after(before, targets) if order == 2 else before)
                if order == 1:
                    assert result["execution_status"] == ABORTED and result["execution_targets"] == 0, result
                    assert result["execution_replayed"], result
                    assert probe.control("state") == state  # no directory generation/cache changes

        with running(host, base / "unpublished") as probe:
            probe.seed()
            before = probe.files()
            body, headers = params(probe)
            probe.fault(1)
            result = cancel(probe, 503)["error"]["details"]
            assert result["accepted"] and result["outcome"] == "pending" and not result["committed"], result
            assert result["restart_required"] and not probe.record().exists()
            stable = probe.files()
            assert cancel(probe, 503)["error"]["details"] == result
            assert probe.files() == stable
            send(probe, "POST", PURGE, body, 503, headers)
            send(probe, "PUT", "/api/v1/pane-layout", {}, 503)
            # A different ID cannot start a cancellation while Home is frozen.
            send(probe, "POST", CANCEL, {**body, "purge_request_id": "f" * 32}, 503, headers)
            assert probe.files() == stable
            probe.stop(); probe.start()
            assert_no_targets(send(probe, "GET", RESULT)[1]["data"])
            assert probe.published() == before
            send(probe, "POST", PURGE, body, 409, headers)

        for fault, checkpoint in ((2, "partial"), (3, "temporary"),
                                  (4, "published"), (6, "accepted")):
            with running(host, base / checkpoint) as probe:
                probe.seed()
                before = probe.files()
                body, headers = interrupted_cancel(probe, fault, checkpoint)
                probe.start()
                data = send(probe, "GET", RESULT)[1]["data"]
                assert_no_targets(data)
                assert not data["restart_required"]
                assert probe.published() == before
                stable = probe.files()
                for _ in range(2):
                    current = probe.control("state")
                    send(probe, "POST", CANCEL, body, headers=headers)
                    send(probe, "POST", PURGE, body, 409, headers)
                    assert probe.files() == stable and probe.control("state") == current
                    probe.stop(); probe.start()
                    assert probe.files() == stable

        with running(host, base / "close-error") as probe:
            probe.seed()
            before = probe.files()
            probe.fault(5)
            data = cancel(probe)["data"]
            assert_no_targets(data)
            assert not data["restart_required"] and probe.published() == before

        with running(host, base / "quota") as probe:
            probe.stop()
            storage = probe.site / "src/storage/home_purge.inc.c"
            source = storage.read_text(encoding="utf-8")
            storage.write_text(source.replace("#define MDO_HOME_PURGE_RECEIPT_LIMIT 1024u",
                "#define MDO_HOME_PURGE_RECEIPT_LIMIT 2u"), encoding="utf-8", newline="\n")
            probe.start(); probe.seed()
            body, headers = params(probe)
            for id in (ID, "e" * 32):
                assert_no_targets(send(probe, "POST", CANCEL, {**body, "purge_request_id": id}, headers=headers)[1]["data"])
            stable = probe.files()
            send(probe, "POST", CANCEL, {**body, "purge_request_id": "f" * 32}, 503, headers)
            assert probe.files() == stable and not probe.record("f" * 32).exists()
            assert_no_targets(cancel(probe)["data"])
    print("Project purge cancellation runtime probe: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
