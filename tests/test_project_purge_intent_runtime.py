"""Bounded portable purge-intent acceptance, acknowledgement and restart.

Production HTTP routes in tiny copied xs/TCC apps, two HTTP clients and
controlled publication faults. No user Home, external models or load tests.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import http.client
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
import time

from test_api_runtime import request
from test_project_purge_http_runtime import send, params, PURGE, RESULT
from test_project_purge_cancel_runtime import CANCEL
from test_project_purge_receipts_runtime import ROOT, ID, ReceiptProbe

INTENT = "/api/v1/project-purge-intent"
PREPARE = "/api/v1/projects/purge-probe/purge-intent"
FAULT = "/__fixture/project-purge-intent/fault"
ETAG = '"mdo-purge-intent-' + ID + '"'
PATH = "data/project-purge-intent.json"


class IntentProbe(ReceiptProbe):
    def __init__(self, host: Path, base: Path):
        super().__init__(host, base)
        shutil.copy2(ROOT / "tests/fixtures/project-purge-intent.c",
                     self.site / "src/bootstrap/project-purge-intent-probe.c")

        def change(relative: str, old: str, new: str):
            path = self.site / relative
            text = path.read_text(encoding="utf-8")
            assert text.count(old) == 1, (relative, old)
            path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

        change("src/bootstrap/service.c", "void ServiceInit(XS_HostInfo* pHost)",
               '#include "project-purge-intent-probe.c"\nvoid ServiceInit(XS_HostInfo* pHost)')
        change("src/bootstrap/service.c", "    if ( MdoPurgeReceiptFixtureControl(pRequest) ) return XS_OK;",
               "    if ( MdoIntentFixtureControl(pRequest) ) return XS_OK;\n"
               "    if ( MdoPurgeReceiptFixtureControl(pRequest) ) return XS_OK;")
        change("src/api/project_purge_intent.c", '#include "purge_intent.h"',
               '#include "purge_intent.h"\n'
               "bool MdoIntentFixtureWrite(cstr, const void*, size_t, bool);\n"
               "bool MdoIntentFixtureRemove(cstr, bool);")
        change("src/api/project_purge_intent.c", "MdoHomeAtomicWrite(MDO_PURGE_INTENT_PATH, Text, Size, false)",
               "MdoIntentFixtureWrite(MDO_PURGE_INTENT_PATH, Text, Size, false)")
        change("src/api/project_purge_intent.c", "MdoHomeRemove(MDO_PURGE_INTENT_PATH, false)",
               "MdoIntentFixtureRemove(MDO_PURGE_INTENT_PATH, false)")

    @property
    def intent_path(self):
        return self.home / PATH

    def intent_fault(self, value):
        self.api("POST", FAULT, {"value": value})

    def prepare(self, expected=200, body=None, headers=None):
        if body is None or headers is None:
            default_body, default_headers = params(self)
            if body is None:
                body = default_body
            if headers is None:
                headers = default_headers
        return send(self, "POST", PREPARE, body, expected, headers)

    def ack(self, expected=200, etag=ETAG):
        return send(self, "DELETE", INTENT, expected=expected, headers={"If-Match": etag})


@contextmanager
def running(host, base):
    probe = IntentProbe(host, base)
    try:
        probe.start()
        yield probe
    except BaseException as error:
        log = probe.log_path.read_text(encoding="utf-8", errors="replace") if probe.log_path.exists() else ""
        raise RuntimeError(f"{error}\n--- xs log ---\n{log[-6000:]}") from error
    finally:
        probe.stop()


def discard_reply(probe, method, path, payload=None, headers=None):
    _, current, _ = request(probe.port, "GET", "/api/v1/bootstrap")
    connection = http.client.HTTPConnection("127.0.0.1", probe.port, timeout=2)
    connection.request(method, path, json.dumps(payload).encode() if payload else None,
                       {"Content-Type": "application/json", **(headers or {}),
                        "X-Mdo-Write-Token": current["x-mdo-write-token"]})
    connection.close()


def crash_at(probe, method, path, body, headers, fault, checkpoint):
    errors = []
    probe.intent_fault(fault)

    def worker():
        try:
            send(probe, method, path, body, headers=headers)
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    deadline = time.monotonic() + 3
    marker = "purge_intent_checkpoint=" + checkpoint
    while marker not in probe.log_path.read_text(encoding="utf-8", errors="replace"):
        assert time.monotonic() < deadline, marker
        time.sleep(0.01)
    probe.stop(); thread.join(timeout=5)
    assert not thread.is_alive() and errors


def without_intent(probe):
    return {p: value for p, value in probe.published().items() if p != PATH}


def run_probe(host):
    with tempfile.TemporaryDirectory(prefix="purge-intent-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        with running(host, base / "normal") as probe:
            headers, body = send(probe, "GET", INTENT)
            assert body["data"] == {"intent": None, "replayed": False}
            assert headers["etag"] == '"mdo-purge-intent-empty"'
            send(probe, "HEAD", INTENT)
            for path, allow in ((INTENT, "GET, HEAD, DELETE, OPTIONS"), (PREPARE, "POST, OPTIONS")):
                assert send(probe, "OPTIONS", path)[0]["allow"] == allow
                send(probe, "PUT", path, {}, 405)
            send(probe, "DELETE", INTENT, expected=428)
            probe.ack(409)
            send(probe, "POST", PREPARE, {"purge_request_id": ID, "created_at": 1}, 404,
                 {"If-Match": '"mdo-project-purge-probe-1"'})
            assert not probe.home.exists()
            probe.seed()
            before, state = probe.files(), probe.control("state")
            payload, project_etag = params(probe)
            for body, headers, status in (({**payload, "created_at": payload["created_at"] + 1}, project_etag, 412),
                    ({**payload, "name": "client name is not trusted"}, project_etag, 422),
                    (payload, {"If-Match": '"mdo-project-purge-probe-2"'}, 412)):
                send(probe, "POST", PREPARE, body, status, headers)
                assert probe.files() == before
            tag, prepared = probe.prepare()
            intent = prepared["data"]["intent"]
            assert tag["etag"] == ETAG and intent == {
                "purge_request_id": ID, "project_id": "purge-probe", "revision": 1,
                "created_at": payload["created_at"], "name": "purge-probe"}
            assert not prepared["data"]["replayed"] and not probe.record().exists()
            assert without_intent(probe) == before and probe.control("state") == state
            stored = json.loads(probe.intent_path.read_text(encoding="utf-8"))
            assert stored == {"schema_version": 1, "intent": intent}
            assert not probe.intent_path.with_suffix(".json.bak").exists()
            stable = probe.files()
            assert probe.prepare()[1]["data"] == {"intent": intent, "replayed": True}
            for path in (PREPARE, PURGE, CANCEL):
                _, conflict = send(probe, "POST", path, {**payload, "purge_request_id": "e" * 32}, 409, project_etag)
                assert conflict["error"]["code"] == "purge_intent_conflict"
                assert probe.files() == stable
            # Unknown result cannot be acknowledged, including after a lost
            # execution reply: it is not evidence that no request will arrive.
            assert probe.ack(409)[1]["error"]["code"] == "purge_intent_unsettled"
            probe.ack(400, 'W/' + ETAG)
            probe.ack(412, '"mdo-purge-intent-' + "e" * 32 + '"')
            assert probe.files() == stable
            probe.stop(); probe.start()
            assert send(probe, "GET", INTENT)[1]["data"]["intent"] == intent
            assert probe.files() == stable
            send(probe, "POST", CANCEL, payload, headers=project_etag)
            assert probe.intent_path.exists()
            ack = probe.ack()[1]["data"]
            assert ack == {"intent": None, "replayed": False}
            assert without_intent(probe) == before
            assert probe.ack()[1]["data"] == {"intent": None, "replayed": True}
            # Late prepare must not resurrect an acknowledged/cancelled ID.
            send(probe, "POST", PREPARE, payload, 409, project_etag)
            send(probe, "POST", PURGE, payload, 409, project_etag)
            assert not probe.intent_path.exists() and without_intent(probe) == before

        with running(host, base / "commit") as probe:
            targets = probe.seed()
            before = probe.files()
            payload, headers = params(probe)
            probe.prepare()
            discard_reply(probe, "POST", PURGE, payload, headers)
            result = send(probe, "GET", RESULT)[1]["data"]
            assert result["committed"] and result["outcome"] == "committed"
            assert probe.intent_path.exists() and without_intent(probe) == probe.expected_after(before, targets)
            # Definition is gone, but preparing/retrieving this binding must
            # still replay the immutable name, not look up a current definition.
            send(probe, "POST", PREPARE, payload, headers=headers)
            discard_reply(probe, "DELETE", INTENT, headers={"If-Match": ETAG})
            assert send(probe, "GET", INTENT)[1]["data"]["intent"] is None
            assert probe.ack()[1]["data"]["replayed"]
            stable = probe.files()
            probe.api("POST", "/api/v1/projects", {"id": "purge-probe", "name": "Recreated"}, 201)
            new_stable = probe.files()
            send(probe, "POST", PREPARE, payload, 409, headers)
            send(probe, "POST", PURGE, payload, headers=headers)
            assert probe.files() == new_stable and stable != new_stable

        with running(host, base / "two-clients") as probe:
            probe.seed()
            payload, headers = params(probe)
            barrier = threading.Barrier(2)
            results, errors = [], []

            def worker(id):
                try:
                    barrier.wait(timeout=2)
                    results.append(request(probe.port, "POST", PREPARE,
                        body=json.dumps({**payload, "purge_request_id": id}).encode(),
                        headers={"Content-Type": "application/json", **headers}))
                except BaseException as error:
                    errors.append(error)

            threads = [threading.Thread(target=worker, args=(id,)) for id in (ID, "e" * 32)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=4)
            assert not errors and all(not thread.is_alive() for thread in threads), errors
            assert sorted(item[0] for item in results) == [200, 409], results
            intent = send(probe, "GET", INTENT)[1]["data"]["intent"]
            winner = json.loads(next(item[2] for item in results if item[0] == 200))["data"]["intent"]
            assert intent == winner and not probe.record(intent["purge_request_id"]).exists()

        with running(host, base / "definition-change") as probe:
            probe.seed()
            payload, headers = params(probe)
            original = probe.prepare()[1]["data"]["intent"]
            current = probe.api("GET", "/api/v1/projects/purge-probe")
            probe.api("PUT", "/api/v1/projects/purge-probe", {
                "name": "Edited 项目", "workspace_root": current["workspace_root"], "default_model_id": ""},
                extra=headers)
            stable = probe.files()
            assert probe.prepare(body=payload, headers=headers)[1]["data"]["intent"] == original
            send(probe, "POST", PURGE, payload, 412, headers)
            assert probe.files() == stable and not probe.record().exists()
            send(probe, "POST", CANCEL, payload, headers=headers)
            probe.ack()
            new_payload, new_headers = params(probe)
            new_payload["purge_request_id"] = "e" * 32
            fresh = probe.prepare(body=new_payload, headers=new_headers)[1]["data"]["intent"]
            assert fresh["revision"] == 2 and fresh["name"] == "Edited 项目"
            stable = probe.files()
            probe.ack(412)  # a delayed old acknowledgement cannot clear the new intent
            assert probe.files() == stable
            send(probe, "POST", CANCEL, new_payload, headers=new_headers)
            probe.ack(etag='"mdo-purge-intent-' + "e" * 32 + '"')

        for fault in (1, 2, 4, 5):
            with running(host, base / f"fault-{fault}") as probe:
                probe.seed()
                payload, headers = params(probe)
                if fault in (1, 2):
                    probe.intent_fault(fault)
                    probe.prepare(503 if fault == 1 else 200)
                    assert probe.intent_path.exists() is (fault == 2)
                    probe.intent_fault(0)
                    probe.prepare()
                else:
                    probe.prepare()
                    send(probe, "POST", CANCEL, payload, headers=headers)
                    stable = probe.files()
                    probe.intent_fault(fault)
                    probe.ack(503 if fault == 4 else 200)
                    assert probe.intent_path.exists() is (fault == 4)
                    if fault == 4:
                        assert probe.files() == stable
                    probe.intent_fault(0)
                    probe.ack()

        for fault, checkpoint in ((3, "saved"), (6, "acknowledged")):
            with running(host, base / checkpoint) as probe:
                probe.seed()
                before = probe.files()
                payload, headers = params(probe)
                if fault == 6:
                    probe.prepare()
                    send(probe, "POST", CANCEL, payload, headers=headers)
                crash_at(probe, "POST" if fault == 3 else "DELETE", PREPARE if fault == 3 else INTENT,
                    payload if fault == 3 else None, headers if fault == 3 else {"If-Match": ETAG}, fault, checkpoint)
                probe.start()
                assert probe.intent_path.exists() is (fault == 3)
                assert without_intent(probe) == before
                if fault == 3:
                    assert send(probe, "GET", INTENT)[1]["data"]["intent"]["purge_request_id"] == ID
                    send(probe, "POST", CANCEL, payload, headers=headers)
                assert probe.ack()[1]["data"]["intent"] is None

        with running(host, base / "frozen") as probe:
            probe.seed()
            before = probe.files()
            payload, headers = params(probe)
            probe.prepare(); probe.fault(1)
            send(probe, "POST", CANCEL, payload, 503, headers)
            stable = probe.files()
            assert send(probe, "GET", INTENT)[1]["data"]["intent"]["purge_request_id"] == ID
            probe.prepare()
            probe.ack(409)  # pending terminal publication, not safe to forget
            assert probe.files() == stable
            probe.stop(); probe.start()
            probe.ack()
            assert without_intent(probe) == before

        with running(host, base / "committed-frozen") as probe:
            probe.seed()
            payload, headers = params(probe)
            probe.prepare(); probe.control("fault-cleanup")
            result = send(probe, "POST", PURGE, payload, 503, headers)[1]["error"]["details"]
            assert result["outcome"] == "committed" and result["committed"]
            stable = probe.files()
            probe.prepare(body=payload, headers=headers)  # replay needs no current definition
            assert probe.ack(503)[1]["error"]["code"] == "purge_restart_required"
            assert probe.files() == stable
            probe.stop(); probe.start()
            probe.ack()
            assert send(probe, "GET", RESULT)[1]["data"]["committed"]

        with running(host, base / "damage") as probe:
            probe.seed(); probe.prepare()
            payload, headers = params(probe)
            original = probe.intent_path.read_bytes()
            data = json.loads(original)
            for altered in ({}, {**data, "extra": True}, {**data, "intent": None},
                    {**data, "schema_version": 2}, {**data, "intent": {**data["intent"], "revision": 0}},
                    {**data, "intent": {**data["intent"], "created_at": -1}},
                    {**data, "intent": {**data["intent"], "purge_request_id": "e" * 33}}):
                probe.intent_path.write_text(json.dumps(altered), encoding="utf-8")
                damaged = probe.files()
                send(probe, "GET", INTENT, expected=503)
                probe.prepare(503)
                send(probe, "POST", PURGE, payload, 503, headers)
                send(probe, "POST", CANCEL, payload, 503, headers)
                probe.ack(503)
                assert probe.files() == damaged and not probe.record().exists()
                probe.intent_path.write_bytes(original)
            for raw in (original[:-1], b"x" * 2049, b"\xff", original.replace(b'"schema_version":1', b'"schema_version":1,"schema_version":1')):
                probe.intent_path.write_bytes(raw)
                damaged = probe.files()
                send(probe, "GET", INTENT, expected=503)
                probe.ack(503)
                assert probe.files() == damaged
                probe.intent_path.write_bytes(original)
            probe.intent_path.unlink(); probe.intent_path.mkdir()
            damaged = probe.files()
            send(probe, "GET", INTENT, expected=503)
            probe.prepare(503)
            assert probe.files() == damaged and probe.intent_path.is_dir()
            probe.intent_path.rmdir(); probe.intent_path.write_bytes(original)
            # Known matching terminal required: do not erase a valid intent on
            # a result accidentally bound to a different project/incarnation.
            send(probe, "POST", CANCEL, payload, headers=headers)
            terminal = probe.record().read_bytes()
            result = json.loads(terminal)
            probe.record().write_text(json.dumps({**result, "created_at_us": result["created_at_us"] + 1}), encoding="utf-8")
            damaged = probe.files()
            assert probe.ack(409)[1]["error"]["code"] == "purge_request_conflict"
            assert probe.files() == damaged
            probe.record().write_bytes(terminal)
            probe.ack()
    print("Portable project purge intent runtime probe: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
