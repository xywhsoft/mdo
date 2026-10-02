"""Durable restore acceptance/results on the real xs/TCC/native filesystem.

One three-file transaction at precise interruption checkpoints. No model,
network provider, pressure loop or user Home. Storage evidence is independent
of the later worker/confirmation-page integration.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import tempfile
from pathlib import Path

from test_home_restore_runtime import (
    ROOT, JOURNAL, GC, SESSION, TARGET, PAYLOAD, BASELINE, PROBE,
    fixture as legacy_fixture, launch, seed, inventory,
)

RECEIPT = f"data/session-restores/{SESSION}.json"
SOURCE_SHA = hashlib.sha256(b"source-manifest").hexdigest()


def fixture(site: Path) -> None:
    legacy_fixture(site)
    source = PROBE
    old = '    if ( RestoreProbeMode("inspect") ) { printf("restore_init=1\\n"); fflush(stdout); return; }'
    new = r'''
    if ( RestoreProbeMode("contract") ) {
        MdoHomeSessionRestoreRequest Request = {0};
        MdoHomeSessionRestoreReceipt Receipt, Before;
        bool Found = true, Ok; xroot Output = NULL;
        Request.Size = sizeof(Request) - 1u;
        Ok = MdoHomeSessionRestoreBeginRequested(&Request, &Output) == NULL && Output == NULL;
        memset(&Receipt, 0xa5, sizeof(Receipt)); Receipt.Size = sizeof(Receipt) - 1u; Before = Receipt;
        Ok = Ok && !MdoHomeSessionRestoreReceiptGet("11111111111111111111111111111111", &Receipt, &Found) &&
            !Found && memcmp(&Receipt, &Before, sizeof(Receipt)) == 0;
        Receipt.Size = sizeof(Receipt); Found = true;
        Ok = Ok && !MdoHomeSessionRestoreReceiptGet("bad", &Receipt, &Found) && !Found &&
            Receipt.Size == sizeof(Receipt) && !Receipt.Committed && Receipt.Request.ProjectId[0] == '\0';
        printf("restore_invalid=%d\n", Ok); fflush(stdout); return;
    }
    if ( RestoreProbeMode("inspect") ) {
        MdoHomeSessionRestoreReceipt Receipt = {0}; bool Found = false, Ok;
        Receipt.Size = sizeof(Receipt); Ok = MdoHomeSessionRestoreReceiptGet("11111111111111111111111111111111", &Receipt, &Found);
        printf("restore_init=1 result_ok=%d found=%d outcome=%u result_commit=%d\n", Ok, Found, (unsigned)Receipt.Outcome, Receipt.Committed);
        fflush(stdout); return;
    }
'''
    assert source.count(old) == 1
    source = source.replace(old, new)
    old = '    Restore = MdoHomeSessionRestoreBegin("restore-project", "11111111111111111111111111111111", &Parent);'
    new = r'''
    {
        MdoHomeSessionRestoreRequest Request = {0};
        Request.Size = sizeof(Request); Request.ProjectRevision = 7u; Request.ProjectCreatedAt = 1234; Request.RestoredAt = 5678;
        snprintf(Request.ProjectId, sizeof(Request.ProjectId), "restore-project");
        snprintf(Request.SessionId, sizeof(Request.SessionId), "11111111111111111111111111111111");
        snprintf(Request.SourceSessionId, sizeof(Request.SourceSessionId), "source-session");
        snprintf(Request.SourceSha256, sizeof(Request.SourceSha256), "SOURCE_SHA");
        Restore = MdoHomeSessionRestoreBeginRequested(&Request, &Parent);
    }
    if ( Restore != NULL ) {
        MdoHomeSessionRestoreReceipt Receipt = {0}; bool Found;
        Receipt.Size = sizeof(Receipt);
        if ( !MdoHomeSessionRestoreReceiptGet("11111111111111111111111111111111", &Receipt, &Found) || !Found ||
             Receipt.Outcome != MDO_HOME_SESSION_RESTORE_PENDING || Receipt.Committed ||
             strcmp(Receipt.Request.SourceSha256, "SOURCE_SHA") != 0 ) abort();
        if ( MdoHomeRenameNoReplace("data", "moved-data") ||
             MdoHomeAtomicWrite("data/session-restores/x.json", "bad", 3u, false) ||
             MdoHomeAtomicWrite("DATA/SESSION-RESTORES./x.json", "bad", 3u, false) ||
             MdoHomeAtomicWrite("data/session-restores /x.json", "bad", 3u, false) ) abort();
        (void)RestoreProbeStep("accepted");
    }
'''.replace("SOURCE_SHA", SOURCE_SHA)
    assert source.count(old) == 1
    source = source.replace(old, new)
    old = '    printf("restore_end=%d committed=%d staged=%d restart=%d busy=%d reserved=%d writable=%d\\n",'
    new = r'''
    {
        MdoHomeSessionRestoreReceipt Receipt = {0}; bool Found;
        Receipt.Size = sizeof(Receipt);
        if ( !MdoHomeSessionRestoreReceiptGet("11111111111111111111111111111111", &Receipt, &Found) || !Found ||
             Receipt.Committed != Committed ) abort();
    }
''' + old
    assert source.count(old) == 1
    source = source.replace(old, new)
    (site / "probe.c").write_text(source, encoding="utf-8", newline="\n")
    leaf = site / "src/storage/home_restore_receipt.inc.c"
    text = leaf.read_text(encoding="utf-8")
    text = text.replace("#define MDO_HOME_RESTORE_RECEIPT_LIMIT 1024u", "#define MDO_HOME_RESTORE_RECEIPT_LIMIT 3u")
    old = '    if ( xrtRootRenameNoReplace(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/result.tmp", Path) ) Ok = true;'
    assert text.count(old) == 1
    text = text.replace(old, '    if ( !RestoreProbeStep("result") ) goto done;\n' + old)
    old = '    xrtFree(Text); xrtFree(Partial); xrtValueRelease(Value); return Ok;'
    assert text.count(old) == 1
    text = text.replace(old, '    if ( Ok && !RestoreProbeStep("result-after") ) Ok = false;\n' + old)
    leaf.write_text(text, encoding="utf-8", newline="\n")


def query(host: Path, site: Path, home: Path, log: Path, outcome: str | None) -> None:
    output = launch(host, site, home, "inspect", log)
    if outcome is None:
        assert "restore_init=1 result_ok=1 found=0" in output, output
        return
    committed = outcome == "committed"
    assert f"restore_init=1 result_ok=1 found=1 outcome={1 if committed else 2} result_commit={int(committed)}" in output, output
    receipt = json.loads((home / RECEIPT).read_bytes())
    assert receipt["version"] == 1 and receipt["outcome"] == outcome
    request = receipt["request"]
    assert request == {"version": 1, "project": "restore-project", "session": SESSION,
                       "source_session": "source-session", "source_sha256": SOURCE_SHA,
                       "project_revision": 7, "project_created_at": 1234, "restored_at": 5678}
    assert ("directory" in receipt) == committed


def check(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="restore-receipt-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site, log = base / "site", base / "probe.log"
        fixture(site)
        home = base / "absent"
        query(host, site, home, log, None)
        assert not home.exists(), "read-only result query materialized Home"
        assert "restore_invalid=1" in launch(host, site, home, "contract", log) and not home.exists()
        published = {f"{TARGET}/{name}": data for name, data in PAYLOAD.items()}
        for mode, commit, ended, restart in (
            ("publish", True, 1, 0), ("abort", False, 1, 0), ("fail-ready", False, 0, 0),
            ("fail-rename-before", False, 0, 0), ("fail-rename-after", True, 1, 0),
            ("collision-after-ready", False, 0, 0), ("fail-result", True, 0, 1),
            ("fail-result-after", True, 0, 1), ("fail-gc", True, 0, 1), ("fail-cleanup-tail", True, 0, 1)):
            home = base / mode
            seed(home)
            output = launch(host, site, home, mode, log)
            assert f"restore_end={ended} committed={int(commit)} staged=1 restart={restart}" in output, output
            query(host, site, home, log, "committed" if commit else "aborted")
            expected = dict(BASELINE, **(published if commit else {}))
            if mode == "collision-after-ready":
                expected[f"{TARGET}/meta.json"] = b"foreign-target"
            actual = inventory(home)
            assert {name: data for name, data in actual.items() if name != RECEIPT} == expected
            assert not (home / JOURNAL).exists() and not (home / GC).exists()
            before = inventory(home)
            assert "restore_begin=0 restart=0" in launch(host, site, home, "publish", log)
            assert inventory(home) == before, "an accepted ID executed a second restore"

        for mode, commit in (
            ("crash-accepted", False), ("crash-prepared", False), ("crash-ready", False),
            ("crash-rename-after", True), ("crash-result", True), ("crash-result-after", True),
            ("crash-gc", True), ("crash-cleanup-tail", True)):
            home = base / mode
            seed(home)
            launch(host, site, home, mode, log, checkpoint=True)
            query(host, site, home, log, "committed" if commit else "aborted")
            before = inventory(home)
            query(host, site, home, log, "committed" if commit else "aborted")
            assert inventory(home) == before and not (home / JOURNAL).exists() and not (home / GC).exists()

        # The result records a historical fact; deleting a later live session
        # cannot erase it or authorize the old ID again.
        home = base / "publish"
        target = (home / TARGET).resolve()
        assert target.is_relative_to(home.resolve())
        shutil.rmtree(target)
        before = inventory(home)
        query(host, site, home, log, "committed")
        assert "restore_begin=0 restart=0" in launch(host, site, home, "publish", log)
        assert inventory(home) == before

        # Recover only a canonical torn result prefix. Unknown scratch and
        # contradictory immutable terminal records preserve all evidence.
        for mode in ("prefix", "empty", "foreign", "conflict"):
            home = base / mode
            seed(home)
            launch(host, site, home, "crash-result", log, checkpoint=True)
            owner = json.loads((home / JOURNAL / "owner").read_bytes())
            ready = json.loads((home / JOURNAL / "ready").read_bytes())
            canonical = json.dumps({"version": 1, "request": owner["request"], "outcome": "committed",
                                    "directory": ready["identity"]}, separators=(",", ":")).encode()
            if mode == "conflict":
                path = home / RECEIPT
                path.parent.mkdir(parents=True, exist_ok=True)
                value = json.loads(canonical)
                value["request"]["source_sha256"] = "f" * 64
                path.write_text(json.dumps(value), encoding="utf-8")
            else:
                (home / JOURNAL / "result.tmp").write_bytes(canonical[:23] if mode == "prefix" else b"" if mode == "empty" else b"foreign-evidence")
            if mode in ("prefix", "empty"):
                query(host, site, home, log, "committed")
            else:
                before = inventory(home)
                assert "restore_init=0" in launch(host, site, home, "inspect", log)
                assert inventory(home) == before

        home = base / "capacity"
        seed(home)
        results = home / "data/session-restores"
        results.mkdir()
        for digit in ("7", "8", "9"):
            (results / (digit * 32 + ".json")).write_bytes(b"{}")
        before = inventory(home)
        assert "restore_begin=0 restart=0" in launch(host, site, home, "publish", log)
        assert inventory(home) == before and not (home / JOURNAL).exists()
        home = base / "damaged-terminal"
        seed(home)
        terminal = home / RECEIPT
        terminal.parent.mkdir()
        terminal.write_bytes(b"{}")
        before = inventory(home)
        assert "restore_init=1 result_ok=0 found=0" in launch(host, site, home, "inspect", log)
        assert inventory(home) == before
    print("durable restore request/result: native acceptance, pending/commit/abort, exact identity, restart/GC, torn/conflicting evidence, namespace/replay/capacity PASS", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    check(args.host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
