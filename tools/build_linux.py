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


def inspect_elf(path: Path, static: bool, maximum: str | None = "2.28") -> dict:
    headers = subprocess.check_output(["readelf", "-l", str(path)], text=True)
    dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    import re
    needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
    if static and ("INTERP" in headers or needed):
        raise build_mdo.BuildError("musl core is not static: " + str(path))
    if any(any(token in name.lower() for token in ("gtk", "webkit", "gdk", "x11", "wayland")) for name in needed):
        raise build_mdo.BuildError("Linux core unexpectedly links the GUI runtime")
    symbols = subprocess.check_output(["readelf", "-W", "--version-info", str(path)], text=True, env=dict(os.environ, LC_ALL="C"))
    imports = symbols.split("Version needs section", 1)[-1] if "Version needs section" in symbols else ""
    versions = sorted(set(re.findall(r"Name:\s+GLIBC_([0-9]+\.[0-9]+(?:\.[0-9]+)?)\b", imports)), key=lambda v: tuple(int(n) for n in v.split('.')))
    required = versions[-1] if versions else None
    if maximum is not None and required and tuple(map(int, required.split('.'))) > tuple(map(int, maximum.split('.'))):
        raise build_mdo.BuildError(f"{path} requires glibc {required}, maximum is {maximum}; use the baseline builder")
    return {"static_core": static, "elf_needed": needed, "glibc_required": required, "glibc_max": maximum}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path)
    parser.add_argument("--edition", choices=("both", "gui", "server"), default="both")
    parser.add_argument("--libc", choices=("both", "glibc", "musl"), default="both")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--musl-cc", default="musl-gcc")
    parser.add_argument("--gui-cc", default="gcc")
    parser.add_argument("--glibc-max", default="2.28", help="release ABI maximum (native disables the limit for development)")
    parser.add_argument("--baseline-root", help="prepared Debian 10 build root; bootstrap with tools/linux/bootstrap_baseline.sh")
    parser.add_argument("--sysroot", type=Path, help="musl target root with include/ and lib/libc.a")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--wsl", action="store_true", help="build using Windows Subsystem for Linux")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-host-build", action="store_true", help="repack using verified cached xs hosts")
    args = parser.parse_args(argv)
    try:
        lock = build_mdo.load_object(build_mdo.LOCK_PATH)
        source = build_mdo.find_xserver(args.xserver_root, lock)
        build_mdo.verify_dependencies(source, lock)
        if os.name == "nt":
            if not args.wsl:
                raise build_mdo.BuildError("Linux builds require Linux; on Windows pass --wsl")
            command = ["wsl", *(["--user", "root"] if args.baseline_root else []), "-e", "python3", wsl_path(Path(__file__)),
                       "--xserver-root", wsl_path(source), "--output-dir", wsl_path(args.output_dir),
                       "--edition", args.edition, "--libc", args.libc,
                       "--cc", args.cc, "--musl-cc", args.musl_cc, "--gui-cc", args.gui_cc]
            command += ["--glibc-max", args.glibc_max]
            if args.baseline_root:
                command += ["--baseline-root", args.baseline_root]
            if args.sysroot:
                command += ["--sysroot", wsl_path(args.sysroot)]
            if args.dry_run:
                command.append("--dry-run")
            if args.skip_host_build:
                command.append("--skip-host-build")
            build_mdo.run(command, ROOT, args.dry_run)
            return 0
        if sys.platform != 'linux':
            raise build_mdo.BuildError('Linux products require a Linux build environment')
        if args.baseline_root:
            # The root contains its own modern Python, compiler, headers and
            # libraries. Only a workspace bind is owned here; do not unmount a
            # mount provided by the caller/bootstrapper.
            baseline = Path(args.baseline_root).resolve()
            interpreter = baseline / "opt/mdo-build-python/bin/python3.12"
            if baseline == Path('/') or not interpreter.is_file():
                raise build_mdo.BuildError("baseline build root is not prepared; run tools/linux/bootstrap_baseline.sh")
            if os.geteuid() != 0:
                raise build_mdo.BuildError("--baseline-root requires root for chroot; run this build with sudo")
            target = baseline / ROOT.parent.relative_to('/')
            marker = target / ROOT.name / 'deps.lock'
            existing = marker.is_file() and marker.samefile(ROOT / 'deps.lock')
            owned = False
            owned_proc = False
            try:
                if not existing:
                    target.mkdir(parents=True, exist_ok=True)
                    if os.path.ismount(target):
                        raise build_mdo.BuildError("baseline workspace mount belongs to another directory")
                    subprocess.run(['mount', '--bind', str(ROOT.parent), str(target)], check=True)
                    owned = True
                proc = baseline / 'proc'
                if not os.path.ismount(proc):
                    proc.mkdir(exist_ok=True)
                    subprocess.run(['mount', '-t', 'proc', 'proc', str(proc)], check=True)
                    owned_proc = True
                command = ['chroot', str(baseline), '/opt/mdo-build-python/bin/python3.12', str(Path(__file__)),
                    '--xserver-root', str(source), '--output-dir', str(args.output_dir.resolve()),
                    '--edition', args.edition, '--libc', args.libc, '--cc', args.cc,
                    '--musl-cc', args.musl_cc, '--gui-cc', args.gui_cc, '--glibc-max', args.glibc_max]
                if args.sysroot:
                    command += ['--sysroot', str(args.sysroot.resolve())]
                if args.dry_run:
                    command.append('--dry-run')
                if args.skip_host_build:
                    command.append('--skip-host-build')
                build_mdo.run(command, ROOT, False)
                return 0
            finally:
                if owned_proc and os.path.ismount(baseline / 'proc'):
                    subprocess.run(['umount', str(baseline / 'proc')], check=True)
                if owned and os.path.ismount(target):
                    subprocess.run(['umount', str(target)], check=True)
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
                command += ["--glibc-max", args.glibc_max]
                if args.skip_host_build:
                    command.append("--skip-host-build")
                if libc == "musl":
                    command += ["--sysroot", str(sysroot or ROOT / ".build/linux-sysroot")]
                build_mdo.run(command, ROOT, args.dry_run)
                if args.dry_run:
                    continue
                receipt = {**build_mdo.load_object(directory / 'build.json'), "schema_version": 1, "platform": "linux", "architecture": architecture,
                           "edition": edition, "libc": libc, "xs_commit": lock["xserver"]["commit"],
                           "mdo_commit": build_mdo.git_head(ROOT), "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                           "bytes": binary.stat().st_size,
                           "gui_runtime": "GTK3 + WebKitGTK 4.1 or 4.0 (>= 2.30)" if edition == "gui" else None,
                           **inspect_elf(binary, libc == "musl", None if args.glibc_max == "native" else args.glibc_max)}
                (directory / "build.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
                shutil.copyfile(ROOT / "docs/linux.md", directory / "README.md")
        return 0
    except (build_mdo.BuildError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print("[mdo-linux] " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
