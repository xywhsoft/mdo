#!/usr/bin/env python3
"""Verify locked dependencies, generate the unity source, and build mdo."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
LOCK_PATH = ROOT / "deps.lock"
SOURCES_PATH = APP / "sources.json"
GENERATED = APP / "generated"
UNITY_PATH = GENERATED / "mdo_unity.c"
GENERATED_LOCK_PATH = GENERATED / "deps.lock"
MODULE_HEADER_PATH = ROOT / "include" / "mdo" / "module.h"
GENERATED_MODULE_HEADER_PATH = GENERATED / "module-sdk" / "mdo" / "module.h"
BUILTIN_CONNECTION_PATH = ROOT / ".build" / "ornith-connection.json"
BUILTIN_KEY_PATH = APP / "default-home" / "config" / "secrets" / "builtin-model.key"
HEX40 = re.compile(r"[0-9a-f]{40}\Z")


class BuildError(RuntimeError):
    pass


def load_object(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BuildError(f"cannot read {path.relative_to(ROOT)}: {error}") from error
    if not isinstance(value, dict):
        raise BuildError(f"{path.relative_to(ROOT)} must contain a JSON object")
    return value


def locked_commit(record: dict, label: str) -> str:
    value = record.get("commit")
    if not isinstance(value, str) or HEX40.fullmatch(value) is None:
        raise BuildError(f"deps.lock {label}.commit must be 40 lowercase hexadecimal characters")
    return value


def git_head(root: Path) -> str | None:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True,
            stderr=subprocess.DEVNULL,
        ).strip().lower()
    except (OSError, subprocess.CalledProcessError):
        return None


def find_xserver(explicit: Path | None, lock: dict) -> Path:
    expected = locked_commit(lock["xserver"], "xserver")
    if explicit is not None:
        candidates = [explicit]
    else:
        candidates = []
        configured = os.environ.get("MDO_XSERVER_ROOT", "").strip()
        if configured:
            candidates.append(Path(configured))
        candidates.extend((ROOT.parent / "xserver", ROOT.parent / "xserver-mdo-refactor"))
    checked: list[str] = []
    seen: set[Path] = set()
    for candidate in candidates:
        root = candidate.expanduser().resolve()
        if root in seen:
            continue
        seen.add(root)
        head = git_head(root) if root.is_dir() else None
        checked.append(f"{root} @ {head or 'unavailable'}")
        if head == expected:
            return root
        if explicit is not None:
            break
    detail = "; ".join(checked) if checked else "no candidates"
    raise BuildError(f"locked xserver {expected} was not found ({detail})")


def normalized_source_bytes(path: Path) -> bytes:
    """Return source bytes in the line-ending form used by deps.lock."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def source_file_sha256(path: Path) -> str:
    return hashlib.sha256(normalized_source_bytes(path)).hexdigest()


def production_files(root: Path) -> list[Path]:
    files = [
        path for path in root.iterdir()
        if path.is_file() and path.suffix in (".c", ".h")
        and not path.name.endswith("-xrt.c")
    ]
    source = root / "src"
    if source.is_dir():
        files.extend(path for path in source.rglob("*") if path.is_file())
    return sorted(files, key=lambda path: path.relative_to(root).as_posix())


def production_tree_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for path in production_files(root):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(normalized_source_bytes(path))
        digest.update(b"\0")
    return digest.hexdigest()


def image_extension_sha256(root: Path) -> str:
    """Pin wrapper/config and every decoder/license byte, using LF normalization."""
    files = production_files(root) + [root / "UPSTREAM.json"]
    files.extend(path for path in (root / "vendor").rglob("*") if path.is_file())
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda path: path.relative_to(root).as_posix()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0"); digest.update(normalized_source_bytes(path)); digest.update(b"\0")
    return digest.hexdigest()


def macro(header: str, name: str) -> str:
    match = re.search(rf"^\s*#define\s+{re.escape(name)}\s+([^\s/]+)", header, re.MULTILINE)
    if match is None:
        raise BuildError(f"required macro {name} is missing")
    return match.group(1).strip('"')


def verify_version(header_path: Path, names: tuple[str, str, str], expected: str) -> None:
    header = header_path.read_text(encoding="utf-8")
    actual = ".".join(macro(header, name).rstrip("uU") for name in names)
    if expected.endswith("-dev"):
        actual += "-dev"
    if actual != expected:
        raise BuildError(f"{header_path.name} version {actual} does not match {expected}")


