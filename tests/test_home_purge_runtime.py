"""Bounded project storage moves, rollback and process-interruption recovery.

Eleven tiny synthetic roots cover every move. Fault controls exist only in a
copied private source; this does not execute a product/API project deletion.
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

PATHS = sorted([
    "projects/probe.json", "projects/probe.json.bak",
    "data/project-drafts/probe.json", "data/project-drafts/probe.json.bak",
    "memory/projects/probe.json", "memory/projects/probe.json.bak",
    "sessions/probe", "migration/session-prompts/probe",
    "schedules/plan.json", "schedules/plan.json.bak", "schedules/history/plan.jsonl",
])

PROBE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>
static unsigned PurgeProbeLimit(void);
static bool PurgeProbeBefore(unsigned Step);
static bool PurgeProbeAfter(unsigned Step);
static bool PurgeProbeCommit(void);
static bool PurgeProbeRename(cstr Source, cstr Target);
static bool PurgeProbeRollback(void);
static void PurgeProbePhase(cstr Phase);
static bool PurgeProbeClean(cstr Path, bool Remove);
#include "src/storage/home.c"

static bool PurgeProbeMode(cstr Text) { return strcmp(getenv("MDO_PURGE_MODE"), Text) == 0; }
static void PurgeProbeCheckpoint(void) {
    printf("purge_checkpoint=1\n"); fflush(stdout); xrtSleep(8000u);
}
static bool PurgeProbeFailure(void) { return MdoHomePurgeError("synthetic purge fault"); }
static unsigned PurgeProbeLimit(void) { return PurgeProbeMode("limit") ? 4u : 8192u; }
static bool PurgeProbeBefore(unsigned Step) {
    char Mode[40]; snprintf(Mode, sizeof(Mode), "fail-%u", Step);
    if ( PurgeProbeMode(Mode) ) return PurgeProbeFailure();
    if ( Step == 1u && PurgeProbeMode("crash-ready") ) PurgeProbeCheckpoint();
    return true;
}
static bool PurgeProbeAfter(unsigned Step) {
    char Mode[40]; snprintf(Mode, sizeof(Mode), "crash-%u", Step);
    if ( PurgeProbeMode(Mode) ) PurgeProbeCheckpoint();
    if ( Step == 1u && PurgeProbeMode("global-crash-1") ) PurgeProbeCheckpoint();
    snprintf(Mode, sizeof(Mode), "fail-after-%u", Step);
    if ( PurgeProbeMode(Mode) || (Step == 4u && PurgeProbeMode("fail-rollback")) )
        return PurgeProbeFailure();
    return true;
}
static bool PurgeProbeRollback(void) {
    return PurgeProbeMode("fail-rollback") ? PurgeProbeFailure() : true;
}
static bool PurgeProbeCommit(void) {
    if ( PurgeProbeMode("fail-commit") || PurgeProbeMode("global-fail-commit") ) return PurgeProbeFailure();
    if ( !MdoHomePurgeMarker("committed", MDO_HOME_PURGE_MAGIC,
            sizeof(MDO_HOME_PURGE_MAGIC) - 1u) ) return false;
    if ( PurgeProbeMode("crash-committed") || PurgeProbeMode("global-crash-committed") ) PurgeProbeCheckpoint();
    return true;
}
static bool PurgeProbeRename(cstr Source, cstr Target) {
    bool Ok = xrtRootRenameNoReplace(g_MdoHome.Root, Source, Target);
    if ( Ok && PurgeProbeMode("close-error") ) return PurgeProbeFailure();
    return Ok;
}
static void PurgeProbePhase(cstr Phase) {
    char Mode[40]; snprintf(Mode, sizeof(Mode), "crash-%s", Phase);
    if ( PurgeProbeMode(Mode) ) PurgeProbeCheckpoint();
}
static bool PurgeProbeClean(cstr Path, bool Remove) {
    if ( !Remove || strcmp(Path, MDO_HOME_PURGE_GC "/payload/0001") != 0 ) return true;
    if ( PurgeProbeMode("fail-cleanup") ) return PurgeProbeFailure();
    if ( PurgeProbeMode("crash-cleanup") ) PurgeProbeCheckpoint();
    return true;
}

static int PurgeProbeCompare(const void* A, const void* B) {
    return strcmp(((const MdoHomePurgeTarget*)A)->Path, ((const MdoHomePurgeTarget*)B)->Path);
}

void ServiceInit(XS_HostInfo* Host) {
    static const char* const Paths[] = {
        "data/project-drafts/probe.json", "data/project-drafts/probe.json.bak",
        "memory/projects/probe.json", "memory/projects/probe.json.bak",
        "migration/session-prompts/probe", "projects/probe.json", "projects/probe.json.bak",
        "schedules/history/plan.jsonl", "schedules/plan.json", "schedules/plan.json.bak", "sessions/probe"
    };
    MdoHomePurgeTarget Targets[13];
    MdoHomeSnapshot Snapshot;
    xfile Held = NULL;
    bool Committed = false, Ok, Exists, Reserved = true;
    size_t i, Count = 11u;
    (void)Host;
    if ( !MdoHomeInit() ) { printf("purge_init=0\n"); fflush(stdout); return; }
    if ( PurgeProbeMode("inspect") ) { printf("purge_inspect=1\n"); goto done; }
    memset(Targets, 0, sizeof(Targets));
    if ( PurgeProbeMode("lazy") ) {
        snprintf(Targets[0].Path, sizeof(Targets[0].Path), "projects/probe.json");
        Targets[0].Info.Type = XFILE_TYPE_FILE; Targets[0].Info.Identity = 1u;
        Targets[0].Info.Available = XFILE_INFO_IDENTITY | XFILE_INFO_SIZE;
        Ok = MdoHomePurgeFiles("probe", Targets, 1u, &Committed);
    } else {
        for ( i = 0u; i < 11u; ++i ) {
            snprintf(Targets[i].Path, sizeof(Targets[i].Path), "%s", Paths[i]);
            if ( !MdoHomeExternalStat(Paths[i], &Exists, &Targets[i].Info) || !Exists ) {
                printf("purge_fixture=0\n"); goto done;
            }
        }
        if ( strncmp(getenv("MDO_PURGE_MODE"), "global-", 7u) == 0 ) {
            static const char* const Globals[] = { "data/draft.json", "data/workspace-state.json" };
            for ( i = 0u; i < 2u; ++i ) {
                snprintf(Targets[Count].Path, sizeof(Targets[Count].Path), "%s", Globals[i]);
                if ( !MdoHomeExternalStat(Globals[i], &Exists, &Targets[Count].Info) || !Exists ) {
                    printf("purge_fixture=0\n"); goto done;
                }
                ++Count;
            }
            qsort(Targets, Count, sizeof(Targets[0]), PurgeProbeCompare);
        }
        if ( PurgeProbeMode("invalid-path") ) snprintf(Targets[0].Path,
            sizeof(Targets[0].Path), "config/defaults.json");
        if ( PurgeProbeMode("stale-identity") ) ++Targets[0].Info.Identity;
        if ( PurgeProbeMode("duplicate") ) Targets[1] = Targets[0];
        if ( PurgeProbeMode("pending") &&
             !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR, 0700u) ) {
            printf("purge_fixture=0\n"); goto done;
        }
        if ( PurgeProbeMode("locked") ) {
            xfileoptions Options; xrtFileOptionsInit(&Options);
            Options.Flags = XFILE_READ | XFILE_NOFOLLOW; Options.Share = XFILE_SHARE_READ;
            Held = xrtRootFileOpen(g_MdoHome.Root, Targets[3].Path, &Options);
            if ( Held == NULL ) { printf("purge_fixture=0\n"); goto done; }
        }
        Ok = MdoHomePurgeFiles("probe", Targets, Count, &Committed);
    }
    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    (void)MdoHomeGetSnapshot(&Snapshot);
    for ( i = 0u; i < 4u; ++i ) {
        static const char* const PathsReserved[] = { ".mdo-purge", ".MDO-PURGE./owner",
            ".mdo-purge-cleanup/owner", ".MDO-PURGE-CLEANUP /ready" };
        Reserved = Reserved && !MdoHomeAtomicWrite(PathsReserved[i], "bad", 3u, false);
    }
    printf("purge_end=%u committed=%u frozen=%u reserved=%u\n", (unsigned)Ok,
        (unsigned)Committed, (unsigned)Snapshot.RestartRequired, (unsigned)Reserved);
    if ( Snapshot.RestartRequired ) {
        xfile Forbidden = MdoHomeOpenWrite("should-not-exist.txt", XFILE_CREATE);
        printf("purge_write_blocked=%u\n", (unsigned)(Forbidden == NULL));
        if ( Forbidden != NULL ) (void)xrtClose(Forbidden);
    }
done:
    if ( Held != NULL ) (void)xrtClose(Held);
    MdoHomeUnit(); printf("purge_finished=1\n"); fflush(stdout);
}
void ServiceUnit(XS_HostInfo* Host) { (void)Host; }
'''


def site_fixture(site: Path) -> None:
    write_site(site)
    (site / "probe.c").write_text(PROBE, encoding="utf-8")
    source = site / "src/storage/home_purge.inc.c"
    text = source.read_text(encoding="utf-8")
    move = "        if ( !MdoHomePurgeMove(Targets[i].Path, Slot, &Targets[i].Info) ) goto done;"
    commit = ('    if ( !MdoHomePurgeMarker("committed", MDO_HOME_PURGE_MAGIC,\n'
              '            sizeof(MDO_HOME_PURGE_MAGIC) - 1u) ) goto done;')
    rollback = "!MdoHomePurgeMove(Slot, Manifest.Targets[i].Path, &Manifest.Targets[i].Info)"
    assert text.count(move) == text.count(commit) == text.count(rollback) == 1
    text = text.replace(move, "        if ( !PurgeProbeBefore((unsigned)i + 1u) ) goto done;\n" +
        move + "\n        if ( !PurgeProbeAfter((unsigned)i + 1u) ) goto done;")
    text = text.replace(commit, "    if ( !PurgeProbeCommit() ) goto done;")
    text = text.replace(rollback, "(!PurgeProbeRollback() || " + rollback + ")")
    text = text.replace("(void)xrtRootRenameNoReplace(g_MdoHome.Root, Source, Target);",
                        "(void)PurgeProbeRename(Source, Target);")
    text = text.replace("if ( xrtRootRenameNoReplace(g_MdoHome.Root, Temporary, Target) )",
                        "if ( PurgeProbeRename(Temporary, Target) )")
    # Checkpoint after a fully flushed temporary marker, before publication.
    text = text.replace("    if ( PurgeProbeRename(Temporary, Target) )",
        '    if ( strcmp(Name, "ready") == 0 ) PurgeProbePhase("ready-temp");\n'
        '    else PurgeProbePhase("commit-temp");\n'
        "    if ( PurgeProbeRename(Temporary, Target) )")
    text = text.replace("    Created = true;", '    Created = true;\n    PurgeProbePhase("created");')
    text = text.replace('         !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/payload", 0700u)',
        '         (PurgeProbePhase("owner"), !xrtRootDirCreate(g_MdoHome.Root, MDO_HOME_PURGE_DIR "/payload", 0700u))')
    text = text.replace("    Ok = MdoHomePurgeGc();", '    PurgeProbePhase("retired");\n    Ok = MdoHomePurgeGc();')
    text = text.replace("    if ( Depth > 16u ||", "    if ( !PurgeProbeClean(Path, Remove) ) return false;\n    if ( Depth > 16u ||")
    text = text.replace("#define MDO_HOME_PURGE_NODE_LIMIT 8192u",
                        "#define MDO_HOME_PURGE_NODE_LIMIT PurgeProbeLimit()")
    marker_loop = "        snprintf(Path, sizeof(Path), \"%s/%s\", MDO_HOME_PURGE_GC, Files[i]);"
    assert text.count(marker_loop) == 1
    text = text.replace(marker_loop,
        '        if ( i == 4u ) PurgeProbePhase("cleanup-markers");\n' + marker_loop)
    source.write_text(text, encoding="utf-8", newline="\n")


def launch(host: Path, site: Path, home: Path, mode: str, *, checkpoint: bool = False) -> str:
    log = site.parent / "purge.log"
    env = dict(os.environ, MDO_HOME=str(home), MDO_PURGE_MODE=mode)
    with log.open("wb") as output:
        process = subprocess.Popen([str(host), str(site / "xs.json")], cwd=site,
            env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            # A visible printf prefix may be a partial native write. Wait for
            # the fixture's final, complete line before terminating it; later
            # frozen/write assertions must also have reached the log.
            needles = ("purge_checkpoint=1\n",) if checkpoint else (
                "purge_finished=1\n", "purge_init=0\n")
            while time.monotonic() < deadline:
                text = log.read_text(encoding="utf-8", errors="replace")
                if any(needle in text for needle in needles):
                    assert process.poll() is None, text
                    break
                if process.poll() is not None: raise AssertionError(text)
                time.sleep(0.025)
            else: raise AssertionError(log.read_text(encoding="utf-8", errors="replace"))
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=3)
    return log.read_text(encoding="utf-8", errors="replace")


def inventory(home: Path) -> dict[str, bytes]:
    result = {}
    for parent, directories, files in os.walk(home, followlinks=False):
        directories[:] = [name for name in directories if not (Path(parent) / name).is_symlink()
                          and not (os.name == "nt" and (Path(parent) / name).is_junction())]
        for name in files:
            path = Path(parent) / name
            if name != ".mdo.lock" and not path.is_symlink():
                result[path.relative_to(home).as_posix()] = path.read_bytes()
    return result


def seed(home: Path) -> dict[str, bytes]:
    for relative in PATHS:
        path = home / relative
        if relative in ("sessions/probe", "migration/session-prompts/probe"):
            path = path / "nested/record.bin"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(relative.encode())
    for relative in ("config/defaults.json", "data/cache/webview2/cache.bin",
                     "sessions/other/record.json", "projects/other.json", "memory/audit.jsonl",
                     "schedules/audit.jsonl", "migration/report.json", "data/workspace-state.json"):
        path = home / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"unrelated bytes")
    return inventory(home)


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="home-purge-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        site_fixture(site)
        lazy = base / "lazy"
        text = launch(host, site, lazy, "lazy")
        assert "purge_end=0 committed=0 frozen=0 reserved=1" in text and not lazy.exists(), text
        for mode in ("success", "close-error"):
            home = base / mode
            before = seed(home)
            text = launch(host, site, home, mode)
            assert "purge_end=1 committed=1 frozen=0 reserved=1" in text, (mode, text)
            expected = {p: b for p, b in before.items() if not any(
                p == root or p.startswith(root + "/") for root in PATHS)}
            assert inventory(home) == expected, mode
            assert "purge_inspect=1" in launch(host, site, home, "inspect")
            assert inventory(home) == expected, mode
        # Caller-approved conditional references use the same manifest and
        # recovery direction, including process interruption after their move.
        globals_owned = ["data/draft.json", "data/workspace-state.json"]
        for mode in ("global-success", "global-fail-commit", "global-crash-1",
                     "global-crash-committed"):
            home = base / mode
            seed(home)
            (home / "data/draft.json").write_bytes(b"caller-validated project draft")
            (home / "data/workspace-state.json").write_bytes(b"caller-validated project selection")
            before = inventory(home)
            crash = mode.startswith("global-crash-")
            text = launch(host, site, home, mode, checkpoint=crash)
            committed = mode in ("global-success", "global-crash-committed")
            if not crash:
                assert f"purge_end={int(committed)} committed={int(committed)} frozen=0 reserved=1" in text, text
            assert "purge_inspect=1" in launch(host, site, home, "inspect")
            expected = {p: b for p, b in before.items() if not committed or not any(
                p == root or p.startswith(root + "/") for root in PATHS + globals_owned)}
            assert inventory(home) == expected, mode
            assert "purge_inspect=1" in launch(host, site, home, "inspect")
            assert inventory(home) == expected, mode
        for mode in ([f"fail-{i}" for i in range(1, 12)] +
                     [f"fail-after-{i}" for i in (1, 4, 11)] +
                     ["fail-commit", "invalid-path", "stale-identity", "duplicate", "limit"]):
            home = base / mode
            before = seed(home)
            text = launch(host, site, home, mode)
            assert "purge_end=0 committed=0 frozen=0 reserved=1" in text, (mode, text)
            assert inventory(home) == before, mode
            assert not (home / ".mdo-purge").exists() and not (home / ".mdo-purge-cleanup").exists()
        for mode in ([f"crash-{i}" for i in range(1, 12)] +
                     ["crash-created", "crash-owner", "crash-ready-temp", "crash-ready", "crash-commit-temp", "crash-committed",
                      "crash-retired", "crash-cleanup", "crash-cleanup-markers"]):
            home = base / mode
            before = seed(home)
            launch(host, site, home, mode, checkpoint=True)
            text = launch(host, site, home, "inspect")
            assert "purge_inspect=1" in text, (mode, text)
            committed = mode in ("crash-committed", "crash-retired", "crash-cleanup", "crash-cleanup-markers")
            expected = {p: b for p, b in before.items() if not committed or not any(
                p == root or p.startswith(root + "/") for root in PATHS)}
            assert inventory(home) == expected, mode
            assert "purge_inspect=1" in launch(host, site, home, "inspect")
            assert inventory(home) == expected, mode
        for mode in ("fail-rollback", "fail-cleanup", "locked", "pending"):
            home = base / mode
            before = seed(home)
            text = launch(host, site, home, mode)
            committed = mode == "fail-cleanup" or (mode == "locked" and os.name != "nt")
            assert f"committed={int(committed)}" in text, (mode, text)
            if mode != "locked":
                assert "frozen=1" in text and "purge_write_blocked=1" in text, (mode, text)
            text = launch(host, site, home, "inspect")
            assert "purge_inspect=1" in text, (mode, text)
            expected = {p: b for p, b in before.items() if not committed or not any(
                p == root or p.startswith(root + "/") for root in PATHS)}
            assert inventory(home) == expected, mode

        # All ambiguity cases must reject before any compensating move.
        for mode in ("both", "neither", "identity", "bytes", "manifest-path", "foreign", "marker",
                     "directory-identity", "journal-identity", "payload-identity", "early-commit", "import-conflict"):
            home = base / mode
            seed(home)
            launch(host, site, home, "crash-11" if mode == "directory-identity" else "crash-4", checkpoint=True)
            manifest_path = home / ".mdo-purge/ready"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            target = home / manifest["targets"][0]["path"]
            slot = home / ".mdo-purge/payload/0000"
            if mode == "both": target.write_bytes(b"foreign replacement")
            elif mode in ("neither", "identity"):
                saved = base / (mode + "-original")
                assert slot.resolve().is_relative_to(base.resolve()) and saved.parent.resolve() == base.resolve()
                slot.rename(saved)
                if mode == "identity": slot.write_bytes(saved.read_bytes())
            elif mode == "manifest-path":
                manifest["targets"][0]["path"] = "config/defaults.json"
                manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            elif mode == "foreign": (home / ".mdo-purge/unknown.txt").write_bytes(b"never delete")
            elif mode == "bytes": slot.write_bytes(slot.read_bytes() + b"unexpected modification")
            elif mode in ("directory-identity", "journal-identity", "payload-identity"):
                changed = home / ({"directory-identity": ".mdo-purge/payload/0010",
                    "journal-identity": ".mdo-purge", "payload-identity": ".mdo-purge/payload"}[mode])
                saved = base / (mode + "-original")
                assert changed.resolve().is_relative_to(base.resolve()) and saved.parent.resolve() == base.resolve()
                changed.rename(saved)
                if mode == "directory-identity": changed.mkdir()
                else: shutil.copytree(saved, changed)
            elif mode == "early-commit":
                (home / ".mdo-purge/committed").write_bytes(b"mdo-project-purge-v1\n")
            elif mode == "import-conflict": (home / ".mdo-import").mkdir()
            else: (home / ".mdo-purge/committed").write_bytes(b"invalid marker")
            before = inventory(home)
            text = launch(host, site, home, "inspect")
            assert "purge_init=0" in text, (mode, text)
            assert inventory(home) == before, mode

        outside = base / "outside"
        outside.mkdir()
        sentinel = outside / "sentinel.bin"
        sentinel.write_bytes(b"never-follow")
        for mode in ("link-source", "link-payload"):
            home = base / mode
            seed(home)
            if mode == "link-payload": launch(host, site, home, "crash-11", checkpoint=True)
            link = home / ("sessions/probe/foreign" if mode == "link-source" else ".mdo-purge/payload/0010/foreign")
            try: link.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                if os.name != "nt" or error.winerror != 1314: raise
                subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(outside)],
                               check=True, capture_output=True)
            try:
                before = inventory(home)
                text = launch(host, site, home, "success" if mode == "link-source" else "inspect")
                assert ("purge_end=0 committed=0" if mode == "link-source" else "purge_init=0") in text, text
                assert inventory(home) == before and sentinel.read_bytes() == b"never-follow"
            finally:
                assert link.parent.resolve().is_relative_to(home.resolve())
                if link.is_symlink(): link.unlink()
                else: link.rmdir()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    run_probe(parser.parse_args().host.resolve())
    print("Home project purge transaction runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
