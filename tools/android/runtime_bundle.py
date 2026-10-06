"""APK native/data mapping; Android native executables must be lib*.so entries."""
import hashlib
import json
from pathlib import Path
import zipfile

def add_runtime(archive, root: Path, edition: str, build_id: int, revision: int):
    files, names = [], set()
    if edition == "full":
        for file in sorted(root.rglob("*")):
            if not file.is_file() or file.suffix in (".md", ".json"):
                continue
            path = file.relative_to(root).as_posix()
            if "/licenses/" in "/" + path:
                archive.write(file, "assets/licenses/runtime/" + path)
                continue
            if file.read_bytes()[:4] == b"\x7fELF":
                # Preserve DT_NEEDED library names; extension modules have unique
                # APK-safe names and get their CPython names through symlinks.
                name = file.name if file.name.startswith("lib") and file.suffix == ".so" else "libmdo_" + path.replace("/", "_").replace(".", "_") + ".so"
                if name in names:
                    raise ValueError("duplicate native runtime name: " + name)
                names.add(name)
                archive.write(file, "lib/arm64-v8a/" + name, compress_type=zipfile.ZIP_STORED)
                files.append({"path": path, "native": name})
            else:
                archive.write(file, "assets/mdo-runtime/" + path, compress_type=zipfile.ZIP_DEFLATED)
                files.append({"path": path, "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
    if edition == "full":
        busybox = next(item["native"] for item in files if item["path"] == "busybox/busybox")
        for applet in ("sh", "ash", "awk", "sed", "grep", "find", "cat", "head", "tail", "sort", "wc", "cp", "mv", "rm", "mkdir", "rmdir", "touch", "tar", "gzip", "gunzip", "unzip", "xargs", "tee", "tr", "which", "basename", "dirname"):
            files.append({"path": "busybox/" + applet, "native": busybox})
    archive.writestr("assets/mdo-runtime.json", json.dumps({"edition": edition, "build_id": str(build_id), "revision": revision, "files": files}))
