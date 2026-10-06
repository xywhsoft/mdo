"""Download locked upstream artifacts, or verify the existing download cache."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--downloads', type=Path, default=ROOT / '.build/runtime-tools/downloads')
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
args.downloads.mkdir(parents=True, exist_ok=True)
records = json.loads(Path(__file__).with_name('sources.lock.json').read_text())

def verify(path, record):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    if path.stat().st_size != record['size'] or digest.hexdigest() != record['sha256']:
        raise ValueError('Artifact hash/size mismatch: ' + path.name)

def fetch(record):
    path = args.downloads / record['file']
    if not path.is_file():
        if args.verify_only:
            raise FileNotFoundError(path)
        request = urllib.request.Request(record['url'], headers={'Accept-Encoding': 'identity', 'User-Agent': 'mdo-tools/1'})
        pending = path.with_name(path.name + '.part')
        try:
            with urllib.request.urlopen(request, timeout=120) as response, pending.open('wb') as output:
                for block in iter(lambda: response.read(1024 * 1024), b''):
                    output.write(block)
            verify(pending, record)
            pending.replace(path)
        finally:
            pending.unlink(missing_ok=True)
    verify(path, record)
    return path.name

with ThreadPoolExecutor(max_workers=4) as workers:
    for name in workers.map(fetch, records):
        print(name + ': verified')
