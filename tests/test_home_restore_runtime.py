"""Small storage-only restore transactions through real xs/TCC and native FS.

Fault hooks live only in a copied fixture. Process interruption is observed at
a precise checkpoint, with three tiny files; no stress or external service.
This does not substitute for the later semantic Stage/manager/API integration.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from test_home_runtime import ROOT, write_site


JOURNAL = ".mdo-session-restore"
GC = JOURNAL + "-cleanup"
SESSION = "1" * 32
STAGE = "restore-" + "2" * 32
TARGET = f"sessions/restore-project/{SESSION}"
ARTIFACT = "artifacts/run-00000000000000000001/00000000000000000001-tool.txt"
PAYLOAD = {"meta.json": b"meta-byte", "snapshot.json": b"snapshot-byte", ARTIFACT: b"artifact-byte"}
BASELINE = {"data/cache/webview2/cache.bin": b"portable-cache", "sessions/other/old.txt": b"keep-old"}

PROBE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>
static bool RestoreProbeStep(cstr Step);
static bool RestoreProbeRename(xroot Root, cstr Source, cstr Target);
#include "src/storage/home.c"

static bool RestoreProbeMode(cstr Mode) { return strcmp(getenv("MDO_RESTORE_MODE"), Mode) == 0; }
static void RestoreProbePause(void)
{
    printf("restore_checkpoint=1\n"); fflush(stdout); xrtSleep(8000000u);
}
static bool RestoreProbeStep(cstr Step)
{
    char Mode[80];
    snprintf(Mode, sizeof(Mode), "crash-%s", Step);
    if ( RestoreProbeMode(Mode) ) RestoreProbePause();
    snprintf(Mode, sizeof(Mode), "fail-%s", Step);
    if ( !RestoreProbeMode(Mode) ) return true;
    MdoHomeErrorSet(strcmp(Step, "preflight") == 0 ? XERR_UNSUPPORTED : XERR_IO,
        MDO_HOME_ERROR_STORAGE, "synthetic bounded restore fault"); return false;
}
static bool RestoreProbeWrite(xroot Root, cstr Path, cstr Text)
{
    xfileoptions Options; xfile File; bool Ok;
    xrtFileOptionsInit(&Options); Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE | XFILE_NOFOLLOW;
    Options.Mode = 0600u; File = xrtRootFileOpen(Root, Path, &Options);
    if ( File == NULL ) return false;
    Ok = xrtWriteFull(File, Text, strlen(Text), NULL) && xrtFlush(File);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
}
static bool RestoreProbeRename(xroot Root, cstr Source, cstr Target)
{
    bool Ok;
    if ( strstr(Target, "sessions/") != Target ) return xrtRootRenameNoReplace(Root, Source, Target);
    if ( !RestoreProbeStep("rename-before") ) return false;
    if ( RestoreProbeMode("collision-after-ready") ) {
        char Path[160];
        if ( !xrtRootDirCreate(Root, Target, 0700u) ) return false;
        snprintf(Path, sizeof(Path), "%s/meta.json", Target);
        if ( !RestoreProbeWrite(Root, Path, "foreign-target") ) return false;
    }
    Ok = xrtRootRenameNoReplace(Root, Source, Target);
    return RestoreProbeStep("rename-after") && Ok;
}
void ServiceInit(XS_HostInfo* Host)
{
    static const char* const Reserved[] = {
        ".mdo-session-restore/payload/bad", ".MDO-SESSION-RESTORE./bad",
        ".mdo-session-restore-cleanup /owner"
    };
    MdoHomeSessionRestore *Restore, *Second;
    xroot Parent = NULL, Other = NULL, Directory = NULL;
    MdoHomeSnapshot Snapshot;
    xfileinfo Identity = {0};
    bool Available, Cache, Staged = false, Ended, Committed = false, ReservedOk = true, Busy, Writable;
    size_t i; char Path[160];
    (void)Host;
    if ( !MdoHomeInit() ) { printf("restore_init=0\n"); fflush(stdout); return; }
    if ( RestoreProbeMode("inspect") ) { printf("restore_init=1\n"); fflush(stdout); return; }
    for ( i = 0u; i < 3u; ++i ) {
        xfile File = MdoHomeOpenWrite(Reserved[i], XFILE_CREATE);
        ReservedOk = ReservedOk && File == NULL;
        if ( File != NULL ) (void)xrtClose(File);
        xrtClearError();
    }
    if ( RestoreProbeMode("invalid") ) {
        bool Ok = MdoHomeSessionRestoreBegin("../bad", "11111111111111111111111111111111", &Parent) == NULL &&
            MdoHomeSessionRestoreBegin("restore-project", "old-id", &Parent) == NULL &&
            MdoHomeSessionRestoreBegin("restore-project", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", &Parent) == NULL && Parent == NULL;
        printf("restore_invalid=%d reserved=%d\n", Ok, ReservedOk); fflush(stdout); return;
    }
    Restore = MdoHomeSessionRestoreBegin("restore-project", "11111111111111111111111111111111", &Parent);
    if ( Restore == NULL ) {
        const xerror* Cause = xrtGetError();
        memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot); MdoHomeGetSnapshot(&Snapshot);
        printf("restore_begin=0 restart=%d unsupported=%d reserved=%d\n", Snapshot.RestartRequired,
            Cause != NULL && xrtErrorKind(Cause) == XERR_UNSUPPORTED, ReservedOk); fflush(stdout); return;
    }
    Second = MdoHomeSessionRestoreBegin("restore-project", "33333333333333333333333333333333", &Other);
    Busy = Second == NULL && Other == NULL && MdoHomeImportInspect(&Available, &Cache) && !Available &&
        !MdoHomePurgeAvailableLocked() &&
        MdoHomeAtomicWrite("data/unrelated-restore-probe.txt", "allowed", 7u, false) &&
        MdoHomeRemove("data/unrelated-restore-probe.txt", false); /* no concurrent callback in this fixture */
    xrtClearError();
    if ( !xrtRootDirCreate(Parent, "restore-22222222222222222222222222222222", 0700u) ) goto end;
    Directory = xrtRootOpenIn(Parent, "restore-22222222222222222222222222222222");
    if ( Directory == NULL || !RestoreProbeWrite(Directory, "meta.json", "meta-byte") ) goto end;
    (void)RestoreProbeStep("partial");
    Staged = RestoreProbeWrite(Directory, "snapshot.json", "snapshot-byte") &&
        xrtRootDirCreate(Directory, "artifacts", 0700u) &&
        xrtRootDirCreate(Directory, "artifacts/run-00000000000000000001", 0700u) &&
        RestoreProbeWrite(Directory, "artifacts/run-00000000000000000001/00000000000000000001-tool.txt", "artifact-byte");
    if ( RestoreProbeMode("unknown-payload") ) Staged = Staged && RestoreProbeWrite(Directory, "foreign.txt", "preserve");
end:
    (void)xrtRootStat(Parent, "restore-22222222222222222222222222222222", false, &Identity);
    if ( Directory != NULL ) (void)xrtRootClose(Directory);
    (void)xrtRootClose(Parent); Parent = NULL;
    (void)RestoreProbeStep("prepared");
    if ( RestoreProbeMode("collision") ) {
        MdoHomeCreateDirectory("sessions/restore-project/11111111111111111111111111111111");
        MdoHomeAtomicWrite("sessions/restore-project/11111111111111111111111111111111/meta.json", "foreign-target", 14u, false);
    }
    if ( RestoreProbeMode("wrong-identity") ) Identity.Identity ^= UINT64_C(1);
    Ended = MdoHomeSessionRestoreEnd(Restore,
        "restore-22222222222222222222222222222222", &Identity, !RestoreProbeMode("abort"), &Committed);
    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot); MdoHomeGetSnapshot(&Snapshot);
    Writable = MdoHomeAtomicWrite("data/unrelated-restore-probe.txt", "allowed", 7u, false);
    if ( Writable ) (void)MdoHomeRemove("data/unrelated-restore-probe.txt", false);
    snprintf(Path, sizeof(Path), "sessions/restore-project/11111111111111111111111111111111/%s", "meta.json");
    printf("restore_end=%d committed=%d staged=%d restart=%d busy=%d reserved=%d writable=%d\n",
        Ended, Committed, Staged, Snapshot.RestartRequired, Busy, ReservedOk, Writable); fflush(stdout);
}
'''


def fixture(site: Path) -> None:
    write_site(site)
    (site / "probe.c").write_text(PROBE, encoding="utf-8", newline="\n")
    leaf = site / "src/storage/home_restore.inc.c"
    text = leaf.read_text(encoding="utf-8")
    changes = {
        '    (void)xrtRootRenameNoReplace(g_MdoHome.Root, Source, Target);':
            '    (void)RestoreProbeRename(g_MdoHome.Root, Source, Target);',
        '    snprintf(Source, sizeof(Source), "%s/payload/%s", MDO_HOME_RESTORE_DIR, Record.Name);':
            '    if ( !RestoreProbeStep("ready") ) goto done;\n'
            '    snprintf(Source, sizeof(Source), "%s/payload/%s", MDO_HOME_RESTORE_DIR, Record.Name);',
        '    if ( !MdoHomeImportStat(MDO_HOME_RESTORE_GC, &Exists, &Info) ) return false;':
            '    if ( !RestoreProbeStep("gc") ) return false;\n'
            '    if ( !MdoHomeImportStat(MDO_HOME_RESTORE_GC, &Exists, &Info) ) return false;',
        '    for ( i = 0u; i < 5u; ++i ) {\n        snprintf(Path, sizeof(Path), "%s/%s", MDO_HOME_RESTORE_GC, Names[i]);':
            '    for ( i = 0u; i < 5u; ++i ) {\n'
            '        if ( i == 4u && !RestoreProbeStep("cleanup-tail") ) return false;\n'
            '        snprintf(Path, sizeof(Path), "%s/%s", MDO_HOME_RESTORE_GC, Names[i]);',
        '    /* Capability preflight moves only the empty owned directory. */':
            '    if ( !RestoreProbeStep("preflight") ) goto fail;\n'
            '    /* Capability preflight moves only the empty owned directory. */',
        '    *Parent = xrtRootOpenIn(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/payload");':
            '    if ( !RestoreProbeStep("owner") ) goto fail;\n'
            '    *Parent = xrtRootOpenIn(g_MdoHome.Root, MDO_HOME_RESTORE_DIR "/payload");',
    }
    for before, after in changes.items():
        assert text.count(before) == 1, before
        text = text.replace(before, after)
    leaf.write_text(text, encoding="utf-8", newline="\n")


def launch(host: Path, site: Path, home: Path, mode: str, log: Path, *, checkpoint: bool = False) -> str:
    env = dict(os.environ, MDO_HOME=str(home), MDO_RESTORE_MODE=mode)
    with log.open("wb") as output:
        process = subprocess.Popen([str(host), str(site / "xs.json")], cwd=site, env=env,
            stdout=output, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                text = log.read_text(encoding="utf-8", errors="replace")
                needles = ("restore_checkpoint=1",) if checkpoint else (
                    "restore_end=", "restore_begin=", "restore_init=", "restore_invalid=")
                # A native printf can become visible in several writes. Wait for
                # its complete record before stopping the owned fixture process.
                if any(line.endswith("\n") and line.startswith(needles)
                       for line in text.splitlines(keepends=True)):
                    assert process.poll() is None, text
                    break
                if process.poll() is not None:
                    raise AssertionError(text)
                time.sleep(0.025)
            else:
                raise AssertionError(log.read_text(encoding="utf-8", errors="replace"))
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
    return log.read_text(encoding="utf-8", errors="replace")


def seed(home: Path) -> None:
    for name, data in BASELINE.items():
        target = home / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def inventory(home: Path) -> dict[str, bytes]:
    return {p.relative_to(home).as_posix(): p.read_bytes() for p in home.rglob("*")
            if p.is_file() and p.name != ".mdo.lock"}


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="home-restore-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        fixture(site)
        log = base / "probe.log"
        published = dict(BASELINE, **{f"{TARGET}/{name}": data for name, data in PAYLOAD.items()})
        for mode, expected_end, committed in (
            ("publish", 1, 1), ("abort", 1, 0), ("fail-ready", 0, 0),
            ("fail-rename-before", 0, 0), ("fail-rename-after", 1, 1),
            ("collision", 0, 0), ("collision-after-ready", 0, 0), ("wrong-identity", 0, 0)):
            home = base / mode
            seed(home)
            output = launch(host, site, home, mode, log)
            assert f"restore_end={expected_end} committed={committed} staged=1 restart=0 busy=1 reserved=1 writable=1" in output, output
            expected = published if committed else dict(BASELINE)
            if mode.startswith("collision"):
                expected[f"{TARGET}/meta.json"] = b"foreign-target"
            assert inventory(home) == expected, (mode, inventory(home))
            assert not (home / JOURNAL).exists() and not (home / GC).exists()
            assert "restore_init=1" in launch(host, site, home, "inspect", log)
            assert inventory(home) == expected

        for mode in ("fail-gc", "fail-cleanup-tail"):
            home = base / mode
            seed(home)
            output = launch(host, site, home, mode, log)
            assert "restore_end=0 committed=1 staged=1 restart=1 busy=1 reserved=1 writable=0" in output, output
            assert (home / GC).is_dir()
            assert "restore_init=1" in launch(host, site, home, "inspect", log)
            assert inventory(home) == published
            assert not (home / GC).exists()

        for mode, committed in (
            ("crash-preflight", False), ("crash-owner", False), ("crash-partial", False),
            ("crash-prepared", False), ("crash-ready", False),
            ("crash-rename-after", True), ("crash-gc", True), ("crash-cleanup-tail", True)):
            home = base / mode
            seed(home)
            launch(host, site, home, mode, log, checkpoint=True)
            assert (home / JOURNAL).is_dir() or (home / GC).is_dir()
            assert "restore_init=1" in launch(host, site, home, "inspect", log), mode
            assert inventory(home) == (published if committed else BASELINE), mode
            assert not (home / JOURNAL).exists() and not (home / GC).exists()
            launch(host, site, home, "inspect", log)
            assert inventory(home) == (published if committed else BASELINE)

        home = base / "unsupported"
        seed(home)
        output = launch(host, site, home, "fail-preflight", log)
        assert "restore_begin=0 restart=0 unsupported=1 reserved=1" in output, output
        assert inventory(home) == BASELINE
        home = base / "invalid"
        output = launch(host, site, home, "invalid", log)
        assert "restore_invalid=1 reserved=1" in output and not home.exists(), output
        home = base / "begin-collision"
        seed(home)
        (home / TARGET).mkdir(parents=True)
        (home / TARGET / "meta.json").write_bytes(b"already-here")
        before = inventory(home)
        output = launch(host, site, home, "publish", log)
        assert "restore_begin=0 restart=0" in output and inventory(home) == before, output
        assert not (home / JOURNAL).exists()

        # Unknown objects block cleanup before any staged bytes are deleted.
        home = base / "unknown"
        seed(home)
        output = launch(host, site, home, "unknown-payload", log)
        assert "restore_end=0 committed=0 staged=1 restart=1" in output, output
        before = inventory(home)
        assert "restore_init=0" in launch(host, site, home, "inspect", log)
        assert inventory(home) == before
        (home / JOURNAL / "payload" / STAGE / "foreign.txt").unlink()
        assert "restore_init=1" in launch(host, site, home, "inspect", log)
        assert inventory(home) == BASELINE

        # Malformed evidence, replaced identities and ambiguous locations fail
        # without traversing a live target or silently erasing the journal.
        for mode in ("bad-owner", "bad-ready", "duplicate-key", "neither", "source-replaced", "parent-replaced",
                     "journal-replaced", "target-replaced", "two-journals"):
            home = base / mode
            seed(home)
            checkpoint_mode = "crash-rename-after" if mode == "target-replaced" else "crash-ready"
            launch(host, site, home, checkpoint_mode, log, checkpoint=True)
            if mode in ("bad-owner", "bad-ready"):
                record = home / JOURNAL / ("owner" if mode == "bad-owner" else "ready")
                value = json.loads(record.read_text())
                value["version"] = 9
                record.write_text(json.dumps(value), encoding="utf-8")
            elif mode == "duplicate-key":
                record = home / JOURNAL / "owner"
                text = record.read_text(encoding="utf-8")
                record.write_text(text[:-1] + ',"version":1}', encoding="utf-8")
            elif mode in ("parent-replaced", "journal-replaced"):
                source = home / JOURNAL / "payload" if mode == "parent-replaced" else home / JOURNAL
                source.rename(base / (mode + "-saved"))
                shutil.copytree(base / (mode + "-saved"), source)
            elif mode in ("neither", "source-replaced"):
                source = home / JOURNAL / "payload" / STAGE
                source.rename(base / (mode + "-saved"))
                if mode == "source-replaced":
                    shutil.copytree(base / (mode + "-saved"), source)
            elif mode == "target-replaced":
                (home / TARGET).rename(base / "target-saved")
                (home / TARGET).mkdir()
                (home / TARGET / "meta.json").write_bytes(b"foreign-target")
            else:
                (home / ".mdo-purge").mkdir()
            before = inventory(home)
            assert "restore_init=0" in launch(host, site, home, "inspect", log), mode
            assert inventory(home) == before, mode

        # No-follow intermediate directory links, including Windows junctions.
        home = base / "linked"
        seed(home)
        launch(host, site, home, "crash-prepared", log, checkpoint=True)
        outside = base / "outside"
        outside.mkdir()
        (outside / "sentinel.txt").write_bytes(b"never-follow")
        artifact_dir = home / JOURNAL / "payload" / STAGE / "artifacts"
        artifact_dir.rename(base / "saved-artifacts")
        try:
            artifact_dir.symlink_to(outside, target_is_directory=True)
        except OSError as error:
            if os.name != "nt" or error.winerror != 1314:
                raise
            subprocess.run(["cmd", "/c", "mklink", "/J", str(artifact_dir), str(outside)],
                check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert "restore_init=0" in launch(host, site, home, "inspect", log)
        assert (outside / "sentinel.txt").read_bytes() == b"never-follow"
        # Remove only this disposable link; TemporaryDirectory never recurses
        # through it. This path was just created inside this fixture.
        artifact_dir.rmdir() if os.name == "nt" else artifact_dir.unlink()
        print("Home session restore storage PASS: bounded publication/collision/identity/crash recovery; no live dispatch", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    run_probe(args.host.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
