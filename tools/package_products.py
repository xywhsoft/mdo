#!/usr/bin/env python3
"""Package verified platform builds as executable + portable mdo-home/docs."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    root = args.dist.resolve()
    packages = root / 'packages'; packages.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'deps.lock').read_text(encoding='utf-8'))
    products = []
    for directory in sorted(root.iterdir()):
        if not directory.is_dir() or directory.is_symlink() or not re.fullmatch(
                r'(linux-(x86_64|arm64)-(glibc|musl)|windows-x86_64)-(gui|server)', directory.name):
            continue
        receipt = json.loads((directory / 'build.json').read_text(encoding='utf-8'))
        name = ('mdo-server' if receipt['edition'] == 'server' else 'mdo') + ('.exe' if receipt['platform'] == 'windows' else '')
        binary = directory / name
        if receipt['xs_commit'] != lock['xserver']['commit'] or hashlib.sha256(binary.read_bytes()).hexdigest() != receipt['sha256']:
            raise ValueError('stale or changed product: ' + directory.name)
        entries = {name: binary.read_bytes(), 'mdo-home/docs/build.json': (directory / 'build.json').read_bytes(),
                   'mdo-home/docs/README.md': (ROOT / 'docs/linux.md').read_bytes()}
        if (ROOT / 'LICENSE').is_file():
            entries['mdo-home/docs/LICENSE'] = (ROOT / 'LICENSE').read_bytes()
        if receipt['edition'] == 'server' and receipt['platform'] == 'linux':
            entries['mdo-home/docs/mdo.service'] = (ROOT / 'tools/service/mdo.service').read_bytes()
        suffix = '.zip' if receipt['platform'] == 'windows' else '.tar.gz'
        archive = packages / (directory.name + suffix)
        temporary = archive.with_suffix(archive.suffix + '.pending')
        try:
            if suffix == '.zip':
                with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as target:
                    for relative, content in entries.items():
                        target.writestr(directory.name + '/' + relative, content)
            else:
                with tarfile.open(temporary, 'w:gz') as target:
                    for relative, content in entries.items():
                        member = tarfile.TarInfo(directory.name + '/' + relative)
                        member.size = len(content); member.mode = 0o755 if relative == name else 0o644
                        target.addfile(member, io.BytesIO(content))
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
        products.append({'product': directory.name, 'binary': directory.name + '/' + name,
                         'binary_bytes': receipt['bytes'], 'binary_sha256': receipt['sha256'],
                         'archive': 'packages/' + archive.name, 'archive_bytes': archive.stat().st_size,
                         'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    if not products:
        raise ValueError('no built products')
    manifest = {'schema_version': 1, 'xs_commit': lock['xserver']['commit'], 'products': products}
    (packages / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