def verify_dependencies(xserver: Path, lock: dict) -> None:
    xrt = lock.get("xrt")
    libraries = lock.get("libraries")
    if not isinstance(xrt, dict) or not isinstance(libraries, dict):
        raise BuildError("deps.lock must define xrt and libraries objects")
    image_record = lock["xserver"].get("native_extensions", {}).get("image", {})
    image_root = xserver / "lib/xs-image"
    if image_record.get("tree_sha256") != image_extension_sha256(image_root):
        raise BuildError("xs image extension/decoder source hash differs from deps.lock")
    if int(macro((image_root / "xs-image.h").read_text(encoding="utf-8"), "XS_IMAGE_ABI_VERSION").rstrip("uU")) != image_record.get("abi_version"):
        raise BuildError("xs image ABI differs from deps.lock")
    xrt_commit = locked_commit(xrt, "xrt")
    header = xserver / "lib" / "xrt.h"
    if source_file_sha256(header) != xrt.get("single_header_sha256"):
        raise BuildError("xserver lib/xrt.h does not match the locked xrt single header")
    if macro(header.read_text(encoding="utf-8"), "XRT_VERSION_TEXT") != xrt.get("version"):
        raise BuildError("xserver lib/xrt.h version does not match deps.lock")

    expected_versions = {
        "xllm": ("xllm.h", ("XLLM_VERSION_MAJOR", "XLLM_VERSION_MINOR", "XLLM_VERSION_PATCH")),
        "xllm-session": ("xllm-session.h", ("XLLM_SESSION_VERSION_MAJOR", "XLLM_SESSION_VERSION_MINOR", "XLLM_SESSION_VERSION_PATCH")),
        "xwork": ("xwork.h", ("XWORK_VERSION_MAJOR", "XWORK_VERSION_MINOR", "XWORK_VERSION_PATCH")),
    }
    for name, (header_name, version_macros) in expected_versions.items():
        record = libraries.get(name)
        if not isinstance(record, dict):
            raise BuildError(f"deps.lock libraries.{name} is missing")
        source_commit = record.get("source_commit")
        if not isinstance(source_commit, str) or HEX40.fullmatch(source_commit) is None:
            raise BuildError(
                f"deps.lock libraries.{name}.source_commit must be "
                "40 lowercase hexadecimal characters"
            )
        root = xserver / "lib" / name
        upstream = (root / "UPSTREAM.txt").read_text(encoding="utf-8")
        if f"基线: xrt@{source_commit}" not in upstream:
            raise BuildError(
                f"{name}/UPSTREAM.txt does not pin {source_commit}"
            )
        actual_hash = production_tree_sha256(root)
        if actual_hash != record.get("production_tree_sha256"):
            raise BuildError(f"{name} production tree hash mismatch: {actual_hash}")
        verify_version(root / header_name, version_macros, record.get("version", ""))

    session_header = (xserver / "lib" / "xllm-session" / "xllm-session.h").read_text(encoding="utf-8")
    if int(macro(session_header, "XLLM_SESSION_PERSISTENCE_SCHEMA_VERSION").rstrip("uU")) != libraries["xllm-session"].get("persistence_schema_version"):
        raise BuildError("xllm-session persistence schema version does not match deps.lock")

    xwork_header = (xserver / "lib" / "xwork" / "xwork.h").read_text(encoding="utf-8")
    if int(macro(xwork_header, "XWORK_ABI_VERSION").rstrip("uU")) != libraries["xwork"].get("abi_version"):
        raise BuildError("xwork ABI version does not match deps.lock")
    if int(macro(xwork_header, "XWORK_EVENT_SCHEMA_VERSION").rstrip("uU")) != libraries["xwork"].get("event_schema_version"):
        raise BuildError("xwork event schema version does not match deps.lock")

    xs_version = (xserver / "src" / "core" / "xs_version.h").read_text(encoding="utf-8")
    if macro(xs_version, "XS_VERSION_STRING") != lock["xserver"].get("version"):
        raise BuildError("xserver version does not match deps.lock")
    pack = (xserver / "src" / "core" / "xs_pack.h").read_text(encoding="utf-8")
    pack_lock = lock.get("pack", {})
    if 'memcpy(arrHeader, "XRTPACK\\0", 8u);' not in pack:
        raise BuildError("xserver does not emit the locked XRTPACK format")
    version = pack_lock.get("version")
    if not isinstance(version, int) or f"XS_PackWriteU16(arrHeader + 8u, {version}u);" not in pack:
        raise BuildError("xserver pack version does not match deps.lock")


def source_list() -> list[str]:
    manifest = load_object(SOURCES_PATH)
    if manifest.get("schema_version") != 1:
        raise BuildError("app/sources.json has an unsupported schema_version")
    values = manifest.get("sources")
    if not isinstance(values, list) or not values:
        raise BuildError("app/sources.json sources must be a nonempty array")
    result: list[str] = []
    seen: set[str] = set()
    for value in values:
        if not isinstance(value, str):
            raise BuildError("every unity source must be a string")
        normalized = value.replace("\\", "/")
        parts = normalized.split("/")
        if normalized.startswith("/") or parts[0] != "src" or any(part in ("", ".", "..") for part in parts):
            raise BuildError(f"invalid application source path: {value}")
        if not normalized.endswith(".c"):
            raise BuildError(f"unity source must be a .c file: {value}")
        key = normalized.casefold()
        if key in seen:
            raise BuildError(f"duplicate application source: {value}")
        path = (APP / normalized).resolve()
        if not path.is_relative_to(APP.resolve()) or not path.is_file():
            raise BuildError(f"missing application source: {value}")
        seen.add(key)
        result.append(normalized)
    return result


