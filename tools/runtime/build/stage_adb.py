"""Stage and package pinned official adb clients, without starting a device server."""
import argparse
import json
from pathlib import Path
import struct
import sys
from urllib.request import urlopen
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from extension_archives import package_tool, digest
from optional_tools import elf_needed, pe_imports, SYSTEM_DLLS

SYSTEM_ELF = {"libdl.so.2", "libpthread.so.0", "libm.so.6", "librt.so.1",
              "libgcc_s.so.1", "libc.so.6", "ld-linux-x86-64.so.2"}


def prepare(record, downloads):
    archive = downloads / record["file"]
    if not archive.exists():
        downloads.mkdir(parents=True, exist_ok=True)
        pending = archive.with_suffix(".pending")
        try:
            with urlopen(record["url"], timeout=45) as source, pending.open("xb") as target:
                while block := source.read(1024 * 1024):
                    target.write(block)
            if pending.stat().st_size != record["size"] or digest(pending) != record["sha256"]:
                raise ValueError("Download does not match pinned adb archive")
            pending.replace(archive)
        finally:
            pending.unlink(missing_ok=True)
    if archive.stat().st_size != record["size"] or digest(archive) != record["sha256"]:
        raise ValueError("Cached adb archive changed: " + str(archive))
    stage = ROOT / "tools/runtime" / record["platform"].removesuffix("-glibc") / "adb"
    stage.mkdir(parents=True, exist_ok=True)
    windows = record["platform"] == "windows-x86_64"
    payload = ["adb.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll", "libwinpthread-1.dll"] if windows else ["adb"]
    payload += ["NOTICE.txt", "source.properties"]
    expected = set(payload) | {"UPSTREAM.json"}
    if any(p.name not in expected or not p.is_file() or p.is_symlink() for p in stage.iterdir()):
        raise ValueError("Unexpected files in owned adb staging directory")
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() is not None:
            raise ValueError("adb archive CRC failed")
        for name in payload:
            data = bundle.read("platform-tools/" + name)
            (stage / name).write_bytes(data)
            if name in ("adb", "adb.exe"):
                (stage / name).chmod(0o755)
    if "Pkg.Revision=" + record["version"] not in (stage / "source.properties").read_text():
        raise ValueError("adb upstream version differs from source lock")
    (stage / "UPSTREAM.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    if windows:
        available = {p.name.lower() for p in stage.iterdir()}
        for name in payload:
            path = stage / name
            if path.suffix.lower() not in (".exe", ".dll"):
                continue
            data = path.read_bytes()
            offset = struct.unpack_from("<I", data, 60)[0]
            # Google's Windows distribution is Win32, including all DLLs. Its
            # supported host here is Windows x64; retain the official bytes.
            if data[offset:offset+4] != b"PE\0\0" or struct.unpack_from("<H", data, offset+4)[0] != 0x14c:
                raise ValueError("Expected the pinned official Win32 binary: " + name)
            for imported in pe_imports(path):
                key = imported.lower()
                if key in available or key.startswith(("api-ms-win-", "ext-ms-win-")) or key.rsplit(".", 1)[0] in SYSTEM_DLLS | {"winusb"}:
                    continue
                raise ValueError("Unbundled Windows dependency: " + imported)
    else:
        data = (stage / "adb").read_bytes()
        if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 62:
            raise ValueError("Expected an x86_64 Linux ELF")
        if set(elf_needed(stage / "adb")) - SYSTEM_ELF:
            raise ValueError("Unbundled Linux adb dependency")
    return stage, "adb.exe" if windows else "adb"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--downloads", type=Path, default=ROOT / ".build/adb-downloads")
    parser.add_argument("--output", type=Path, default=ROOT / "tools/runtime/packages/adb")
    args = parser.parse_args()
    records = json.loads(Path(__file__).with_name("adb.lock.json").read_text(encoding="utf-8"))
    description = next(item for item in json.loads((ROOT / "tools/runtime/official.json").read_text(encoding="utf-8"))["tools"] if item["id"] == "adb")
    rows = []
    for record in records:
        stage, entry = prepare(record, args.downloads)
        metadata = {key: description[key] for key in ("id", "name", "prompt", "license", "links", "description", "recommended")}
        metadata.update(platform=record["platform"], version=record["version"], entry=entry,
                        author="Android Open Source Project", commands=[dict(name="adb", entry=entry, args=[])])
        row = package_tool(stage, metadata, args.output)
        rows.append(row)
        print(row["file"], row["size"], row["sha256"], flush=True)
    (args.output / "catalog.json").write_text(json.dumps({"tools": rows}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
