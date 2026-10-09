#!/usr/bin/env python3
"""Build the desktop app and both Android editions into the repository root."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import build_mdo

ROOT = build_mdo.ROOT


def android_toolchain(args: argparse.Namespace) -> list[str]:
    """Use explicit paths, a local verified toolchain, or the existing WSL cache."""
    sdk = args.sdk or os.environ.get("MDO_ANDROID_SDK")
    java = args.java_home or os.environ.get("MDO_ANDROID_JAVA_HOME")
    if sdk and java:
        return ["--sdk", sdk, "--java-home", java, *(["--wsl"] if args.wsl else [])]
    if not args.wsl:
        roots = [ROOT / ".build/android-toolchain-windows", ROOT / ".build/android-toolchain",
                 Path.home() / ".cache/mdo-android-toolchain"]
        for root in roots:
            candidate = Path(sdk) if sdk else root / "sdk"
            java_root = java or os.environ.get("JAVA_HOME")
            suffix = ".exe" if os.name == "nt" else ""
            if (candidate / "ndk").is_dir() and java_root and (Path(java_root) / "bin" / ("javac" + suffix)).is_file():
                return ["--sdk", str(candidate), "--java-home", java_root]
    if os.name == "nt":
        # Query existing Linux paths without downloading tools or changing WSL.
        probe = '''import json,os
from pathlib import Path
roots=[Path.home()/".cache/mdo-android-toolchain",*sorted(Path("/home").glob("*/.cache/mdo-android-toolchain"))]
sdks=[Path(os.environ["MDO_ANDROID_SDK"])] if os.environ.get("MDO_ANDROID_SDK") else [p/"sdk" for p in roots]
sdk=next((p for p in sdks if (p/"ndk").is_dir()),None)
candidates=[Path(p) for p in (os.environ.get("MDO_ANDROID_JAVA_HOME"),os.environ.get("JAVA_HOME")) if p]
for root in roots:
    candidates += sorted((root/"java/usr/lib/jvm").glob("*"))
candidates += sorted(Path("/usr/lib/jvm").glob("*"))
java=next((p for p in candidates if (p/"bin/javac").is_file()),None)
print(json.dumps({"sdk":str(sdk) if sdk else None,"java":str(java) if java else None}))
'''
        try:
            value = json.loads(subprocess.check_output(["wsl", "-e", "python3", "-c", probe],
                                                      text=True, stderr=subprocess.PIPE, timeout=30))
            sdk, java = sdk or value.get("sdk"), java or value.get("java")
            if sdk and java:
                return ["--sdk", sdk, "--java-home", java, "--wsl"]
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
    raise build_mdo.BuildError("Android SDK/JDK unavailable; pass --sdk and --java-home (plus --wsl for Linux tools on Windows)")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path)
    parser.add_argument("--sdk", help="Android SDK; otherwise MDO_ANDROID_SDK or existing toolchain cache")
    parser.add_argument("--java-home", help="JDK 17+; otherwise MDO_ANDROID_JAVA_HOME or existing toolchain cache")
    parser.add_argument("--wsl", action="store_true")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--output", type=Path, help="desktop executable path; APKs are always written to the repository root")
    parser.add_argument("--skip-host-build", action="store_true")
    parser.add_argument("--skip-native-build", action="store_true")
    parser.add_argument("--full-host", action="store_true")
    parser.add_argument("--builtin-connection", type=Path)
    parser.add_argument("--keystore", type=Path)
    parser.add_argument("--key-alias")
    parser.add_argument("--store-password-env")
    parser.add_argument("--key-password-env")
    parser.add_argument("--dry-run", action="store_true", help="print both build commands without executing them")
    args = parser.parse_args(argv)
    try:
        toolchain = android_toolchain(args)
        lock = build_mdo.load_object(build_mdo.LOCK_PATH)
        xserver = build_mdo.find_xserver(args.xserver_root, lock)
        common = ["--xserver-root", str(xserver), "--cc", args.cc]
        if args.full_host:
            common.append("--full-host")
        if args.builtin_connection:
            common += ["--builtin-connection", str(args.builtin_connection.resolve())]
        suffix = ".exe" if os.name == "nt" else ""
        desktop_output = (args.output or ROOT / ("mdo" + suffix)).resolve()
        build_mdo.run([sys.executable, str(ROOT / "tools/build_mdo.py"), *common,
                       "--output", str(desktop_output),
                       *(["--skip-host-build"] if args.skip_host_build else [])], ROOT, args.dry_run)
        android = [sys.executable, str(ROOT / "tools/build_android.py"), *common, *toolchain,
                   "--skip-host-build", "--edition", "both"]
        if args.skip_native_build:
            android.append("--skip-native-build")
        for name in ("keystore", "key_alias", "store_password_env", "key_password_env"):
            value = getattr(args, name)
            if value is not None:
                android += ["--" + name.replace("_", "-"), str(value)]
        build_mdo.run(android, ROOT, args.dry_run)
    except (build_mdo.BuildError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print("[mdo] build failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
