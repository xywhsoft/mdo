"""APK native/data mapping; Android native executables must be lib*.so entries."""
import hashlib
import json
from pathlib import Path
import zipfile

def add_runtime(archive, root: Path, edition: str, build_id: int, revision: int):
    files, names, executables = [], {}, {}
    if edition == "full":
        for file in sorted(root.rglob("*")):
            if not file.is_file() or file.suffix in (".md", ".json"):
                continue
            path = file.relative_to(root).as_posix()
            if "/licenses/" in "/" + path:
                archive.write(file, "assets/licenses/runtime/" + path)
                continue
            with file.open("rb") as source:
                elf = source.read(4) == b"\x7fELF"
            if elf:
                # Preserve DT_NEEDED library names; extension modules have unique
                # APK-safe names and get their CPython names through symlinks.
                name = file.name if file.name.startswith("lib") and file.suffix == ".so" else "libmdo_" + path.replace("/", "_").replace(".", "_") + ".so"
                with file.open("rb") as source:
                    digest = hashlib.file_digest(source, "sha256").hexdigest()
                library = file.name.startswith("lib") and file.suffix == ".so"
                if not library and digest in executables:
                    name = executables[digest]
                if name in names and names[name] != digest:
                    raise ValueError("conflicting native runtime name: " + name)
                if name not in names:
                    names[name] = digest
                    archive.write(file, "lib/arm64-v8a/" + name, compress_type=zipfile.ZIP_STORED)
                if not library:
                    executables[digest] = name
                files.append({"path": path, "native": name})
            else:
                archive.write(file, "assets/mdo-runtime/" + path, compress_type=zipfile.ZIP_DEFLATED)
                files.append({"path": path, "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
    if edition == "full":
        # Build-time alias lists represent upstream hardlinks without storing
        # dozens of identical Git executables in either the APK or staging.
        native = {item["path"]: item["native"] for item in files if "native" in item}
        existing = {item["path"] for item in files}
        for manifest in sorted(root.glob("*/native-aliases.json")):
            aliases = json.loads(manifest.read_text(encoding="utf-8"))
            if not isinstance(aliases, dict) or len(aliases) > 1024:
                raise ValueError("invalid native alias manifest")
            for path, target in sorted(aliases.items()):
                if not isinstance(path, str) or not isinstance(target, str) or path in existing or target not in native:
                    raise ValueError("invalid native alias target")
                parts = path.split("/")
                if len(path) > 220 or len(parts) < 2 or parts[0] != manifest.parent.name or any(not p or p.startswith(".") or any(ord(c) < 32 or ord(c) == 127 or c in '\\:<>"|?*' for c in p) for p in parts):
                    raise ValueError("unsafe native alias path")
                files.append({"path": path, "native": native[target]})
                existing.add(path)
        busybox = next(item["native"] for item in files if item["path"] == "busybox/busybox")
        for applet in ("sh", "ash", "awk", "sed", "grep", "find", "cat", "head", "tail", "sort", "wc", "cp", "mv", "rm", "mkdir", "rmdir", "touch", "tar", "gzip", "gunzip", "unzip", "xargs", "tee", "tr", "which", "basename", "dirname"):
            files.append({"path": "busybox/" + applet, "native": busybox})
    archive.writestr("assets/mdo-runtime.json", json.dumps({"edition": edition, "build_id": str(build_id), "revision": revision, "files": files}))
