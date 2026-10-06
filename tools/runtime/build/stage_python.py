from pathlib import Path
import json
import shutil
import zipfile
import tarfile

repo = Path(__file__).resolve().parents[3]
root = repo / 'tools/runtime/android-arm64-v8a'
source = repo / '.build/runtime-tools/python-sdk/prefix'
if not source.is_dir():
    with tarfile.open(repo / '.build/runtime-tools/downloads/python-3.14.8-aarch64-linux-android.tar.gz') as archive:
        archive.extractall(source.parent, filter='data')
python = root / 'python'
(python / 'lib/python3.14/lib-dynload').mkdir(parents=True, exist_ok=True)
(python / 'bin').mkdir(exist_ok=True)
# Keep only runtime libraries: the generic .so aliases and SDK headers belong
# in the build cache. The official Python-specific names are DT_NEEDED names.
for name in ('libpython3.14.so', 'libpython3.so', 'libcrypto_python.so', 'libssl_python.so', 'libsqlite3_python.so'):
    shutil.copy2(source / 'lib' / name, python / 'lib' / name)
stdlib = source / 'lib/python3.14'
skip = {'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip', '__pycache__', 'site-packages', 'lib-dynload', '__phello__'}
with zipfile.ZipFile(python / 'lib/python314.zip', 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for file in sorted(stdlib.rglob('*')):
        relative = file.relative_to(stdlib)
        if file.is_file() and file.suffix in ('.py', '.json') and not any(part in skip or part.startswith('config-') for part in relative.parts):
            archive.write(file, relative.as_posix())
for file in (stdlib / 'lib-dynload').glob('*.so'):
    if not file.name.startswith(('_test', '_ctypes_test')):
        shutil.copy2(file, python / 'lib/python3.14/lib-dynload' / file.name)
shutil.copy2(stdlib / 'LICENSE.txt', python / 'LICENSE.txt')
shutil.copy2(repo / '.build/runtime-tools/python-sdk/README.md', python / 'UPSTREAM-README.md')
shutil.copy2(repo / '.build/runtime-tools/downloads/cacert.pem', python / 'cacert.pem')
print(json.dumps({'python_runtime_files': sum(1 for x in python.rglob('*') if x.is_file()), 'python_runtime_bytes': sum(x.stat().st_size for x in python.rglob('*') if x.is_file())}))
