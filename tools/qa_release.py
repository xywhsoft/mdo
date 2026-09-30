#!/usr/bin/env python3
"""Run the bounded Windows/Linux mdo release gate.

The gate intentionally excludes stress and high-load tests.  It rebuilds the
locked host by default, compiles the mdo unity source with warnings as errors,
runs every bounded runtime probe, compares two packs, and performs the Windows
single-file startup checks.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

import build_mdo


ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
BUILD = ROOT / ".build"


class GateError(RuntimeError):
    pass


@contextmanager
def temporary_gate_directory(prefix: str):
    root = Path(tempfile.mkdtemp(prefix=prefix))
    try:
        yield root
    finally:
        temp_root = Path(tempfile.gettempdir()).resolve()
        actual = root.resolve()
        if actual.parent != temp_root or not actual.name.startswith(prefix):
            raise GateError(f"refusing to clean unexpected temporary path: {actual}")
        deadline = time.monotonic() + 5
        while root.exists():
            try:
                shutil.rmtree(root)
            except PermissionError as error:
                if time.monotonic() >= deadline:
                    raise GateError("portable WebView2 profile stayed open after exit") from error
                time.sleep(0.2)


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


def frontend_checks() -> None:
    node = shutil.which("node")
    if node is None:
        raise GateError(
            "Node.js is required for frontend release checks; "
            "build_mdo.py remains Node-free"
        )
    run([node, "--experimental-vm-modules",
         str(ROOT / "tools/check_web_modules.mjs")])
    tests = sorted(TESTS.glob("*.mjs"), key=lambda path: path.name)
    if not tests:
        raise GateError("no frontend interaction tests were found")
    run([node, "--test", *(str(path) for path in tests)])


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
    with temporary_gate_directory("mdo-release-clean-") as root:
        first = root / "portable-first"
        first.mkdir()
        # A unique name also makes any accidental AppData fallback observable.
        name = f"mdo-portable-{uuid.uuid4().hex}.exe"
        target = first / name
        shutil.copy2(executable, target)

        def launch(path: Path, *, arguments: tuple[str, ...] = (),
                   environment: dict[str, str] | None = None) -> None:
            env = os.environ.copy()
            env.pop("MDO_HOME", None)
            env["XS_APP_AUTOCLOSE_MS"] = str(observe_seconds * 1000)
            if environment:
                env.update(environment)
            process = subprocess.Popen(
                [str(path), *arguments], cwd=path.parent, env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            try:
                wait_alive(process, max(1, observe_seconds - 1))
                if process.wait(timeout=observe_seconds + 10) != 0:
                    raise GateError("portable GUI did not close cleanly")
            except subprocess.TimeoutExpired as error:
                raise GateError("portable GUI did not auto-close") from error
            finally:
                stop_process(process)

        def profile(home: Path) -> Path:
            path = home / "data/cache/webview2"
            if not path.is_dir():
                raise GateError(f"WebView2 did not use portable Home: {path}")
            return path

        launch(target)
        marker = profile(first / "mdo-home") / "mdo-portability-probe"
        marker.write_text("keep", encoding="ascii")
        unexpected = sorted(path.name for path in first.iterdir()
                            if path.name not in {name, "mdo-home"})
        if unexpected:
            raise GateError("single-file startup wrote side files: " +
                            ", ".join(unexpected))
        appdata = os.environ.get("APPDATA")
        if appdata and (Path(appdata) / name).exists():
            raise GateError("WebView2 profile fell back to AppData")

        # Move exactly the executable and its Home, then open the same profile.
        second = root / "portable-moved"
        second.mkdir()
        target.replace(second / name)
        source_home = first / "mdo-home"
        deadline = time.monotonic() + 5
        while True:
            try:
                source_home.replace(second / "mdo-home")
                break
            except PermissionError as error:
                if time.monotonic() >= deadline:
                    raise GateError("WebView2 kept portable Home open after close") from error
                time.sleep(0.2)
        launch(second / name)
        profile(second / "mdo-home")
        if (second / "mdo-home/data/cache/webview2" /
                marker.name).read_text(encoding="ascii") != "keep":
            raise GateError("moved WebView2 profile lost existing data")
        if sorted(path.name for path in second.iterdir()) != ["mdo-home", name]:
            raise GateError("moved single-file startup wrote side files")

        env_home = root / "环境 Home"
        launch(second / name, environment={"MDO_HOME": str(env_home)})
        profile(env_home)
        cli_home = root / "命令 Home"
        ignored_env = root / "ignored-environment"
        launch(second / name, arguments=("--", "--home", str(cli_home)),
               environment={"MDO_HOME": str(ignored_env)})
        profile(cli_home)
        if ignored_env.exists():
            raise GateError("--home did not override MDO_HOME")
        if sorted(path.name for path in second.iterdir()) != ["mdo-home", name]:
            raise GateError("Home overrides wrote beside the executable")
        if appdata and (Path(appdata) / name).exists():
            raise GateError("WebView2 profile fell back to AppData")


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
        frontend_checks()
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
        run([sys.executable, str(TESTS / "test_packed_home_lease.py"),
             "--packed", str(first)])
        run([sys.executable,
             str(TESTS / "test_packed_queue_start_recovery.py"),
             "--packed", str(first)])

        if os.name == "nt" and not args.skip_gui_smoke:
            windows_single_file(first, args.observe_seconds)
            windows_packed_crash_gate(packer, max(args.observe_seconds, 20))

        print(f"[qa] PASS: unit/contract suite, {probe_count} runtime probes")
        print(f"[qa] PASS: deterministic pack sha256={first_hash}")
        if os.name == "nt" and not args.skip_gui_smoke:
            print("[qa] PASS: portable WebView2 Home and packed crash gates")
        return 0
    except (GateError, build_mdo.BuildError, KeyError, OSError,
            subprocess.CalledProcessError, ValueError) as error:
        print(f"[qa] FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
