#!/usr/bin/env python3
"""Build mdo's local ARM64 APK from the locked xs SDK and existing frontend."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

import build_mdo

ROOT = build_mdo.ROOT


def linux_path(path: Path) -> str:
    value = path.resolve().as_posix()
    if len(value) < 3 or value[1:3] != ":/":
        raise build_mdo.BuildError("WSL requires a local drive path: " + value)
    return "/mnt/" + value[0].lower() + value[2:]


def extract_pack(packed: Path, output: Path) -> None:
    """Keep xs's validated archive byte-for-byte, independent of host ISA."""
    data = packed.read_bytes()
    trailer = data[-32:]
    if len(trailer) != 32 or trailer[:8] != b"XRTPEND\0":
        raise build_mdo.BuildError("missing XRTPACK trailer")
    size = struct.unpack_from("<Q", trailer, 8)[0]
    if size < 112 or size > len(data):
        raise build_mdo.BuildError("invalid XRTPACK range")
    archive = data[-size:]
    if archive[:8] != b"XRTPACK\0" or zlib.crc32(trailer[:24] + archive[:80]) != struct.unpack_from("<I", trailer, 24)[0]:
        raise build_mdo.BuildError("XRTPACK metadata validation failed")
    output.write_bytes(archive)


def edition_outputs(args: argparse.Namespace, release: dict) -> list[tuple[str, Path, int]]:
    editions = ("lite", "full") if args.edition == "both" else (args.edition,)
    if len(editions) > 1 and (args.output is not None or args.version_code is not None):
        raise build_mdo.BuildError("--output and --version-code require --edition lite or --edition full")
    result = []
    for edition in editions:
        code = args.version_code if args.version_code is not None else release[f"android_{edition}_build_id"]
        if type(code) is not int or not 10000000 <= code <= 99999999:
            raise build_mdo.BuildError("version-code must be an eight-digit build ID")
        name = "mdo-arm64-v8a.apk" if edition == "lite" else "mdo-full-arm64-v8a.apk"
        result.append((edition, (args.output or ROOT / name).resolve(), code))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path)
    parser.add_argument("--sdk", required=True, help="verified Android SDK root")
    parser.add_argument("--java-home", required=True, help="JDK 17+ root")
    parser.add_argument("--wsl", action="store_true", help="use Linux Android tools from a Windows workspace")
    parser.add_argument("--cc", default="gcc", help="native Android build's host C compiler")
    parser.add_argument("--skip-host-build", action="store_true", help="reuse the existing packer after dependency verification")
    parser.add_argument("--skip-native-build", action="store_true", help="reuse this build directory's libxs.so")
    parser.add_argument("--full-host", action="store_true", help="build the complete xs/xrt SDK instead of the mdo compact profile")
    parser.add_argument("--builtin-connection", type=Path)
    parser.add_argument("--output", type=Path, help="single-edition APK output path")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".build/android")
    parser.add_argument("--keystore", type=Path, default=ROOT / ".build/android-signing/development.p12")
    parser.add_argument("--key-alias", default="xs-development")
    parser.add_argument("--store-password-env", help="runtime environment variable containing the release keystore password")
    parser.add_argument("--key-password-env", help="runtime environment variable containing the release key password")
    parser.add_argument("--version-code", type=int, default=None)
    parser.add_argument("--debuggable", action="store_true")
    parser.add_argument("--edition", choices=("both", "lite", "full"), default="both",
                        help="build both editions by default; select one for a custom output or build ID")
    args = parser.parse_args(argv)
    try:
        release = build_mdo.load_object(ROOT / "app/release.json")
        outputs = edition_outputs(args, release)
        if args.wsl and os.name != "nt":
            raise build_mdo.BuildError("--wsl is a Windows driver option")
        lock = build_mdo.load_object(build_mdo.LOCK_PATH)
        sdk_source = build_mdo.find_xserver(args.xserver_root, lock)
        build_mdo.verify_dependencies(sdk_source, lock)
        build_mdo.prepare(lock, args.builtin_connection)
        directory = args.build_dir.resolve(); directory.mkdir(parents=True, exist_ok=True)
        host = ROOT / ".build/host" / ("xsw.exe" if os.name == "nt" else "xs")
        if not args.skip_host_build:
            subprocess.run([sys.executable, str(sdk_source / "tools/build.py"), *lock["xserver"]["required_extensions"],
                "--output", str(host.with_name("xs.exe" if os.name == "nt" else "xs")),
                "--build-dir", str(ROOT / ".build/host-objects"),
                *build_mdo.host_profile_arguments(args.full_host),
                *(["--icon", str(build_mdo.ICON_PATH)] if os.name == "nt" else [])], cwd=sdk_source, check=True)
        if not host.is_file():
            raise build_mdo.BuildError("missing pack host: " + str(host))
        build_mdo.verify_host_receipt(host, lock, args.full_host, icon=os.name == "nt")
        packed = directory / "app-packed"
        subprocess.run([str(host), "pack", str(build_mdo.APP), "-o", str(packed)], cwd=ROOT, check=True)
        pack = directory / "app.xrtpack"; extract_pack(packed, pack)
        native = directory / "native"
        revision = lock["xserver"]["commit"]

        def target_run(script: Path, arguments: list[str], edition: str = "full") -> None:
            environment = dict(os.environ)
            secret_names = [name for name in (args.store_password_env, args.key_password_env) if name]
            if any(not environment.get(name) for name in secret_names):
                raise build_mdo.BuildError("release signing password environment variable is missing")
            if args.wsl:
                environment["WSLENV"] = ":".join(filter(None,[environment.get("WSLENV", ""), *secret_names]))
                command = ["wsl", "-e", "env", "XS_BUILD_COMMIT=" + revision, "MDO_ANDROID_XSERVER_ROOT=" + linux_path(sdk_source), "MDO_ANDROID_EDITION=" + edition, "python3", linux_path(script), *arguments]
                subprocess.run(command, cwd=ROOT, check=True, env=environment)
            else:
                subprocess.run([sys.executable, str(script), *arguments], cwd=ROOT, check=True,
                               env={**os.environ, "XS_BUILD_COMMIT": revision, "MDO_ANDROID_XSERVER_ROOT": str(sdk_source), "MDO_ANDROID_EDITION": edition})

        path = linux_path if args.wsl else lambda p: str(p.resolve())
        if not args.skip_native_build:
            target_run(sdk_source / "tools/android/build_native.py", [*lock["xserver"]["required_extensions"],
                "--sdk", args.sdk, "--out", path(native), "--cc", args.cc,
                *([] if args.full_host else ["--profile", path(build_mdo.HOST_PROFILE_PATH)])])
        build_mdo.verify_host_receipt(native / "libxs.so", lock, args.full_host)
        for edition, output, code in outputs:
            target_run(ROOT / "tools/android/build_apk.py", ["--sdk", args.sdk, "--java-home", args.java_home,
                "--library", path(native / "libxs.so"), "--pack", path(pack), "--output", path(output),
                "--app-link", "https://ai.xywhsoft.com/app/mdo/callback",
                "--package", "org.xleaves.mdo", "--label", "@string/app_name", "--home-name", "mdo-home",
                "--resources", path(ROOT / "assets/branding/android/res"),
                "--package-install",
                "--version", release["version_name"], "--version-code", str(code), "--keystore", path(args.keystore),
                "--key-alias", args.key_alias,
                *(["--store-password-env",args.store_password_env] if args.store_password_env else []),
                *(["--key-password-env",args.key_password_env] if args.key_password_env else []),
                *(["--debuggable"] if args.debuggable else [])], edition)
            print(f"[mdo] {edition} APK {output} SHA256 " + hashlib.sha256(output.read_bytes()).hexdigest())
        return 0
    except (OSError, ValueError, build_mdo.BuildError, subprocess.CalledProcessError) as error:
        print("[mdo] Android build failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
