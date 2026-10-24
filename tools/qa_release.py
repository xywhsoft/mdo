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
import json
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


def release_inputs() -> dict[str, str]:
    files = {path.relative_to(ROOT).as_posix(): digest(path)
             for path in sorted((ROOT / "app").rglob("*"))
             if path.is_file() and "generated" not in path.relative_to(ROOT / "app").parts}
    return {"application": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(),
            "dependency lock": digest(ROOT / "deps.lock"),
            "host profile": digest(ROOT / "tools/host-profile.json")}


def verify_release_inputs(expected: dict[str, str]) -> None:
    actual = release_inputs()
    changed = [name for name in expected if actual.get(name) != expected[name]]
    if changed:
        raise GateError("release inputs changed during validation: " + ", ".join(changed) +
                        "; finish concurrent edits and rerun the gate")


def strict_compile(cc: str, xserver: Path) -> None:
    includes = (
        xserver / "src/sdk",
        xserver / "lib",
        xserver / "lib/xwork",
        xserver / "lib/xllm",
        xserver / "lib/xllm-session",
        xserver / "lib/xs-image",
        xserver / "lib/xoauth2",
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
        "-DXS_USE_IMAGE=1",
        str(ROOT / "app/generated/mdo_unity.c"),
    ))
    run(command)


def runtime_probe_command(probe: Path, host: Path, packed: Path, *,
                          optional_tools_stage: Path | None = None,
                          optional_tools_platform: str | None = None) -> list[str]:
    # The cleanup regression exercises the product pack, not an unpacked host.
    # Always pass this run's candidate instead of its default root mdo.exe.
    if probe.name == "test_debug_cleanup_runtime.py":
        return [sys.executable, str(probe), "--packed-path", str(packed)]
    if probe.name == "test_session_purge_runtime.py":
        return [sys.executable, str(probe), "--host", str(host), "--packed", str(packed)]
    if probe.name == "test_task_stop_runtime.py":
        directory = BUILD / "qa-release" / ("task-stop-" + uuid.uuid4().hex)
        return [sys.executable, str(probe), "--host", str(host), "--directory", str(directory)]
    if probe.name in {"test_conversation_groups_runtime.py", "test_responses_followup_runtime.py",
                      "test_session_profile_errors_runtime.py"}:
        return [sys.executable, str(probe), "--packed", str(packed)]
    if probe.name == "test_optional_tools_runtime.py":
        platform = optional_tools_platform or ("windows-x86_64" if os.name == "nt" else "linux-x86_64")
        return [sys.executable, str(probe), "--stage", str(optional_tools_stage or BUILD / "optional-tools"), "--platform", platform]
    return [sys.executable, str(probe), "--host", str(host)]


def runtime_probes(host: Path, packed: Path, *, optional_tools_stage: Path | None = None,
                   optional_tools_platform: str | None = None) -> int:
    device_probes = {
        "test_android_conversation_runtime.py": "requires an isolated Android QA package and device",
        "test_android_device_runtime.py": "requires an explicitly selected Android device",
    }
    probes = []
    for probe in sorted(TESTS.glob("test_*_runtime.py"), key=lambda path: path.name):
        if probe.name in device_probes:
            print(f"[qa] DEVICE CHECK NOT RUN: {probe.name}: {device_probes[probe.name]}", flush=True)
        elif probe.name == "test_optional_tools_runtime.py" and optional_tools_stage is None:
            print(f"[qa] ARTIFACT CHECK NOT RUN: {probe.name}: pass --optional-tools-stage for the staged tool payloads", flush=True)
        else:
            probes.append(probe)
    if not probes:
        raise GateError("no bounded runtime probes were found")
    for probe in probes:
        if probe.name == "test_optional_tools_runtime.py":
            run(runtime_probe_command(probe, host, packed, optional_tools_stage=optional_tools_stage,
                                      optional_tools_platform=optional_tools_platform))
        else:
            run(runtime_probe_command(probe, host, packed))
    return len(probes)


def packed_probe_commands(packed: Path, output: Path) -> list[list[str]]:
    names = (
        "test_packed_home_lease.py", "test_packed_default_workspace.py",
        "test_packed_module_catalog.py", "test_live_runtime.py",
        "test_packed_queue_start_recovery.py", "test_packed_backup_download.py",
        "test_packed_restore.py", "test_packed_compaction_failure.py",
        "test_packed_compaction_recovery.py", "test_packed_fork_recovery.py",
        "test_packed_message_edit_recovery.py", "test_packed_recovery_ack.py",
        "test_grep_regex_packed.py",
    )
    commands = []
    for name in names:
        command = [sys.executable, str(TESTS / name), "--packed", str(packed)]
        if name == "test_packed_fork_recovery.py":
            command += ["--report", str(output / "fork-recovery.json")]
        if name == "test_grep_regex_packed.py":
            command += ["--report", str(output / "grep-regex.json")]
        commands.append(command)
    return commands


