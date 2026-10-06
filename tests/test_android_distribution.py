"""Inspect built APKs without a device: metadata, native mapping and ELF closure."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SYSTEM_LIBRARIES = {"libc.so", "libdl.so", "libm.so", "liblog.so", "libz.so", "libandroid.so"}


def elf_dependencies(data, name):
    assert data[:6] == b"\x7fELF\x02\x01", name
    assert struct.unpack_from("<H", data, 18)[0] == 183, name
    offset = struct.unpack_from("<Q", data, 32)[0]
    width, count = struct.unpack_from("<HH", data, 54)
    headers = [struct.unpack_from("<IIQQQQQQ", data, offset + i * width) for i in range(count)]
    loads = [h for h in headers if h[0] == 1]
    assert loads and all(h[7] >= 16384 and h[2] % 16384 == h[3] % 16384 for h in loads), name
    dynamic = next((h for h in headers if h[0] == 2), None)
    if dynamic is None:
        return set()
    tags = []
    for position in range(dynamic[2], dynamic[2] + dynamic[5], 16):
        tag, value = struct.unpack_from("<qQ", data, position)
        if not tag:
            break
        tags.append((tag, value))
    address = next((v for t, v in tags if t == 5), None)
    if address is None:
        return set()
    segment = next(h for h in loads if h[3] <= address < h[3] + h[5])
    strings = segment[2] + address - segment[3]
    return {data[strings + v:data.index(0, strings + v)].decode("ascii") for t, v in tags if t == 1}


def inspect(edition, release):
    path = ROOT / f".build/releases/mdo-android-{edition}-arm64-v8a.apk"
    with zipfile.ZipFile(path) as apk:
        assert apk.testzip() is None
        names = apk.namelist()
        assert len(names) == len(set(names)), "duplicate APK entries"
        metadata = json.loads(apk.read("assets/mdo-runtime.json"))
        assert metadata["edition"] == edition
        assert int(metadata["build_id"]) == release[f"android_{edition}_build_id"]
        assert metadata["revision"] == release["toolpack_revision"]
        native = {n.rsplit("/", 1)[1]: n for n in names if n.startswith("lib/")}
        assert native and all(n.startswith("lib/arm64-v8a/lib") and n.endswith(".so") for n in native.values())
        for name in native.values():
            info = apk.getinfo(name)
            assert info.compress_type == zipfile.ZIP_STORED, name
            # zipalign places each uncompressed native entry on a 16 KiB boundary.
            with path.open("rb") as stream:
                stream.seek(info.header_offset)
                header = stream.read(30)
            length, extra = struct.unpack_from("<HH", header, 26)
            assert (info.header_offset + 30 + length + extra) % 16384 == 0, name
            missing = elf_dependencies(apk.read(name), name) - native.keys() - SYSTEM_LIBRARIES
            assert not missing, (name, missing)
        mapped = set()
        logical_paths = set()
        for record in metadata["files"]:
            relative = record["path"]
            assert relative not in logical_paths and not relative.startswith("/") and ".." not in relative.split("/")
            logical_paths.add(relative)
            if "native" in record:
                assert record["native"] in native, record
                mapped.add(record["native"])
            else:
                data = apk.read("assets/mdo-runtime/" + relative)
                assert not data.startswith(b"\x7fELF"), relative
                assert hashlib.sha256(data).hexdigest() == record["sha256"], relative
        assert mapped == native.keys() - {"libxs.so"}, "unmapped native tools"
        if edition == "lite":
            assert len(native) == 1 and not metadata["files"]
        else:
            assert {"busybox/busybox", "busybox/sh", "curl/curl", "jq/jq", "openssh/ssh", "openssh/scp", "openssh/sftp", "python/bin/python3"} <= logical_paths
            assert len(native) > 5
        return {"edition": edition, "bytes": path.stat().st_size, "native_elf_files": len(native), "runtime_paths": len(logical_paths)}


def main():
    release = json.loads((ROOT / "app/release.json").read_text())
    result = {"passed": True, "checks": "APK CRC, IDs, native/data mapping, dependencies, 16 KiB ELF and ZIP alignment", "apks": [inspect(e, release) for e in ("lite", "full")]}
    report = ROOT / ".build/releases/android-validation.json"
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
