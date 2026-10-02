"""Bounded real xs/TCC definition writer and publication binding probe.

One controlled writer thread, tiny project files and a fixed callback. The
test never stages a session, opens a model, or performs stress/high-load work.
"""
from __future__ import annotations

import argparse
import os
import shutil
import tempfile
from pathlib import Path

from test_home_runtime import ROOT, run_probe, write_site


def write_binding_site(site: Path) -> None:
    write_site(site)
    (site / "src/projects").mkdir(parents=True)
    for name in ("projects.h", "project_lifecycle.h", "project_binding.h"):
        shutil.copy2(ROOT / "app/include/mdo" / name, site / "include/mdo" / name)
    for name in ("lifecycle.c", "manager.c", "binding.c"):
        shutil.copy2(ROOT / "app/src/projects" / name, site / "src/projects" / name)
    manager = site / "src/projects/manager.c"
    text = manager.read_text(encoding="utf-8")
    needle = "    if ( !MdoHomeAtomicWrite(Path, Json, Size, false) ) {"
    assert text.count(needle) == 1
    manager.write_text(text.replace(needle,
        "    if ( !BindingProbeWriteCheckpoint() || !MdoHomeAtomicWrite(Path, Json, Size, false) ) {"),
        encoding="utf-8")
    binding = site / "src/projects/binding.c"
    text = binding.read_text(encoding="utf-8")
    needle = "File != NULL && !xrtClose(File)"
    assert text.count(needle) == 1
    binding.write_text(text.replace(needle, "File != NULL && !BindingProbeClose(File)"), encoding="utf-8")
    shutil.copy2(ROOT / "tests/fixtures/project-binding.c", site / "probe.c")
    for leaf in ("空间 alpha", "other"):
        (site / "workspaces" / leaf).mkdir(parents=True)
    (site / "workspaces/not-directory.txt").write_text("keep-file-root", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path,
        default=ROOT / ".build/host" / ("xs.exe" if os.name == "nt" else "xs"))
    args = parser.parse_args()
    host = args.host.resolve()
    prior = os.environ.get("MDO_BINDING_MODE")
    try:
        with tempfile.TemporaryDirectory(prefix="project-binding-", dir=ROOT / ".build") as raw:
            base = Path(raw)
            site, home = base / "site", base / "state"
            write_binding_site(site)
            # xsAppPath is the executable's directory, including in dev mode.
            # Copy the already verified standalone host so relative workspace
            # fixtures stay entirely inside this temporary test directory.
            local_host = site / ("xs-probe.exe" if os.name == "nt" else "xs-probe")
            shutil.copy2(host, local_host)
            os.environ["MDO_BINDING_MODE"] = "readonly"
            output = run_probe(local_host, site, home)
            assert "probe_ok=1" in output and "binding_calls=0" in output, output
            assert not home.exists(), "inspection or refused writer materialized Home"
            os.environ["MDO_BINDING_MODE"] = "write"
            output = run_probe(local_host, site, home)
            assert "probe_ok=1" in output and "binding_calls=6" in output, output
            assert (home / "data/binding-commit.txt").read_bytes() == b"committed"
            assert not (home / "projects/callback-new.json").exists()
            assert (site / "workspaces/not-directory.txt").read_bytes() == b"keep-file-root"
            assert (site / "workspaces/alpha-old").is_dir()
            assert (site / "workspaces/空间 alpha").is_dir()
            print(output.strip())
    finally:
        if prior is None:
            os.environ.pop("MDO_BINDING_MODE", None)
        else:
            os.environ["MDO_BINDING_MODE"] = prior
    print("project binding runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
