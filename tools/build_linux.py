#!/usr/bin/env python3
"""Build Linux desktop/server products with glibc or a static musl core.

The GTK window is a separate glibc executable embedded in GUI hosts. Static
musl applies to the core, never to the system WebKitGTK browser runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

import build_mdo

ROOT = build_mdo.ROOT


def wsl_path(path: Path) -> str:
    value = path.resolve().as_posix()
    if len(value) < 3 or value[1:3] != ":/":
        raise build_mdo.BuildError("WSL requires a local drive path: " + value)
    return "/mnt/" + value[0].lower() + value[2:]


def prepare_musl_sysroot(output: Path, include: Path | None = None, library: Path | None = None) -> Path:
    # This builder owns only its .build cache. A supplied --sysroot is consumed
    # unchanged and never enters this function. Replacing the cache avoids
    # mixing headers from an older compiler/distribution with the current CRT.
    if output.is_symlink() or not output.resolve().is_relative_to((ROOT / '.build').resolve()):
        raise build_mdo.BuildError('generated sysroot must stay inside the build workspace')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='musl-sysroot-', dir=output.parent) as temporary:
        staged = _copy_musl_sysroot(Path(temporary) / 'root', include, library)
        if output.exists():
            shutil.rmtree(output)
        staged.rename(output)
    return output.resolve()


def _copy_musl_sysroot(output: Path, include: Path | None, library: Path | None) -> Path:
    """Embed the target CRT and Linux UAPI; never copy the host glibc headers."""
    candidates = sorted(Path("/usr/include").glob("*-linux-musl"))
    include = include or (candidates[0] if len(candidates) == 1 else None)
    libraries = sorted(Path("/usr/lib").glob("*-linux-musl/libc.a"))
    library = library or (libraries[0] if len(libraries) == 1 else None)
    if include is None or library is None or not include.is_dir() or not library.is_file():
        raise build_mdo.BuildError("musl target headers/libc.a unavailable; install musl-tools or pass --sysroot")
    output.mkdir(parents=True, exist_ok=True)
    seen = set()
    sources = [(include, Path("include"))]
    sources += [(Path("/usr/include") / name, Path("include") / name) for name in ("linux", "asm-generic")]
    asm = sorted(Path("/usr/include").glob("*-linux-gnu/asm"))
    if len(asm) == 1:
        sources.append((asm[0], Path("include/asm")))
    for source, prefix in sources:
        if not source.is_dir():
            raise build_mdo.BuildError("missing Linux UAPI headers: " + str(source))
        for path in sorted(source.rglob("*")):
            if not path.is_file():
                continue
            relative = prefix / path.relative_to(source)
            key = relative.as_posix().casefold()
            if key in seen:
                continue  # The xs SDK's VFS keys are case insensitive.
            seen.add(key)
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    (output / "lib").mkdir(exist_ok=True)
    shutil.copyfile(library, output / "lib/libc.a")
    return output.resolve()


def inspect_elf(path: Path, static: bool) -> dict:
    headers = subprocess.check_output(["readelf", "-l", str(path)], text=True)
    dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    import re
    needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
    if static and ("INTERP" in headers or needed):
        raise build_mdo.BuildError("musl core is not static: " + str(path))
    if any(any(token in name.lower() for token in ("gtk", "webkit", "gdk", "x11", "wayland")) for name in needed):
        raise build_mdo.BuildError("Linux core unexpectedly links the GUI runtime")
    return {"static_core": static, "elf_needed": needed}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path)
    parser.add_argument("--edition", choices=("both", "gui", "server"), default="both")
    parser.add_argument("--libc", choices=("both", "glibc", "musl"), default="both")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--musl-cc", default="musl-gcc")
    parser.add_argument("--gui-cc", default="gcc")
    parser.add_argument("--sysroot", type=Path, help="musl target root with include/ and lib/libc.a")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--wsl", action="store_true", help="build using Windows Subsystem for Linux")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    try:
        lock = build_mdo.load_object(build_mdo.LOCK_PATH)
        source = build_mdo.find_xserver(args.xserver_root, lock)
        build_mdo.verify_dependencies(source, lock)
        if os.name == "nt":
            if not args.wsl:
                raise build_mdo.BuildError("Linux builds require Linux; on Windows pass --wsl")
            command = ["wsl", "-e", "python3", wsl_path(Path(__file__)),
                       "--xserver-root", wsl_path(source), "--output-dir", wsl_path(args.output_dir),
                       "--edition", args.edition, "--libc", args.libc,
                       "--cc", args.cc, "--musl-cc", args.musl_cc, "--gui-cc", args.gui_cc]
            if args.sysroot:
                command += ["--sysroot", wsl_path(args.sysroot)]
            if args.dry_run:
                command.append("--dry-run")
            build_mdo.run(command, ROOT, args.dry_run)
            return 0
        if sys.platform != 'linux':
            raise build_mdo.BuildError('Linux products require a Linux build environment')
        machine = platform.machine()
        architecture = {"x86_64": "x86_64", "aarch64": "arm64"}.get(machine)
        if architecture is None:
            raise build_mdo.BuildError("unsupported Linux host architecture: " + machine)
        libcs = ("glibc", "musl") if args.libc == "both" else (args.libc,)
        editions = ("gui", "server") if args.edition == "both" else (args.edition,)
        sysroot = args.sysroot
        if "musl" in libcs and not args.dry_run and sysroot is None:
            sysroot = prepare_musl_sysroot(ROOT / ".build/linux-sysroot")
        for libc in libcs:
            for edition in editions:
                directory = args.output_dir.resolve() / f"linux-{architecture}-{libc}-{edition}"
                binary = directory / ("mdo" if edition == "gui" else "mdo-server")
                command = [sys.executable, str(ROOT / "tools/build_mdo.py"),
                           "--xserver-root", str(source), "--edition", edition,
                           "--libc", libc, "--cc", args.musl_cc if libc == "musl" else args.cc,
                           "--gui-cc", args.gui_cc, "--output", str(binary)]
                if libc == "musl":
                    command += ["--sysroot", str(sysroot or ROOT / ".build/linux-sysroot")]
                build_mdo.run(command, ROOT, args.dry_run)
                if args.dry_run:
                    continue
                receipt = {**build_mdo.load_object(directory / 'build.json'), "schema_version": 1, "platform": "linux", "architecture": architecture,
                           "edition": edition, "libc": libc, "xs_commit": lock["xserver"]["commit"],
                           "mdo_commit": build_mdo.git_head(ROOT), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                           "bytes": binary.stat().st_size,
                           "gui_runtime": "GTK3 + WebKitGTK 4.1 (system libraries)" if edition == "gui" else None,
                           **inspect_elf(binary, libc == "musl")}
                (directory / "build.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
                shutil.copyfile(ROOT / "docs/linux.md", directory / "README.md")
        return 0
    except (build_mdo.BuildError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print("[mdo-linux] " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
