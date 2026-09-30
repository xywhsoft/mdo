"""Durable purge request/receipt replay, isolation and bounded crash recovery.

Copied xs/TCC applications only; no external model or user data is touched.
Tiny Homes exercise real publication edges, never stress or high-load tests.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import threading
import time

from test_project_purge_runtime import (ROOT, Probe, OK, INVALID,
    REVISION_CONFLICT, UNAVAILABLE, ABORTED, RESTART)

CONFLICT = 8
PENDING, COMMITTED, ROLLED_BACK = range(3)
CONTROL = "/__fixture/project-purge-request/"
ID = "d" * 32


class ReceiptProbe(Probe):
    def __init__(self, host: Path, base: Path):
        super().__init__(host, base)
        shutil.copy2(ROOT / "tests/fixtures/project-purge-receipts.c",
                     self.site / "src/bootstrap/project-purge-receipts-probe.c")

        def change(relative: str, old: str, new: str) -> None:
            path = self.site / relative
            source = path.read_text(encoding="utf-8")
            assert source.count(old) == 1, (relative, old)
            path.write_text(source.replace(old, new, 1), encoding="utf-8", newline="\n")

        change("src/bootstrap/service.c", "void ServiceInit(XS_HostInfo* pHost)",
            '#include "project-purge-receipts-probe.c"\nvoid ServiceInit(XS_HostInfo* pHost)')
        change("src/bootstrap/service.c", "    if ( MdoPurgeFixtureControl(pRequest) ) return XS_OK;",
            "    if ( MdoPurgeReceiptFixtureControl(pRequest) ) return XS_OK;\n"
            "    if ( MdoPurgeFixtureControl(pRequest) ) return XS_OK;")
        change("src/bootstrap/service.c", "    (void)MdoApiInit();",
            "    MdoReceiptFixtureInitState();\n"
            "    (void)MdoApiInit();")
        change("src/storage/home_purge.inc.c", "bool MdoPurgeFixtureCommitAllowed(void);",
            "bool MdoPurgeFixtureCommitAllowed(void);\n"
            "bool MdoReceiptFixturePublish(const MdoHomePurgeRequest*, MdoHomePurgeOutcome);\n"
            "bool MdoReceiptFixtureWrite(cstr, const void*, size_t);\n"
            "bool MdoReceiptFixtureRename(xroot, cstr, cstr);\nvoid MdoReceiptFixtureAccepted(void);")
        change("src/storage/home_purge.inc.c", "HasRequest && !MdoHomePurgeReceiptPublish(&Request,",
            "HasRequest && !MdoReceiptFixturePublish(&Request,")
        change("src/storage/home_purge.inc.c",
            '    Ok = Text != NULL && Size <= MDO_HOME_PURGE_RECEIPT_BYTES && MdoHomePurgeMarker("request", Text, Size);',
            '    Ok = Text != NULL && Size <= MDO_HOME_PURGE_RECEIPT_BYTES && MdoHomePurgeMarker("request", Text, Size);\n'
            "    if ( Ok ) MdoReceiptFixtureAccepted();")
        change("src/storage/home_purge.inc.c",
            'MdoHomeImportWrite(MDO_HOME_PURGE_DIR "/result.tmp", Text, Size)',
            'MdoReceiptFixtureWrite(MDO_HOME_PURGE_DIR "/result.tmp", Text, Size)')
        change("src/storage/home_purge.inc.c",
            'xrtRootRenameNoReplace(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/result.tmp", Path)',
            'MdoReceiptFixtureRename(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/result.tmp", Path)')
        change("src/projects/purge.c", '#include "../sessions/internal.h"',
            '#include "../sessions/internal.h"\nvoid MdoReceiptFixtureBeforeReferences(void);')
        change("src/projects/purge.c", "    References = MdoApiProjectReferencesBegin(ProjectId, Owner, Error);",
            "    MdoReceiptFixtureBeforeReferences();\n"
            "    References = MdoApiProjectReferencesBegin(ProjectId, Owner, Error);")

    def reject_startup(self) -> None:
        self.log = self.log_path.open("ab")
        start_size = self.log_path.stat().st_size
        self.process = subprocess.Popen([str(self.host), str(self.config)], cwd=self.site,
            env=dict(os.environ, MDO_HOME=str(self.home),
                MDO_LING_RESPONSES_URL="https://example.invalid/v1", MDO_LING_API_KEY="bounded-purge-fixture"),
            stdout=self.log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 3
        while b"receipt_fixture_init_failed=1" not in self.log_path.read_bytes()[start_size:]:
            assert time.monotonic() < deadline, "damaged journal unexpectedly started or did not reject"
            time.sleep(0.01)
        self.stop()

    def origin(self, project: str = "purge-probe") -> dict:
        info = self.api("GET", "/api/v1/projects/" + project)
        return {"project": project, "revision": info["revision"], "created_at": info["created_at"]}

    def execute(self, origin: dict, id: str = ID) -> dict:
        return self.api("POST", CONTROL + "execute", {"id": id, **origin})

    def receipt(self, id: str = ID, expected: int = 200) -> dict:
        return self.api("POST", CONTROL + "get", {"id": id}, expected)

    def fault(self, value: int) -> None:
        self.api("POST", CONTROL + "fault", {"value": value})

    def record(self, id: str = ID) -> Path:
        return self.home / "data/project-purges" / (id + ".json")

    def published(self) -> dict[str, bytes]:
        return {p: value for p, value in self.files().items()
            if not p.startswith("data/project-purges/") and not p.startswith(".mdo-purge")}


@contextmanager
def running(host: Path, base: Path):
    probe = ReceiptProbe(host, base)
    try:
        probe.start()
        yield probe
    except BaseException as error:
        output = probe.log_path.read_text(encoding="utf-8", errors="replace") if probe.log_path.exists() else ""
        raise RuntimeError(f"{error}\n--- xs log ---\n{output[-6500:]}") from error
    finally:
        probe.stop()


def crash(probe: ReceiptProbe, origin: dict, fault: int, checkpoint: str) -> None:
    errors: list[BaseException] = []
    probe.fault(fault)

    def execute() -> None:
        try:
            probe.execute(origin)
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=execute)
    thread.start()
    deadline = time.monotonic() + 3
    marker = "purge_receipt_checkpoint=" + checkpoint
    while marker not in probe.log_path.read_text(encoding="utf-8", errors="replace"):
        assert time.monotonic() < deadline, marker
        time.sleep(0.01)
    probe.stop()
    thread.join(timeout=5)
    assert not thread.is_alive() and errors


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="purge-receipts-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        with running(host, base / "normal") as probe:
            assert probe.receipt() == {"found": False} and not probe.home.exists()
            probe.api("POST", CONTROL + "reserved", {})
            assert not probe.home.exists()
            targets = probe.seed()
            before, origin, state = probe.files(), probe.origin(), probe.control("state")
            for invalid in ("A" * 32, "../receipt", "short"):
                assert probe.execute(origin, invalid)["status"] == INVALID
            assert probe.execute({**origin, "created_at": origin["created_at"] + 1})["status"] == REVISION_CONFLICT
            assert probe.execute({**origin, "revision": origin["revision"] + 1})["status"] == REVISION_CONFLICT
            assert probe.receipt() == {"found": False} and probe.files() == before
            result = probe.execute(origin)
            assert result["status"] == OK and result["committed"] and not result["replayed"], result
            assert result["request_id"] == ID and result["targets"] == len(targets), result
            assert probe.published() == probe.expected_after(before, targets)
            record = json.loads(probe.record().read_text(encoding="utf-8"))
            assert record["request_id"] == ID and record["outcome"] == "committed", record
            assert record["created_at_us"] == origin["created_at"] and record["revision"] == 1, record
            assert probe.receipt()["outcome"] == COMMITTED
            stable, current = probe.files(), probe.control("state")
            probe.api("POST", CONTROL + "reserved", {})
            assert probe.files() == stable and not (probe.home / "data-saved").exists()
            replay = probe.execute(origin)
            assert replay == {**result, "replayed": True}, (result, replay)
            assert probe.files() == stable and probe.control("state") == current
            # An ID never changes meaning after the original definition goes.
            assert probe.execute({**origin, "project": "purge-other"})["status"] == CONFLICT
            assert probe.execute({**origin, "revision": 2})["status"] == CONFLICT
            assert probe.execute({**origin, "created_at": origin["created_at"] + 1})["status"] == CONFLICT
            assert probe.files() == stable
            probe.api("POST", "/api/v1/projects", {"id": "purge-probe", "name": "Recreated"}, 201)
            recreated = probe.origin()
            assert recreated["revision"] == 1 and recreated["created_at"] != origin["created_at"]
            stable = probe.files()
            assert probe.execute(origin)["status"] == OK  # replay does not clear the new project
            assert probe.execute(recreated)["status"] == CONFLICT
            assert probe.execute(origin, "e" * 32)["status"] == REVISION_CONFLICT
            assert probe.files() == stable
            assert probe.control("state")["schedule_generation"] == state["schedule_generation"] + 1
            probe.stop(); probe.start()
            assert probe.execute(origin)["committed"] and probe.execute(origin)["replayed"]
            assert probe.files() == stable
            # Bad metadata cannot be treated as a missing result/new action.
            original = probe.record().read_bytes()
            for malformed in (b'{"unknown":true}', original.replace(b'"committed"', b'"pending"'),
                    original.replace(ID.encode(), b"f" * 32), original[:-1], b"x" * 2049):
                probe.record().write_bytes(malformed)
                corrupted = probe.files()
                probe.receipt(expected=503)
                assert probe.execute(origin)["status"] == UNAVAILABLE
                assert probe.files() == corrupted
                probe.record().write_bytes(original)

        with running(host, base / "collision") as probe:
            for project in ("purge-probe", "purge-other"):
                probe.api("POST", "/api/v1/projects", {"id": project, "name": project}, 201)
            result = probe.api("POST", CONTROL + "collision", {})
            assert {result["first"], result["second"]} == {OK, CONFLICT}, result
            receipt = probe.receipt("8" * 32)
            assert receipt["found"] and receipt["outcome"] == COMMITTED
            definitions = sorted(p.name for p in (probe.home / "projects").glob("*.json"))
            assert definitions == ["purge-other.json" if receipt["project"] == "purge-probe" else "purge-probe.json"]

        with running(host, base / "rollback") as probe:
            probe.seed()
            before, origin, state = probe.files(), probe.origin(), probe.control("state")
            probe.control("fault-rollback")
            result = probe.execute(origin)
            assert result["status"] == ABORTED and not result["committed"], result
            assert probe.receipt()["outcome"] == ROLLED_BACK and probe.published() == before
            probe.control("fault-none")
            stable = probe.files()
            replay = probe.execute(origin)
            assert replay["status"] == ABORTED and replay["replayed"] and not replay["committed"], replay
            assert probe.files() == stable and probe.control("state") == state
            assert probe.execute(origin, "e" * 32)["status"] == OK

        for rollback in (False, True):
            with running(host, base / ("unpublished-abort" if rollback else "unpublished-commit")) as probe:
                targets = probe.seed()
                before, origin = probe.files(), probe.origin()
                if rollback:
                    probe.control("fault-rollback")
                probe.fault(1)
                result = probe.execute(origin)
                assert result["status"] == RESTART and result["restart_required"], result
                assert result["committed"] is not rollback and not probe.record().exists(), result
                receipt = probe.receipt()
                assert receipt["outcome"] == PENDING and receipt["committed"] is not rollback, receipt
                stable = probe.files()
                replay = probe.execute(origin)
                assert replay["status"] == RESTART and replay["committed"] is not rollback, replay
                assert probe.files() == stable
                probe.control("frozen-write")
                assert probe.files() == stable
                probe.stop(); probe.start()
                receipt = probe.receipt()
                assert receipt["outcome"] == (ROLLED_BACK if rollback else COMMITTED), receipt
                assert probe.published() == (before if rollback else probe.expected_after(before, targets))
                stable = probe.files()
                replay = probe.execute(origin)
                assert replay["status"] == (ABORTED if rollback else OK) and replay["replayed"], replay
                assert probe.files() == stable

        for fault, checkpoint, committed in ((2, "partial", True), (3, "temporary", True),
                (4, "published", True), (6, "accepted", False)):
            with running(host, base / checkpoint) as probe:
                targets = probe.seed()
                before, origin = probe.files(), probe.origin()
                crash(probe, origin, fault, checkpoint)
                probe.start()
                assert probe.receipt()["outcome"] == (COMMITTED if committed else ROLLED_BACK)
                assert probe.published() == (probe.expected_after(before, targets) if committed else before)
                stable = probe.files()
                for _ in range(2):
                    result = probe.execute(origin)
                    assert result["replayed"] and result["committed"] is committed, result
                    assert probe.files() == stable
                    probe.stop(); probe.start()
                    assert probe.files() == stable

        with running(host, base / "close-error") as probe:
            targets = probe.seed()
            before, origin = probe.files(), probe.origin()
            probe.fault(5)
            result = probe.execute(origin)
            assert result["status"] == OK and result["committed"] and not result["restart_required"], result
            assert probe.receipt()["outcome"] == COMMITTED
            assert probe.published() == probe.expected_after(before, targets)

        for damage in ("missing-result", "wrong-outcome", "wrong-request", "foreign-temporary",
                       "active-wrong-outcome", "active-premature-abort"):
            with running(host, base / damage) as probe:
                probe.seed()
                origin = probe.origin()
                if damage == "foreign-temporary":
                    crash(probe, origin, 3, "temporary")
                    target = probe.home / ".mdo-purge/result.tmp"
                    target.write_bytes(b"foreign private scratch is evidence")
                elif damage in {"wrong-request", "active-wrong-outcome", "active-premature-abort"}:
                    probe.control("fault-unresolved")
                    result = probe.execute(origin)
                    assert result["status"] == RESTART and not result["committed"]
                    probe.stop()
                    target = probe.home / ".mdo-purge/request"
                    altered = json.loads(target.read_text(encoding="utf-8"))
                    if damage == "wrong-request":
                        altered["revision"] = 2
                        target.write_text(json.dumps(altered), encoding="utf-8")
                    else:
                        target = probe.record()
                        target.parent.mkdir(parents=True, exist_ok=True)
                        altered["outcome"] = "committed" if damage == "active-wrong-outcome" else "aborted"
                        target.write_text(json.dumps(altered), encoding="utf-8")
                else:
                    probe.control("fault-cleanup")
                    result = probe.execute(origin)
                    assert result["committed"] and result["restart_required"], result
                    probe.stop()
                    target = probe.record()
                    if damage == "missing-result":
                        target.unlink()
                    else:
                        document = json.loads(target.read_text(encoding="utf-8"))
                        document["outcome"] = "aborted"
                        target.write_text(json.dumps(document), encoding="utf-8")
                damaged = probe.files()
                probe.reject_startup()
                assert probe.files() == damaged  # no compensation or payload deletion on contradictory evidence

        with running(host, base / "quota") as probe:
            # Lower only the copied source's capacity to two records; exercise
            # the bound with two tiny aborts, never generate many operations.
            # Restart to compile the changed copy before creating any data.
            probe.stop()
            storage = probe.site / "src/storage/home_purge.inc.c"
            source = storage.read_text(encoding="utf-8")
            assert "#define MDO_HOME_PURGE_RECEIPT_LIMIT 1024u" in source
            storage.write_text(source.replace("#define MDO_HOME_PURGE_RECEIPT_LIMIT 1024u",
                "#define MDO_HOME_PURGE_RECEIPT_LIMIT 2u"), encoding="utf-8", newline="\n")
            probe.start()
            probe.seed()
            origin = probe.origin()
            probe.control("fault-rollback")
            for id in (ID, "e" * 32):
                assert probe.execute(origin, id)["status"] == ABORTED
            stable = probe.files()
            third = probe.execute(origin, "f" * 32)
            assert not third["committed"] and not third["restart_required"], third
            assert probe.receipt("f" * 32) == {"found": False} and probe.files() == stable
            assert probe.execute(origin)["replayed"] and probe.files() == stable
    print("Durable project purge request/receipt runtime probe: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