def frontend_checks() -> None:
    node = shutil.which("node")
    if node is None:
        raise GateError(
            "Node.js is required for frontend release checks; "
            "build_mdo.py remains Node-free"
        )
    run([node, "--experimental-vm-modules",
         str(ROOT / "tools/check_web_modules.mjs")])
    tests = sorted(TESTS.glob("test_*.mjs"), key=lambda path: path.name)
    if not tests:
        raise GateError("no frontend interaction tests were found")
    run([node, "--test", *(str(path) for path in tests)])


def native_workbench_probe_commands(packed: Path, output: Path) -> list[list[str]]:
    return [[sys.executable, str(TESTS / "test_workbench_native_browser.py"),
             "--packed", str(packed), "--" + mode, "--evidence", str(output / ("native-" + mode))]
            for mode in ("start", "display", "application", "changes", "terminal")]


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
        # Validate both ends before moving the executable or its complete Home.
        # In particular, an unexpected Home junction must never move data from
        # outside this gate's disposable directory.
        source_home = first / "mdo-home"
        destination_home = second / "mdo-home"
        move_paths = (target, second / name, source_home, destination_home)
        if any(not path.resolve().is_relative_to(root.resolve()) for path in move_paths):
            raise GateError("portable move escaped the disposable gate directory")
        target.replace(second / name)
        deadline = time.monotonic() + 5
        while True:
            try:
                source_home.replace(destination_home)
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
        shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(TESTS / "packed-startup-smoke.ps1"),
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
    parser.add_argument("--optional-tools-stage", type=Path,
                        help="explicit independent tool payload stage containing input.json")
    parser.add_argument("--optional-tools-platform",
                        help="tool stage platform (defaults to the current Windows/Linux platform)")
    parser.add_argument("--observe-seconds", type=int, default=5,
                        choices=range(5, 61), metavar="5..60")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        lock = build_mdo.load_object(ROOT / "deps.lock")
        xserver = build_mdo.find_xserver(args.xserver_root, lock)
        build_mdo.verify_dependencies(xserver, lock)
        inputs = release_inputs()
        output_dir = BUILD / "qa-release"
        output_dir.mkdir(parents=True, exist_ok=True)
        suffix = ".exe" if os.name == "nt" else ""
        first = output_dir / f"mdo-release-a{suffix}"
        second = output_dir / f"mdo-release-b{suffix}"
        host_dir = BUILD / "host" if os.name == "nt" else BUILD / "host/linux-gui-glibc"
        host = host_dir / f"xs{suffix}"
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
        host_hash, packer_hash = digest(host), digest(packer)
        strict_compile(args.cc, xserver)
        if os.name == "nt":
            # Distribution probes require catalog/archives from this source,
            # not an earlier release's ignored build cache.
            run([sys.executable, str(ROOT / "tools/build_toolpacks.py"),
                 "--packer", str(host)])
        probe_count = runtime_probes(host, first, optional_tools_stage=args.optional_tools_stage,
                                    optional_tools_platform=args.optional_tools_platform)
        verify_release_inputs(inputs)
        # Reproduce the product staging too: build_mdo adds edition/platform
        # definitions and normalizes xs.json before packing. Packing raw app/
        # compares different inputs and cannot test product determinism.
        repeat_build = [item for item in build if item != "--skip-host-build"]
        repeat_build[repeat_build.index(str(first))] = str(second)
        run([*repeat_build, "--skip-host-build"])
        first_hash = digest(first)
        second_hash = digest(second)
        if first_hash != second_hash:
            raise GateError(
                f"pack is not deterministic: {first_hash} != {second_hash}"
            )
        packed_commands = packed_probe_commands(first, output_dir)
        for command in packed_commands:
            run(command)

        if os.name == "nt" and not args.skip_gui_smoke:
            for command in native_workbench_probe_commands(first, output_dir):
                run(command)
            windows_single_file(first, args.observe_seconds)
            windows_packed_crash_gate(packer, max(args.observe_seconds, 20))

        verify_release_inputs(inputs)
        if (digest(host), digest(packer)) != (host_hash, packer_hash):
            raise GateError("native hosts changed during validation; rerun the gate")

        print(f"[qa] PASS: unit/contract suite, {probe_count} runtime probes")
        print(f"[qa] PASS: {len(packed_commands)} packed-product probes")
        print(f"[qa] PASS: deterministic pack sha256={first_hash}")
        if os.name == "nt" and not args.skip_gui_smoke:
            print("[qa] PASS: native workbench, portable WebView2 Home and packed crash gates")
        return 0
    except (GateError, build_mdo.BuildError, KeyError, OSError,
            subprocess.CalledProcessError, ValueError) as error:
        print(f"[qa] FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
