"""Reproduce the pinned, offline terminal renderer. No npm/Node runtime needed.

Only known files are read from verified immutable npm archives. JS source-map
directives are removed because the release does not bundle upstream source.
Run without --fetch to check the committed files without network access.
"""
import argparse, base64, hashlib, io, json, tarfile, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'app/web/js/vendor/terminal'
ASSETS = ROOT / 'app/web/assets/terminal'
PACKAGES = [
 ('@xterm/xterm', '6.0.0', 'TQwDdQGtwwDt+2cgKDLn0IRaSxYu1tSUjgKarSDkUM0ZNiSRXFpjxEsvc/Zgc5kq5omJ+V0a8/kIM2WD3sMOYg=='),
 ('@xterm/addon-fit', '0.11.0', 'jYcgT6xtVYhnhgxh3QgYDnnNMYTcf8ElbxxFzX0IZo+vabQqSPAjC3c1wJrKB5E19VwQei89QCiZZP86DCPF7g=='),
 ('@xterm/addon-search', '0.16.0', '9OeuBFu0/uZJPu+9AHKY6g/w0Czyb/Ut0A5t79I4ULoU4IfU5BEpPFVGQxP4zTTMdfZEYkVIRYbHBX1xWwjeSA=='),
 ('@xterm/addon-unicode11', '0.9.0', 'FxDnYcyuXhNl+XSqGZL/t0U9eiNb/q3EWT5rYkQT/zuig8Gz/VagnQANKHdDWFM2lTMk9ly0EFQxxxtZUoRetw=='),
]

def main():
 p=argparse.ArgumentParser();p.add_argument('--fetch',action='store_true');a=p.parse_args()
 manifest=ASSETS/'manifest.json'
 if a.fetch:
  DEST.mkdir(parents=True,exist_ok=True);ASSETS.mkdir(parents=True,exist_ok=True);records=[]
  for package,version,integrity in PACKAGES:
   name=package.split('/')[-1];url=f'https://registry.npmjs.org/{package}/-/{name}-{version}.tgz'
   archive=urllib.request.urlopen(url,timeout=30).read()
   assert base64.b64encode(hashlib.sha512(archive).digest()).decode()==integrity,package+' integrity mismatch'
   with tarfile.open(fileobj=io.BytesIO(archive),mode='r:gz') as tar:
    targets=[(f'package/lib/{name}.mjs',DEST/(name+'.js')),( 'package/LICENSE',ASSETS/(name+'-LICENSE'))]
    if name=='xterm':targets.append(('package/css/xterm.css',ASSETS/'xterm.css'))
    files=[]
    for source,target in targets:
     data=tar.extractfile(source).read()
     if source.endswith('.mjs'):data=b'\n'.join(line for line in data.split(b'\n') if not line.startswith(b'//# sourceMappingURL='))
     target.write_bytes(data);files.append({'path':target.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(data).hexdigest()})
    records.append({'package':package,'version':version,'source':url,'integrity':'sha512-'+integrity,'license':'MIT','files':files})
  manifest.write_text(json.dumps({'upstream':'https://github.com/xtermjs/xterm.js','packages':records},indent=2)+'\n',encoding='utf-8')
 records=json.loads(manifest.read_text(encoding='utf-8'))['packages']
 assert [(r['package'],r['version'],r['integrity'][7:]) for r in records]==PACKAGES
 for record in records:
  for file in record['files']:assert hashlib.sha256((ROOT/file['path']).read_bytes()).hexdigest()==file['sha256'],file['path']
 print('Pinned terminal assets and licenses: verified')

if __name__=='__main__':main()
