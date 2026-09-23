#!/usr/bin/env python3
"""Run the bounded Windows/Linux mdo release gate.

The gate intentionally excludes stress and high-load tests.  It rebuilds the
locked host by default, compiles the mdo unity source with warnings as errors,
runs every bounded runtime probe, compares two packs, and performs the Windows
single-file startup checks.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import build_mdo


ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
BUILD = ROOT / ".build"


class GateError(RuntimeError):
    pass


def command_text(command: list[str]) -> str:
    return subprocess.list2cmdline(command)


def run(command: list[str], *, cwd: Path = ROOT) -> None:
    print(f"[qa] {command_text(command)}", flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            value.update(chunk)
    return value.hexdigest()


def strict_compile(cc: str, xserver: Path) -> None:
    includes = (
        xserver / "src/sdk",
        xserver / "lib",
        xserver / "lib/xwork",
        xserver / "lib/xllm",
        xserver / "lib/xllm-session",
        xserver / "tcc",
        xserver,
    )
    command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-fsyntax-only"]
    command.extend(item for path in includes for item in ("-I", str(path)))
    command.extend((
        "-DXS_USE_XLLM=1",
        "-DXS_USE_XLLM_SESSION=1",
        "-DXS_USE_XWORK=1",
        "-DXS_USE_WEBVIEW=1",
        str(ROOT / "app/generated/mdo_unity.c"),
    ))
    run(command)


def runtime_probes(host: Path) -> int:
    probes = sorted(TESTS.glob("test_*_runtime.py"), key=lambda path: path.name)
    if not probes:
        raise GateError("no bounded runtime probes were found")
    for probe in probes:
        run([sys.executable, str(probe), "--host", str(host)])
    return len(probes)


def wait_alive(process: subprocess.Popen[bytes], seconds: int) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        code = process.poll()
        if code is not None:
            raise GateError(f"single-file mdo exited early with status {code}")
        time.sleep(0.1)


def stop_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def windows_single_file(executable: Path, observe_seconds: int) -> None:
    with tempfile.TemporaryDirectory(prefix="mdo-release-clean-") as temporary:
        root = Path(temporary)
        target = root / "mdo.exe"
        shutil.copy2(executable, target)
        process = subprocess.Popen(
            [str(target)], cwd=root,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            wait_alive(process, observe_seconds)
        finally:
            stop_process(process)
        unexpected = sorted(path.name for path in root.iterdir()
                            if path.name != target.name)
        if unexpected:
            raise GateError(
                "single-file startup wrote unexpected side files: " +
                ", ".join(unexpected)
            )


def windows_packed_crash_gate(packer: Path, observe_seconds: int) -> None:
    shell = shutil.which("pwsh") or shutil.which("powershell")
    if shell is None:
        raise GateError("PowerShell is required for the packed startup gate")
    run([
        shell, "-NoProfile", "-File", str(TESTS / "packed-startup-smoke.ps1"),
        "-Xsw", str(packer),
        "-App", str(TESTS / "fixtures/packed-startup-app"),
        "-ObserveSeconds", str(observe_seconds),
    ])


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path,
                        help="xserver checkout at the deps.lock revision")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--skip-host-build", action="store_true",
                        help="reuse .build/host after dependency verification")
    parser.add_argument("--skip-gui-smoke", action="store_true",
                        help="skip Windows-only single-file GUI checks")
    parser.add_argument("--observe-seconds", type=int, default=5,
                        choices=range(5, 61), metavar="5..60")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        lock = build_mdo.load_object(ROOT / "deps.lock")
        xserver = build_mdo.find_xserver(args.xserver_root, lock)
        build_mdo.verify_dependencies(xserver, lock)
        output_dir = BUILD / "qa-release"
        output_dir.mkdir(parents=True, exist_ok=True)
        suffix = ".exe" if os.name == "nt" else ""
        first = output_dir / f"mdo-release-a{suffix}"
        second = output_dir / f"mdo-release-b{suffix}"
        host = BUILD / "host" / f"xs{suffix}"
        packer = host.with_name("xsw.exe") if os.name == "nt" else host

        run([sys.executable, "-m", "unittest", "discover", "-s", "tests"])
        build = [
            sys.executable, str(ROOT / "tools/build_mdo.py"),
            "--xserver-root", str(xserver), "--output", str(first),
            "--cc", args.cc,
        ]
        if args.skip_host_build:
            build.append("--skip-host-build")
        run(build)
        strict_compile(args.cc, xserver)
        probe_count = runtime_probes(host)
        run([str(packer), "pack", str(ROOT / "app"), "-o", str(second)])
        first_hash = digest(first)
        second_hash = digest(second)
        if first_hash != second_hash:
            raise GateError(
                f"pack is not deterministic: {first_hash} != {second_hash}"
            )

        if os.name == "nt" and not args.skip_gui_smoke:
            windows_single_file(first, args.observe_seconds)
            windows_packed_crash_gate(packer, max(args.observe_seconds, 20))

        print(f"[qa] PASS: unit/contract suite, {probe_count} runtime probes")
        print(f"[qa] PASS: deterministic pack sha256={first_hash}")
        if os.name == "nt" and not args.skip_gui_smoke:
            print("[qa] PASS: single-file zero-write and packed crash gates")
        return 0
    except (GateError, build_mdo.BuildError, KeyError, OSError,
            subprocess.CalledProcessError, ValueError) as error:
        print(f"[qa] FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
