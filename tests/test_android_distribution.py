"""Inspect built APKs without a device: metadata, native mapping and ELF closure."""
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SYSTEM_LIBRARIES = {"libc.so", "libdl.so", "libm.so", "liblog.so", "libz.so", "libandroid.so",
                    "libmediandk.so", "libcamera2ndk.so"}


def elf_program(data):
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        return False
    offset = struct.unpack_from('<Q', data, 32)[0]
    width, count = struct.unpack_from('<HH', data, 54)
    return struct.unpack_from('<H', data, 16)[0] == 2 or any(
        struct.unpack_from('<I', data, offset+i*width)[0] == 3 for i in range(count))


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


def inspect(edition, release, path=None):
    path = path or ROOT / ("mdo-arm64-v8a.apk" if edition == "lite" else "mdo-full-arm64-v8a.apk")
    with zipfile.ZipFile(path) as apk:
        assert apk.testzip() is None
        names = apk.namelist()
        assert len(names) == len(set(names)), "duplicate APK entries"
        dex = b"".join(apk.read(name) for name in names if name.endswith(".dex"))
        assert "更新文件超过 512 MiB 上限".encode("utf-8") in dex, "mdo installer policy missing from APK"
        assert "更新文件路径或大小无效".encode("utf-8") not in dex, "old 32 MiB SDK installer shipped"
        metadata = json.loads(apk.read("assets/mdo-runtime.json"))
        assert metadata["edition"] == edition
        assert int(metadata["build_id"]) == release[f"android_{edition}_build_id"]
        assert metadata["version_code"] == release["version_code"].replace(".", "").upper()
        assert 'revision' not in metadata and 'extensions' in metadata
        native = {n.rsplit("/", 1)[1]: n for n in names if n.startswith("lib/")}
        assert native and all(n.startswith("lib/arm64-v8a/lib") and n.endswith(".so") for n in native.values())
        host_files = {"libxs.so", "libmdo_command_worker.so", "libmdo_cli_forwarder.so"}
        assert host_files <= native.keys(), "C command runtime/helper missing"
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
        logical_paths = set();commands=set()
        for tool in metadata['extensions']:
            payload=apk.read(tool['asset']);assert len(payload)==tool['bytes'] and hashlib.sha256(payload).hexdigest()==tool['sha256']
            with zipfile.ZipFile(__import__('io').BytesIO(payload)) as bundle:
                assert bundle.testzip() is None and len(bundle.namelist())==len(set(bundle.namelist()))
                manifest=json.loads(bundle.read('extension.json'));assert manifest['kind']=='external-tools' and manifest['alias']==tool['id']
                assert not {'format','files','resources','payload','min_build_id'} & manifest.keys()
                recipe=json.loads(bundle.read(manifest['entry']));assert recipe['program'] in bundle.namelist()
                assert elf_program(bundle.read(recipe['program'])), (tool['id'], 'main entry is not a program')
                for command in recipe['commands']:
                    assert elf_program(bundle.read(command['entry'])), (tool['id'], command, 'shared library exported as a command')
                commands.update(c['name'] for c in recipe['commands'])
                logical_paths.update(tool['id']+'/'+p for p in bundle.namelist())
        native_paths=set()
        for record in metadata["files"]:
            relative = record['tool']+'/'+record["path"]
            assert relative not in native_paths and not relative.startswith("/") and ".." not in relative.split("/");native_paths.add(relative)
            assert record["native"] in native, record
            assert hashlib.sha256(apk.read(native[record['native']])).hexdigest()==record['sha256']
            assert record.get('alias') or relative in logical_paths
            mapped.add(record["native"])
        assert mapped == native.keys() - host_files, "unmapped native tools"
        if edition == "lite":
            assert len(native) == len(host_files) and not metadata["files"] and not metadata['extensions']
        else:
            assert {"busybox/busybox", "curl/curl", "jq/jq", "openssh/ssh", "openssh/scp", "openssh/sftp", "python/bin/python3", "aria2/aria2c", "ripgrep/rg", "7zip/7zz"} <= logical_paths
            assert {'sh','awk','python','python3','git','rg','7zz'}<=commands
            assert len(native) > 5
            if release["toolpack_revision"] >= 4:
                assert {"git/git", "sqlite/sqlite3", "poppler/pdftotext", "poppler/pdftoppm",
                        "ffmpeg/ffmpeg", "ffmpeg/ffprobe", "imagemagick/magick", "poppler/fonts/fonts.conf"} <= logical_paths
                assert {'git','sqlite3','pdfinfo','pdftotext','pdftoppm','ffmpeg','ffprobe','magick'} <= commands
                assert not {"liblzma.so", "libcrypto.so", "libssl.so"} & native.keys(), "external libraries shadow Android framework dependencies"
        return {"edition": edition, "bytes": path.stat().st_size, "native_elf_files": len(native), "runtime_paths": len(logical_paths)}


def main():
    release = json.loads((ROOT / "app/release.json").read_text())
    result = {"passed": True, "checks": "APK CRC, IDs, native/data mapping, dependencies, 16 KiB ELF and ZIP alignment", "apks": [inspect(e, release) for e in ("lite", "full")]}
    report = ROOT / ".build/releases/android-validation.json"
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
