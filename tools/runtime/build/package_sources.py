"""Package locked tool source archives, build recipes and shipped notices."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[3]
BUILD = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--downloads', type=Path, default=ROOT / '.build/runtime-tools/downloads')
    parser.add_argument('--output', type=Path, default=ROOT / '.build/releases/runtime-sources.tar.gz')
    args = parser.parse_args()
    records = json.loads((BUILD / 'sources.lock.json').read_text(encoding='utf-8'))
    source_suffixes = ('.tar.gz', '.tar.xz', '.tar.bz2', '.tgz', '-src.7z')
    sources = [row for row in records if row['file'].endswith(source_suffixes)
               and 'linux-android.tar.gz' not in row['file']]
    files = []
    for row in sources:
        path = args.downloads / row['file']
        if path.stat().st_size != row['size'] or hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
            raise ValueError('Source checksum mismatch: ' + path.name)
        files.append((path, 'sources/' + path.name))
    files.extend((p, 'build/' + p.name) for p in BUILD.iterdir()
                 if p.is_file() and p.suffix in ('.py', '.sh', '.json', '.c', '.h'))
    for platform in ('windows-x86_64', 'android-arm64-v8a'):
        base = ROOT / 'tools/runtime' / platform
        for path in base.rglob('*'):
            if path.is_file() and (path.suffix.lower() in ('.txt', '.md', '.config')
                                  or path.name.upper().startswith(('LICENSE', 'COPYING', 'UNLICENSE', 'AUTHORS'))):
                files.append((path, 'notices/' + platform + '/' + path.relative_to(base).as_posix()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.output, 'w:gz', compresslevel=1) as archive:
        for path, name in sorted(files, key=lambda item: item[1]):
            archive.add(path, arcname=name, recursive=False)
        archive.add(ROOT / 'tools/runtime/README.md', arcname='README.md', recursive=False)
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(args.output.suffix + '.sha256').write_text(digest + '  ' + args.output.name + '\n')
    print(json.dumps({'file': str(args.output), 'sha256': digest,
                      'size': args.output.stat().st_size, 'source_archives': len(sources), 'entries': len(files) + 1}))


if __name__ == '__main__':
    main()