def generated_unity(lock: dict, sources: list[str]) -> str:
    lines = [
        "/* Generated by tools/build_mdo.py from app/sources.json. Do not edit. */",
        f'#define MDO_BUILD_XRT_COMMIT "{lock["xrt"]["commit"]}"',
        f'#define MDO_BUILD_XSERVER_COMMIT "{lock["xserver"]["commit"]}"',
        "",
    ]
    lines.extend(f'#include "../{source}"' for source in sources)
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path: Path, content: bytes) -> None:
    if path.is_file() and path.read_bytes() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(content)
    os.replace(temporary, path)


def prepare_builtin_credential(connection: Path | None = None) -> None:
    """Provision the optional bundled credential without tracking or logging it.

    A checkout can always build without credentials; its runtime then uses
    MDO_ORNITH_API_KEY or a portable Home key file. Never reuse a stale key from
    a previous provisioned build when its input has been removed.
    """
    source = connection if connection is not None else BUILTIN_CONNECTION_PATH
    if not source.is_file():
        if connection is not None:
            raise BuildError("the explicit built-in credential input is unavailable")
        BUILTIN_KEY_PATH.unlink(missing_ok=True)
        return
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
        key = value.get("api_key") if isinstance(value, dict) else None
    except (OSError, UnicodeError, json.JSONDecodeError):
        raise BuildError("cannot read built-in credential input") from None
    if not isinstance(key, str) or not key or len(key) > 4096 or any(
        ord(char) < 0x21 or ord(char) > 0x7e for char in key
    ):
        raise BuildError("built-in credential must be nonempty printable ASCII below 4097 bytes")
    write_if_changed(BUILTIN_KEY_PATH, key.encode("ascii"))


def prepare(lock: dict, connection: Path | None = None) -> None:
    prepare_builtin_credential(connection)
    write_if_changed(UNITY_PATH, generated_unity(lock, source_list()).encode("utf-8"))
    write_if_changed(GENERATED_LOCK_PATH, LOCK_PATH.read_bytes())
    write_if_changed(GENERATED_MODULE_HEADER_PATH, MODULE_HEADER_PATH.read_bytes())


def command_text(command: list[str]) -> str:
    return subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)


def run(command: list[str], cwd: Path, dry_run: bool) -> None:
    print(command_text(command), flush=True)
    if not dry_run:
        subprocess.run(command, cwd=cwd, check=True)


def build(args: argparse.Namespace, xserver: Path, lock: dict) -> None:
    suffix = ".exe" if os.name == "nt" else ""
    output = (args.output or ROOT / ("mdo" + suffix)).resolve()
    if not args.dry_run:
        output.parent.mkdir(parents=True, exist_ok=True)
    host = ROOT / ".build" / "host" / ("xs" + suffix)
    extensions = lock["xserver"].get("required_extensions")
    if not isinstance(extensions, list) or not all(isinstance(item, str) for item in extensions):
        raise BuildError("xserver.required_extensions must be a string array")
    if not args.skip_host_build:
        run([
            sys.executable, "tools/build.py", *extensions,
            "--build-dir", str(ROOT / ".build" / "xserver"),
            "--output", str(host),
            "--cc", args.cc,
        ], xserver, args.dry_run)
    elif not args.dry_run and not host.is_file():
        raise BuildError(f"--skip-host-build requested but {host} does not exist")

    packer = host.with_name("xsw.exe") if os.name == "nt" else host
    if not args.dry_run and not packer.is_file():
        raise BuildError(f"pack host was not built: {packer}")
    run([str(packer), "pack", str(APP), "-o", str(output)], ROOT, args.dry_run)
    print(f"[mdo] {'would build' if args.dry_run else 'built'} {output}", flush=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path,
                        help="xserver checkout at the exact revision in deps.lock")
    parser.add_argument("--output", type=Path, help="mdo executable path")
    parser.add_argument("--cc", default="gcc", help="C/C++ compiler used by xserver")
    parser.add_argument("--builtin-connection", type=Path,
                        help="local JSON with api_key for the bundled Ornith service (never logged)")
    parser.add_argument("--prepare-only", action="store_true",
                        help="verify dependencies and generate the unity source only")
    parser.add_argument("--skip-host-build", action="store_true",
                        help="reuse .build/host after dependency verification")
    parser.add_argument("--dry-run", action="store_true",
                        help="print host and pack commands without executing them")
    args = parser.parse_args(argv)
    try:
        lock = load_object(LOCK_PATH)
        if lock.get("schema_version") != 1:
            raise BuildError("deps.lock has an unsupported schema_version")
        xserver = find_xserver(args.xserver_root, lock)
        verify_dependencies(xserver, lock)
        prepare(lock, args.builtin_connection)
        print(f"[mdo] dependencies verified at {lock['xserver']['commit'][:12]}", flush=True)
        print(f"[mdo] generated {UNITY_PATH.relative_to(ROOT)}", flush=True)
        if not args.prepare_only:
            build(args, xserver, lock)
    except (BuildError, KeyError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"[mdo] failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
