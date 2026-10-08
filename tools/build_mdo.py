#!/usr/bin/env python3
"""Verify locked dependencies, generate the unity source, and build mdo."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
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
ICON_PATH = ROOT / "assets" / "branding" / "mdo.ico"
HOST_PROFILE_PATH = ROOT / "tools" / "host-profile.json"
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
        # Windows-created Git worktrees also build inside WSL. Resolve only
        # their .git pointer; never change the worktree or global Git config.
        pointer = root / ".git"
        if os.name != "nt" and pointer.is_file():
            value = pointer.read_text(encoding="utf-8").strip().removeprefix("gitdir: ")
            if re.match(r"^[A-Za-z]:/", value):
                mapped = "/mnt/" + value[0].lower() + value[2:]
                try:
                    return subprocess.check_output(["git", "--git-dir", mapped, "rev-parse", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip().lower()
                except (OSError, subprocess.CalledProcessError):
                    pass
        return None


def restore_bundled_xserver(candidates: list[Path], lock: dict) -> Path | None:
    """Restore a locked compatibility commit without changing any user checkout.

    The small Git bundle carries only the conversation repair commits; the
    normal xs repository supplies their historical base. It also lets a fresh
    machine build before the compatible branch is published upstream.
    """
    bundle = lock["xserver"].get("source_bundle")
    if bundle is None:
        return None
    if not isinstance(bundle, dict):
        raise BuildError("xserver.source_bundle must be an object")
    relative, digest, ref = bundle.get("path"), bundle.get("sha256"), bundle.get("ref")
    if (not isinstance(relative, str) or not isinstance(digest, str)
            or not re.fullmatch(r"[0-9a-f]{64}", digest)
            or not isinstance(ref, str) or not ref.startswith("refs/heads/")):
        raise BuildError("xserver source bundle path/hash is invalid")
    path = (ROOT / relative).resolve()
    # Binary Git bundles must be hashed byte-for-byte, not normalized.
    if (not path.is_relative_to(ROOT.resolve()) or not path.is_file()
            or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
        raise BuildError("xserver source bundle is missing or has the wrong SHA-256")
    expected = locked_commit(lock["xserver"], "xserver")
    destination = ROOT / ".build" / "locked-xs" / expected
    if destination.exists():
        if git_head(destination) == expected:
            return destination
        raise BuildError(f"existing locked xserver checkout has a different revision: {destination}")
    for candidate in candidates:
        root = candidate.expanduser().resolve()
        if not root.is_dir() or git_head(root) is None:
            continue
        exists = subprocess.run(["git", "cat-file", "-e", expected + "^{commit}"],
            cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not exists:
            if subprocess.run(["git", "bundle", "verify", str(path)], cwd=root,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
                continue  # This clone does not have the bundle's base yet.
            subprocess.run(["git", "fetch", str(path), ref], cwd=root, check=True,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            exists = subprocess.run(["git", "cat-file", "-e", expected + "^{commit}"],
                cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if exists:
            destination.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "worktree", "add", "--detach", str(destination), expected],
                cwd=root, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return destination
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
        candidates.extend((ROOT.parent / "xserver", ROOT.parent / "xserver-mdo-refactor",
                           ROOT / ".build" / "implementation-xs", ROOT / ".build/linux-xs"))
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
    if explicit is None:
        recovered = restore_bundled_xserver(candidates, lock)
        if recovered is not None:
            return recovered
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


def validate_source_graph(sources: list[str]) -> None:
    """Reject orphan application C files and private headers before packing.

    TCC modules in default-home are discovered at runtime and have separate
    entry points. SDK headers outside app/ are validated by the dependency lock.
    """
    application = APP.resolve()
    pending = [(application / source).resolve() for source in sources]
    reached: set[Path] = set()
    quoted_include = re.compile(r'^\s*#\s*include\s*"([^"\r\n]+)"', re.MULTILINE)
    while pending:
        source = pending.pop()
        if source in reached:
            continue
        reached.add(source)
        for include in quoted_include.findall(source.read_text(encoding="utf-8")):
            dependency = (source.parent / include).resolve()
            if dependency.is_relative_to(application) and dependency.is_file():
                pending.append(dependency)
    owned = {path.resolve() for directory in (APP / "src", APP / "include")
             for path in directory.rglob("*") if path.suffix in (".c", ".h") and path.is_file()}
    orphans = sorted(path.relative_to(application).as_posix() for path in owned - reached)
    if orphans:
        raise BuildError("application files are not reachable from sources.json: " + ", ".join(orphans))


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
    validate_source_graph(result)
    return result


def generated_unity(lock: dict, sources: list[str]) -> str:
    release=load_object(APP / "release.json")
    ids=[release[key] for key in ("windows_build_id","android_lite_build_id","android_full_build_id")]
    if any(type(i) is not int or not 10000000 <= i <= 99999999 for i in ids) or len(set(ids))!=3:
        raise BuildError("release build IDs must be distinct eight-digit integers")
    lines = [
        f'#define MDO_VERSION_TEXT {json.dumps(release["version_name"])}',
        f'#define MDO_BUILD_ID {release["windows_build_id"]}u',
        f'#define MDO_TOOLPACK_REVISION {release["toolpack_revision"]}u',
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
    """Remove legacy bundled keys; online models now use the user's login.

    Keep the legacy argument accepted by shared desktop/Android build callers,
    but never read or embed its provider secret in a client artifact.
    """
    BUILTIN_KEY_PATH.unlink(missing_ok=True)


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


def host_profile_arguments(full_host: bool) -> list[str]:
    return [] if full_host else ["--profile", str(HOST_PROFILE_PATH)]


def host_profile_digest(full_host: bool) -> str:
    if full_host:
        return "full"
    value = load_object(HOST_PROFILE_PATH)
    value["xrt_modules"] = sorted(value["xrt_modules"])
    if "windows_sdk_headers" in value:
        value["windows_sdk_headers"] = sorted(value["windows_sdk_headers"])
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def verify_host_receipt(host: Path, lock: dict, full_host: bool, *, icon: bool = False) -> None:
    """A locked dependency alone cannot prove a reused binary matches it."""
    receipt_path = host.with_name(host.name + ".build.json")
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        revision = receipt.get("revision", "")
        valid = (receipt.get("schema_version") == 1
                 and receipt.get("profile_sha256") == host_profile_digest(full_host)
                 and re.fullmatch(r"[0-9a-f]{7,40}", revision) is not None
                 and lock["xserver"]["commit"].startswith(revision)
                 and set(lock["xserver"]["required_extensions"]).issubset(receipt.get("extensions", []))
                 and receipt.get("binary_sha256") == hashlib.sha256(host.read_bytes()).hexdigest())
        if icon:
            valid = valid and receipt.get("icon_sha256") == hashlib.sha256(ICON_PATH.read_bytes()).hexdigest()
        if not valid:
            raise ValueError("binary, revision, profile, extensions or icon mismatch")
    except (OSError, ValueError, TypeError, AttributeError) as error:
        raise BuildError(f"cannot reuse {host}: {error}; rebuild without --skip-host-build/--skip-native-build") from error
    if not full_host:
        # Check actual application API references before creating a release.
        # This also guards future mdo changes that need an additional xrt root.
        available = set(receipt.get("xrt_symbols", []))
        used = set()
        for path in APP.rglob("*"):
            if path.suffix not in (".c", ".h") or "generated" in path.relative_to(APP).parts:
                continue
            source = path.read_text(encoding="utf-8")
            # SDK headers declare the entire public API; declarations and text
            # are not references from the application to a runtime symbol.
            source = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', " ", source, flags=re.DOTALL)
            source = re.sub(r"\bXRT_API\b[^;{}]*;", " ", source)
            used.update(re.findall(r"\b(xrt[A-Za-z0-9_]+)\s*\(", source))
        missing = sorted(used - available)
        if missing:
            raise BuildError("compact host is missing application APIs: " + ", ".join(missing) + "; extend tools/host-profile.json")


def build(args: argparse.Namespace, xserver: Path, lock: dict) -> None:
    edition = getattr(args, "edition", "gui")
    libc = getattr(args, "libc", "glibc")
    if os.name == "nt" and libc == "musl":
        raise BuildError("musl targets Linux; use MinGW GCC on Windows")
    if libc == "musl" and getattr(args, "sysroot", None) is None:
        raise BuildError("musl builds require --sysroot; tools/build_linux.py prepares it automatically")
    frontend = "none" if edition == "server" else "native"
    suffix = ".exe" if os.name == "nt" else ""
    output = (args.output or ROOT / (("mdo-server" if edition == "server" else "mdo") + suffix)).resolve()
    if not args.dry_run:
        output.parent.mkdir(parents=True, exist_ok=True)
    legacy = os.name == "nt" and edition == "gui"
    host_dir = ROOT / ".build/host" if legacy else ROOT / ".build/host" / (("windows" if os.name == "nt" else "linux") + "-" + edition + "-" + libc)
    host = host_dir / ("xs" + suffix)
    extensions = lock["xserver"].get("required_extensions")
    if not isinstance(extensions, list) or not all(isinstance(item, str) for item in extensions):
        raise BuildError("xserver.required_extensions must be a string array")
    if not args.skip_host_build:
        os.environ["XS_BUILD_COMMIT"] = lock["xserver"]["commit"]
        run([
            sys.executable, "tools/build.py", *extensions,
            "--build-dir", str(ROOT / ".build" / "xserver" / (edition + "-" + libc)),
            "--output", str(host),
            "--cc", args.cc,
            "--frontend", frontend,
            "--gui-cc", getattr(args, "gui_cc", "gcc"),
            *(["--sysroot", str(args.sysroot), "--compile-extra", "-idirafter " + shlex.quote(str(args.sysroot / "include")), "--link-extra=-static"] if libc == "musl" else []),
            *host_profile_arguments(args.full_host),
            *(["--icon", str(ICON_PATH)] if os.name == "nt" else []),
        ], xserver, args.dry_run)
    elif not args.dry_run and not host.is_file():
        raise BuildError(f"--skip-host-build requested but {host} does not exist")

    packer = host.with_name("xsw.exe") if os.name == "nt" and edition == "gui" else host
    if not args.dry_run and not packer.is_file():
        raise BuildError(f"pack host was not built: {packer}")
    if not args.dry_run:
        verify_host_receipt(packer, lock, args.full_host, icon=os.name == "nt")
    if not args.dry_run:
        receipt = load_object(host.with_name(host.name + ".build.json"))
        if receipt.get("frontend") != frontend or (libc == "musl" and "-static" not in receipt.get("link_extra", "")):
            raise BuildError("host frontend/libc does not match this product; rebuild the host")
    package = ROOT / ".build/packages" / (("windows" if os.name == "nt" else "linux") + "-" + edition + "-" + libc)
    if not args.dry_run:
        # copytree overlays stale files, so pack only a fresh staging tree.
        if not package.resolve().is_relative_to((ROOT / ".build/packages").resolve()):
            raise BuildError("package staging directory escaped the build workspace")
        if package.exists():
            shutil.rmtree(package)
        shutil.copytree(APP, package)
        config = load_object(package / "xs.json")
        for service in config["services"]:
            if service.get("class") != "app":
                continue
            if edition == "server":
                service.pop("window", None)
                service["port"] = 5390
            elif os.name != "nt":
                service["window"]["profile_dir"]["subdir"] = "data/cache/webkit"
        write_if_changed(package / "xs.json", (json.dumps(config, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        unity = package / "generated/mdo_unity.c"
        unity.write_text(("#define MDO_SERVER_BUILD 1\n" if edition == "server" else "") + '#define MDO_PRODUCT_EDITION "' + ("server" if edition == "server" else "desktop") + '"\n' + unity.read_text(encoding="utf-8"), encoding="utf-8")
    run([str(packer), "pack", str(package), "-o", str(output)], ROOT, args.dry_run)
    if not args.dry_run:
        digest = hashlib.sha256()
        for path in sorted(package.rglob('*')):
            if path.is_file():
                digest.update(path.relative_to(package).as_posix().encode() + b'\0')
                digest.update(hashlib.sha256(path.read_bytes()).digest())
        product = {"schema_version": 1, "platform": "windows" if os.name == "nt" else "linux",
                   "edition": edition, "libc": "mingw" if os.name == "nt" else libc,
                   "xs_commit": lock['xserver']['commit'], "mdo_commit": git_head(ROOT),
                   "packaged_app_sha256": digest.hexdigest(),
                   "mdo_worktree_dirty": subprocess.run(["git", "diff", "--quiet"], cwd=ROOT, stderr=subprocess.DEVNULL).returncode != 0,
                   "sha256": hashlib.sha256(output.read_bytes()).hexdigest(), "bytes": output.stat().st_size,
                   "host": receipt}
        receipt_path = output.with_name('build.json') if output.parent != ROOT else ROOT / '.build/product-receipts' / (output.name + '.json')
        write_if_changed(receipt_path, (json.dumps(product, indent=2) + '\n').encode())
    print(f"[mdo] {'would build' if args.dry_run else 'built'} {output}", flush=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xserver-root", type=Path,
                        help="xserver checkout at the exact revision in deps.lock")
    parser.add_argument("--output", type=Path, help="mdo executable path")
    parser.add_argument("--edition", choices=("gui", "server"), default="gui")
    parser.add_argument("--libc", choices=("glibc", "musl"), default="glibc", help="Linux libc (Windows uses MinGW)")
    parser.add_argument("--sysroot", type=Path, help="musl target CRT to embed in the TCC VFS")
    parser.add_argument("--gui-cc", default="gcc", help="glibc compiler for Linux GTK helper")
    parser.add_argument("--cc", default="gcc", help="C/C++ compiler used by xserver")
    parser.add_argument("--builtin-connection", type=Path,
                        help="obsolete compatibility argument; online provider keys are never bundled")
    parser.add_argument("--prepare-only", action="store_true",
                        help="verify dependencies and generate the unity source only")
    parser.add_argument("--skip-host-build", action="store_true",
                        help="reuse .build/host after dependency verification")
    parser.add_argument("--full-host", action="store_true", help="use the complete xs/xrt/Windows SDK instead of the mdo compact profile")
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
