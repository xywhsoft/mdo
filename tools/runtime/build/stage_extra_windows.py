"""Stage locked upstream aria2/ripgrep/7-Zip Windows releases and source archive."""
from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[3]
DOWNLOADS=ROOT/'.build/runtime-tools/downloads'
STAGE=ROOT/'.build/runtime-tools/extra-staging'
OUT=ROOT/'tools/runtime/windows-x86_64'
records=json.loads(Path(__file__).with_name('sources.lock.json').read_text())
names=['aria2-1.37.0-win-64bit-build1.zip','aria2-1.37.0.tar.xz','ripgrep-15.2.0-x86_64-pc-windows-msvc.zip','ripgrep-15.2.0.tar.gz','7z2604-x64.exe','7z2604-src.7z','7zr.exe']
for name in names:
    record=next(r for r in records if r['file']==name);p=DOWNLOADS/name
    assert p.stat().st_size==record['size'] and hashlib.sha256(p.read_bytes()).hexdigest()==record['sha256'],p
STAGE.mkdir(parents=True,exist_ok=True)
for name in names[:1]+names[2:3]:
    with zipfile.ZipFile(DOWNLOADS/name) as archive:archive.extractall(STAGE)
subprocess.run([str(DOWNLOADS/'7zr.exe'),'x',str(DOWNLOADS/'7z2604-x64.exe'),'-o'+str(STAGE/'7zip'),'-y'],check=True,stdout=subprocess.DEVNULL)
subprocess.run([str(STAGE/'7zip/7z.exe'),'x',str(DOWNLOADS/'7z2604-src.7z'),'-o'+str(STAGE/'7zip-source'),'-y'],check=True,stdout=subprocess.DEVNULL)
for tool,source,files in [
    ('aria2',STAGE/'aria2-1.37.0-win-64bit-build1',['aria2c.exe','COPYING','LICENSE.OpenSSL','AUTHORS']),
    ('ripgrep',STAGE/'ripgrep-15.2.0-x86_64-pc-windows-msvc',['rg.exe','COPYING','LICENSE-MIT','UNLICENSE']),
    ('7zip',STAGE/'7zip',['7z.exe','7z.dll','License.txt'])]:
    target=OUT/tool;target.mkdir(parents=True,exist_ok=True)
    for file in files:shutil.copy2(source/file,target/file)
sources=ROOT/'tools/runtime/sources';sources.mkdir(exist_ok=True)
for name in ['aria2-1.37.0.tar.xz','ripgrep-15.2.0.tar.gz','7z2604-src.7z']:shutil.copy2(DOWNLOADS/name,sources/name)
print('Windows extra tools staged; original source archives retained')
