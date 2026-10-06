"""Check catalog hashes, archive contents, CPU architecture and Android ELF alignment."""
from pathlib import Path
import hashlib
import json
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
catalog = json.loads((ROOT / 'catalog.json').read_text(encoding='utf-8'))

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            value.update(block)
    return value.hexdigest()

native = []
file_count = 0
for source in catalog['source_archives']:
    path = ROOT / source['path']
    assert path.stat().st_size == source['bytes'] and digest(path) == source['sha256'], path
for item in catalog['tools']:
    directory = ROOT / item['directory']
    metadata = item['archive']
    archive_path = ROOT / metadata['path']
    assert digest(archive_path) == metadata['sha256'], archive_path
    assert archive_path.stat().st_size == metadata['bytes'], archive_path
    expected = {item['id'] + '/' + file['path'] for file in item['files']}
    with zipfile.ZipFile(archive_path) as archive:
        assert set(archive.namelist()) == expected, archive_path
        for record in item['files']:
            path = directory / record['path']
            assert path.stat().st_size == record['bytes'] and digest(path) == record['sha256'], path
            info = archive.getinfo(item['id'] + '/' + record['path'])
            assert (info.external_attr >> 16) & 0o777 == int(record['mode'], 8), path
            assert hashlib.sha256(archive.read(info)).hexdigest() == record['sha256'], path
            file_count += 1
            if item['platform'] == 'android-arm64-v8a':
                with path.open('rb') as stream:
                    data = stream.read(4096)
                if data[:4] != b'\x7fELF':
                    continue
                assert data[4:6] == b'\x02\x01', path
                assert struct.unpack_from('<H', data, 18)[0] == 183, path
                offset = struct.unpack_from('<Q', data, 32)[0]
                size, count = struct.unpack_from('<HH', data, 54)
                alignment = []
                for index in range(count):
                    header = struct.unpack_from('<IIQQQQQQ', data, offset + index * size)
                    if header[0] == 1:
                        assert header[-1] >= 16384, (path, header[-1])
                        assert header[2] % 16384 == header[3] % 16384, path
                        alignment.append(header[-1])
                native.append({'file': path.relative_to(ROOT).as_posix(), 'machine': 'AArch64',
                               'minimum_load_alignment': min(alignment)})
        if item['platform'] == 'windows-x86_64':
            for entry in item['entrypoints']:
                data = (directory / entry).read_bytes()
                assert data[:2] == b'MZ', entry
                offset = struct.unpack_from('<I', data, 0x3c)[0]
                assert data[offset:offset+4] == b'PE\0\0', entry
                assert struct.unpack_from('<H', data, offset+4)[0] == 0x8664, entry
print(json.dumps({'packages': len(catalog['tools']), 'verified_files': file_count,
                  'android_native_files': len(native), 'alignment': '>=16KiB', 'passed': True}))
report = ROOT.parents[1] / '.build/runtime-tools/checks/packages.json'
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(json.dumps({'passed': True, 'packages': len(catalog['tools']),
                             'verified_files': file_count, 'android_elf': native}, indent=2) + '\n', encoding='utf-8')
