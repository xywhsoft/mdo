"""Bounded cache-preserving Home transactions and process-interruption recovery.

Fault controls are injected only into a copied translation unit. Each payload
has seven tiny files; no external service or load/stress workload is used.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
import time
from pathlib import Path

from test_home_runtime import ROOT, write_site


PROBE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

static bool HomeProbeBefore(unsigned Step);
static bool HomeProbeAfter(unsigned Step);
static bool HomeProbeCommit(void);
static void HomeProbeCleanup(unsigned Step);
static bool HomeProbeRollback(void);
#include "src/storage/home.c"

static void HomeProbeCheckpoint(void)
{
    printf("import_checkpoint=1\n"); fflush(stdout);
    xrtSleep(8000u); /* Python terminates only after observing this live point. */
}

static bool HomeProbeFailure(void)
{
    MdoHomeErrorSet(XERR_IO, MDO_HOME_ERROR_STORAGE, "synthetic import failure");
    return false;
}

static bool HomeProbeBefore(unsigned Step)
{
    char Expected[40];
    const char* Mode = getenv("MDO_IMPORT_MODE");
    snprintf(Expected, sizeof(Expected), "fail-%u", Step);
    if ( strcmp(Mode, Expected) == 0 ) return HomeProbeFailure();
    if ( Step == 1u && strcmp(Mode, "crash-ready") == 0 ) HomeProbeCheckpoint();
    return true;
}

static bool HomeProbeAfter(unsigned Step)
{
    char Expected[40];
    const char* Mode = getenv("MDO_IMPORT_MODE");
    snprintf(Expected, sizeof(Expected), "crash-%u", Step);
    if ( strcmp(Mode, Expected) == 0 ) HomeProbeCheckpoint();
    snprintf(Expected, sizeof(Expected), "fail-after-%u", Step);
    if ( Step == 4u && strcmp(Mode, "fail-rollback") == 0 ) return HomeProbeFailure();
    return strcmp(Mode, Expected) == 0 ? HomeProbeFailure() : true;
}

static bool HomeProbeCommit(void)
{
    const char* Mode = getenv("MDO_IMPORT_MODE");
    if ( strcmp(Mode, "fail-commit") == 0 ) return HomeProbeFailure();
    if ( !MdoHomeImportMarker("committed", MDO_HOME_IMPORT_MAGIC) ) return false;
    if ( strcmp(Mode, "crash-committed") == 0 ) HomeProbeCheckpoint();
    return true;
}

static void HomeProbeCleanup(unsigned Step)
{
    if ( Step == 4u && strcmp(getenv("MDO_IMPORT_MODE"), "crash-cleanup") == 0 )
        HomeProbeCheckpoint();
}

static bool HomeProbeRollback(void)
{
    return strcmp(getenv("MDO_IMPORT_MODE"), "fail-rollback") == 0 ? HomeProbeFailure() : true;
}

static bool HomeProbeFrozen(void)
{
    xfile File = MdoHomeOpenWrite("should-not-exist.txt", XFILE_CREATE);
    char* Native = MdoHomeExternalPath("sessions/old-manager/journal.jsonl");
    bool Ok = File == NULL && Native == NULL &&
        !MdoHomeAtomicWrite("config/test.txt", "bad", 3u, false) &&
        !MdoHomeCreateDirectory("created") &&
        !MdoHomeRemove("config/test.txt", false) &&
        !MdoHomeRemoveEmptyDirectory("data/cache") &&
        !MdoHomeRenameNoReplace("data", "moved-cache") &&
        !MdoResourceMaterialize("config/defaults.json");
    if ( File != NULL ) (void)xrtClose(File);
    xrtFree(Native); xrtClearError();
    return Ok;
}

void ServiceInit(XS_HostInfo* Host)
{
    const char* Mode = getenv("MDO_IMPORT_MODE");
    xroot Stage = NULL;
    char* Path = NULL;
    MdoHomeImport* Import;
    MdoHomeSnapshot Snapshot;
    xfile Cache;
    char CacheBytes[16];
    bool Available = false, PreserveCache = false;
    bool Staged = true, Ended, Abort, Frozen, Reserved = true;
    size_t i;
    static const char* const ReservedPaths[] = {
        ".mdo-import", ".MDO-IMPORT./payload/config/a", ".mdo-import-cleanup /owner"
    };
    (void)Host;
    if ( !MdoHomeInit() ) { printf("import_init=0\n"); fflush(stdout); return; }
    if ( !MdoHomeImportInspect(&Available, &PreserveCache) ) {
        printf("import_inspect=0\n"); fflush(stdout); return;
    }
    if ( strcmp(Mode, "inspect") == 0 || strcmp(Mode, "crash-cleanup") == 0 ) {
        printf("import_inspect=1 available=%d cache=%d\n", Available, PreserveCache);
        fflush(stdout); return;
    }
    for ( i = 0u; i < sizeof(ReservedPaths) / sizeof(ReservedPaths[0]); ++i ) {
        xfile File = MdoHomeOpenWrite(ReservedPaths[i], XFILE_CREATE);
        Reserved = Reserved && File == NULL;
        if ( File != NULL ) (void)xrtClose(File);
    }
    Cache = MdoHomeOpenRead("data/cache/webview2/cache.bin");
    Import = MdoHomeImportBegin(&Stage, &Path);
    if ( Import == NULL || Stage == NULL || Path == NULL || Cache == NULL ) {
        printf("import_begin=0\n"); fflush(stdout); return;
    }
    Frozen = HomeProbeFrozen();
    for ( i = 0u; i < 7u; ++i ) {
        char Relative[96];
        xfileoptions Options;
        xfile File;
        Staged = Staged && xrtRootDirCreate(Stage, g_MdoHomeImportRoots[i], 0700u);
        snprintf(Relative, sizeof(Relative), "%s/test.txt", g_MdoHomeImportRoots[i]);
        xrtFileOptionsInit(&Options);
        Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE | XFILE_SYNC;
        Options.Mode = 0600u;
        File = xrtRootFileOpen(Stage, Relative, &Options);
        if ( File == NULL ) { Staged = false; break; }
        Staged = Staged && xrtWriteFull(File, g_MdoHomeImportRoots[i],
            strlen(g_MdoHomeImportRoots[i]), NULL) && xrtFlush(File);
        if ( !xrtClose(File) ) Staged = false;
    }
    if ( strcmp(Mode, "crash-preparing") == 0 ) HomeProbeCheckpoint();
    if ( !xrtRootClose(Stage) ) Staged = false;
    xrtFree(Path);
    Abort = strcmp(Mode, "abort") == 0;
    Ended = MdoHomeImportEnd(Import, Staged && !Abort);
    if ( !Ended ) {
        const xerror* Cause = xrtGetError();
        printf("import_error=%s\n", Cause != NULL ? xrtErrorMessage(Cause) : "none");
    }
    memset(&Snapshot, 0, sizeof(Snapshot)); Snapshot.Size = sizeof(Snapshot);
    (void)MdoHomeGetSnapshot(&Snapshot);
    if ( Snapshot.RestartRequired ) Frozen = Frozen && HomeProbeFrozen();
    else Frozen = Frozen && MdoHomeAtomicWrite("resumed.txt", "ok", 2u, false) &&
        MdoHomeRemove("resumed.txt", false);
    memset(CacheBytes, 0, sizeof(CacheBytes));
    Staged = Staged && xrtReadFull(Cache, CacheBytes, 10u, NULL) &&
        memcmp(CacheBytes, "cache-byte", 10u) == 0 && xrtClose(Cache);
    printf("import_end=%d staged=%d frozen=%d restart=%d reserved=%d\n",
        Ended, Staged, Frozen, Snapshot.RestartRequired, Reserved);
    fflush(stdout);
}

void ServiceUnit(XS_HostInfo* Host) { (void)Host; MdoHomeUnit(); }
'''


def site_fixture(site: Path) -> None:
    write_site(site)
    (site / "probe.c").write_text(PROBE, encoding="utf-8")
    source = site / "src/storage/home_import.inc.c"
    text = source.read_text(encoding="utf-8")
    move = "        if ( !xrtRootRenameNoReplace(g_MdoHome.Root, Source, g_MdoHomeImportRoots[i]) ) return false;"
    commit = '    return MdoHomeImportMarker("committed", MDO_HOME_IMPORT_MAGIC);'
    cleanup = '            snprintf(Path, sizeof(Path), "%s/%s", MDO_HOME_IMPORT_GC, Files[i]);'
    rollback = '                if ( !xrtRootRenameNoReplace(g_MdoHome.Root,\n                        g_MdoHomeImportRoots[i], Destination) ) return false;'
    assert text.count(move) == text.count(commit) == text.count(cleanup) == 1
    text = text.replace(move, '        if ( !HomeProbeBefore((unsigned)i + 1u) ) return false;\n' +
        move + '\n        if ( !HomeProbeAfter((unsigned)i + 1u) ) return false;')
    text = text.replace(commit, "    return HomeProbeCommit();")
    text = text.replace(cleanup, "            HomeProbeCleanup((unsigned)i);\n" + cleanup)
    assert text.count(rollback) == 1
    text = text.replace(rollback, '                if ( !HomeProbeRollback() || !xrtRootRenameNoReplace(g_MdoHome.Root,\n                        g_MdoHomeImportRoots[i], Destination) ) return false;')
    source.write_text(text, encoding="utf-8", newline="\n")


def launch(host: Path, site: Path, home: Path, mode: str, log: Path,
           *, checkpoint: bool = False) -> str:
    env = dict(os.environ, MDO_HOME=str(home), MDO_IMPORT_MODE=mode)
    with log.open("wb") as output:
        process = subprocess.Popen([str(host), str(site / "xs.json")],
            cwd=site, env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            needles = ("import_checkpoint=1",) if checkpoint else (
                "import_end=", "import_init=0", "import_inspect=")
            while time.monotonic() < deadline:
                text = log.read_text(encoding="utf-8", errors="replace")
                if any(needle in text for needle in needles):
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


def cache_home(home: Path) -> None:
    (home / "data/cache/webview2").mkdir(parents=True)
    (home / "data/cache/webview2/cache.bin").write_bytes(b"cache-byte")


def inventory(home: Path) -> dict[str, bytes]:
    return {p.relative_to(home).as_posix(): p.read_bytes()
            for p in home.rglob("*") if p.is_file() and p.name != ".mdo.lock"}


def run_probe(host: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="home-import-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        site = base / "site"
        site_fixture(site)
        baseline = {"data/cache/webview2/cache.bin": b"cache-byte"}
        roots = ("config", "secrets", "projects", "sessions", "memory", "schedules", "migration")
        for mode in ("abort", *(f"fail-{i}" for i in range(1, 8)),
                     "fail-after-4", "fail-after-7", "fail-commit", "fail-rollback"):
            home = base / mode
            cache_home(home)
            output = launch(host, site, home, mode, base / "probe.log")
            expected = "import_end=1" if mode == "abort" else "import_end=0"
            assert expected in output and "staged=1 frozen=1" in output and "reserved=1" in output, (mode, output)
            if "restart=1" in output:
                # A controlled failed rollback (or a real Windows handle lock)
                # must isolate writes, preserve the journal, and recover later.
                assert (home / ".mdo-import").exists() or (home / ".mdo-import-cleanup").exists(), output
                assert (home / "data/cache/webview2/cache.bin").read_bytes() == b"cache-byte"
                recovered = launch(host, site, home, "inspect", base / "failure-recover.log")
                assert "import_inspect=1 available=1 cache=1" in recovered, recovered
            else:
                assert mode != "fail-rollback", output
            assert inventory(home) == baseline, (mode, list(inventory(home)))
            assert not (home / ".mdo-import").exists(), mode
            assert not (home / ".mdo-import-cleanup").exists(), mode

        for mode in ("crash-preparing", "crash-ready", *(f"crash-{i}" for i in range(1, 8)),
                     "crash-committed"):
            home = base / mode
            cache_home(home)
            launch(host, site, home, mode, base / "probe.log", checkpoint=True)
            assert (home / ".mdo-import").is_dir(), mode
            output = launch(host, site, home, "inspect", base / "recover.log")
            committed = mode == "crash-committed"
            assert f"import_inspect=1 available={int(not committed)} cache={int(not committed)}" in output, output
            expected = dict(baseline)
            if committed:
                expected.update({f"{name}/test.txt": name.encode() for name in roots})
            assert inventory(home) == expected, (mode, list(inventory(home)))
            assert not (home / ".mdo-import").exists(), mode
            assert not (home / ".mdo-import-cleanup").exists(), mode
            # A second startup must not revisit a retired transaction.
            launch(host, site, home, "inspect", base / "recover-twice.log")
            assert inventory(home) == expected, mode

        home = base / "success"
        cache_home(home)
        output = launch(host, site, home, "publish", base / "probe.log")
        assert "import_end=1 staged=1 frozen=1 restart=1 reserved=1" in output, output
        assert (home / ".mdo-import/committed").read_bytes() == b"mdo-home-import-v1\n"
        launch(host, site, home, "crash-cleanup", base / "gc.log", checkpoint=True)
        assert not (home / ".mdo-import").exists()
        assert (home / ".mdo-import-cleanup/owner").is_file()
        output = launch(host, site, home, "inspect", base / "gc-recover.log")
        assert "available=0 cache=0" in output, output
        assert not (home / ".mdo-import-cleanup").exists()
        assert inventory(home) == dict(baseline, **{f"{n}/test.txt": n.encode() for n in roots})

        # Unrecognized data must never be treated as browser cache.
        for relative in ("user.txt", "data/queue/state.json", "data/cache/other/file"):
            home = base / ("ineligible-" + relative.replace("/", "-"))
            cache_home(home)
            target = home / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"preserve")
            before = inventory(home)
            output = launch(host, site, home, "inspect", base / "unknown.log")
            assert "available=0 cache=0" in output, output
            assert before == inventory(home)

        # Cache parents and staged payload must not traverse a directory link.
        # Keep the target inside this disposable fixture; its bytes never move.
        outside = base / "outside"
        outside.mkdir()
        sentinel = outside / "sentinel.bin"
        sentinel.write_bytes(b"never-follow")
        def directory_link(target: Path) -> None:
            try:
                target.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                if os.name != "nt" or error.winerror != 1314:
                    raise
                # A junction also exercises no-follow reparse handling and
                # requires no change to this machine's symlink permissions.
                subprocess.run(["cmd", "/c", "mklink", "/J", str(target), str(outside)],
                    check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        for relative in ("data", "data/cache", "data/cache/webview2"):
            home = base / ("linked-" + relative.replace("/", "-"))
            target = home / relative
            target.parent.mkdir(parents=True)
            directory_link(target)
            output = launch(host, site, home, "inspect", base / "links.log")
            assert "available=0 cache=0" in output, output
            assert os.path.samefile(target, outside) and sentinel.read_bytes() == b"never-follow"
        home = base / "linked-payload"
        cache_home(home)
        launch(host, site, home, "crash-ready", base / "probe.log", checkpoint=True)
        link = home / ".mdo-import/payload/memory/foreign"
        directory_link(link)
        output = launch(host, site, home, "inspect", base / "links.log")
        assert "import_init=0" in output, output
        assert os.path.samefile(link, outside) and sentinel.read_bytes() == b"never-follow"
        assert (home / "data/cache/webview2/cache.bin").read_bytes() == b"cache-byte"

        # An invalid manifest stays intact and prevents manager initialization.
        home = base / "invalid"
        cache_home(home)
        launch(host, site, home, "crash-ready", base / "probe.log", checkpoint=True)
        (home / ".mdo-import/ready").write_text('{"version":1,"roots":999}', encoding="utf-8")
        before = inventory(home)
        output = launch(host, site, home, "inspect", base / "invalid.log")
        assert "import_init=0" in output, output
        assert inventory(home) == before

        for mode in ("ambiguous", "replaced", "foreign-journal"):
            home = base / mode
            cache_home(home)
            launch(host, site, home, "crash-1", base / "probe.log", checkpoint=True)
            if mode == "ambiguous":
                duplicate = home / ".mdo-import/payload/config"
                duplicate.mkdir()
                (duplicate / "foreign.txt").write_bytes(b"never-delete")
            elif mode == "replaced":
                # Keep the original directory alive so its inode/file ID
                # cannot be recycled into the replacement before recovery.
                (home / "config").rename(base / "original-config")
                (home / "config").mkdir()
                (home / "config/foreign.txt").write_bytes(b"never-delete")
            else:
                (home / ".mdo-import/foreign.txt").write_bytes(b"never-delete")
            before = inventory(home)
            output = launch(host, site, home, "inspect", base / "invalid.log")
            assert "import_init=0" in output, (mode, output)
            assert inventory(home) == before, mode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    host = parser.parse_args().host.resolve()
    run_probe(host)
    print("Home import transaction runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
