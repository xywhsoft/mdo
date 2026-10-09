"""Check the actual EXE receipt and this fix's embedded source bytes."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import struct
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from build_android import extract_pack

FILES = ('src/api/decision_cache.c', 'src/api/decision_cache.h', 'src/api/asks.c',
    'src/api/approvals.c', 'src/api/router.c', 'web/js/state/decision-action.js',
    'web/js/state/asks.js', 'web/js/state/tasks.js', 'web/js/state/approvals.js',
    'web/js/features/asks/ask-card.js', 'web/js/features/chat/conversation-docks.js',
    'web/js/utils/dom.js', 'web/lang/en-US.json', 'web/lang/ru-RU.json', 'web/lang/zh-CN.json')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files_from_pack(pack):
    count = struct.unpack_from('<I', pack, 20)[0]
    position, size = struct.unpack_from('<QQ', pack, 40)
    end = position + size
    assert end == len(pack) - 32
    assert zlib.crc32(pack[position:end]) == struct.unpack_from('<I', pack, 72)[0]
    result = {}
    for _ in range(count):
        length, codec = struct.unpack_from('<HB', pack, position)
        offset, stored, original, crc = struct.unpack_from('<QQQI', pack, position + 8)
        name = pack[position + 40:position + 40 + length].decode('utf-8')
        assert name not in result and offset + stored <= end - size
        data = pack[offset:offset + stored]
        if codec == 1:
            properties = data[0]
            dictionary = struct.unpack_from('<I', data, 1)[0]
            decoder = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[{
                'id': lzma.FILTER_LZMA1, 'dict_size': dictionary, 'lc': properties % 9,
                'lp': (properties // 9) % 5, 'pb': properties // 45}])
            # XRTPACK stores the decoded size separately and permits LZMA1
            # without an end marker. Bound output like xrtVfsPackLoadBlob.
            data = decoder.decompress(data[5:], max_length=original)
        else:
            assert codec == 0
        assert len(data) == original and zlib.crc32(data) == crc, (name, codec,
            len(data), original, zlib.crc32(data), crc)
        result[name] = data
        position += 40 + length
    assert position == end
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'app')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    receipt = json.loads(args.exe.with_name('build.json').read_text())
    raw = args.exe.read_bytes()
    assert receipt['sha256'] == sha(raw) and receipt['bytes'] == len(raw)
    assert not receipt['mdo_worktree_dirty']
    assert receipt['xs_commit'] == json.loads((ROOT / 'deps.lock').read_text())['xserver']['commit']
    with tempfile.TemporaryDirectory() as directory:
        archive = Path(directory) / 'app.xrtpack'
        extract_pack(args.exe, archive)
        pack = archive.read_bytes()
        entries = files_from_pack(pack)
    sources = {}
    for name in FILES:
        expected = (args.source / name).read_bytes()
        assert entries[name] == expected, name
        sources[name] = {'sha256': sha(expected), 'matches': True}
    assert not any('decision-cache-check' in name or 'qa-decision' in name for name in entries)
    report = {key: receipt[key] for key in ('mdo_commit', 'xs_commit', 'mdo_worktree_dirty',
        'sha256', 'bytes', 'packaged_app_sha256')}
    report.update(pack_sha256=sha(pack), sources=sources, passed=True,
        daily_app_overwritten=False, android_device_tested=False, external_provider_tested=False)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS clean candidate and {len(sources)} exact VFS source files')


if __name__ == '__main__':
    main()
