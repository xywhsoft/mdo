#!/usr/bin/env python3
"""Give bundled Android CLI libraries private SONAMEs, retaining 16 KiB pages.

Android framework libraries can depend on private system versions of liblzma,
libcrypto, etc. A tool's generic LD_LIBRARY_PATH must not replace those system
dependencies. All new tool libraries and their DT_NEEDED references are renamed
together before APK deduplication. Existing Python's private names are separate.
"""
import argparse
import json
from pathlib import Path
import subprocess

from optional_tools import digest, elf_needed


def namespace(stage, receipt):
    mapping, changes = {}, []
    for path in stage.glob("*/*/lib/*.so"):
        if not path.name.startswith("libmdo_ext_"):
            mapping[path.name] = "libmdo_ext_"+path.name[3:]
    for root in sorted(stage.glob("*/*")):
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            with path.open("rb") as source:
                if source.read(4) != b"\x7fELF":
                    continue
            arguments = []
            for name in elf_needed(path):
                if name in mapping:
                    arguments += ["--replace-needed", name, mapping[name]]
            if path.parent.name == "lib" and path.name in mapping:
                arguments += ["--set-soname", mapping[path.name]]
            if arguments:
                before = digest(path)
                subprocess.run(["patchelf", "--page-size", "16384", *arguments, str(path)], check=True)
                changes.append(dict(path=path.relative_to(stage).as_posix(), before=before, after=digest(path)))
        for path in (root/"lib").glob("*.so"):
            if path.name in mapping:
                destination = path.with_name(mapping[path.name])
                if destination.exists():
                    raise ValueError("Private library destination already exists: "+str(destination))
                path.rename(destination)
    if mapping:
        receipt.write_text(json.dumps(dict(mapping=mapping, files=changes), indent=2)+"\n", encoding="utf-8")
    print("Isolated", len(mapping), "Android library names;", len(changes), "ELF files patched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True, help="staged Android platform directory")
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    namespace(args.stage.resolve(), args.receipt.resolve())
