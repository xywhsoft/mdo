"""Create deterministic per-tool ZIP archives and a local deployment catalog."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = json.loads(Path(__file__).with_name('sources.lock.json').read_text(encoding='utf-8'))
SOURCE_MAP = {x['file']: x for x in SOURCES}
TOOLS = {
    'windows-x86_64': {
        'busybox': ('1.38.0-FRP-6075-g169694ebd', 'core', ['busybox.exe'], ['busybox64u.exe', 'busybox-w32-FRP-6075-g169694ebd.tgz']),
        'curl': ('8.13.0', 'core', ['curl.exe'], ['cacert.pem', 'curl-windows-COPYING']),
        'jq': ('1.8.2', 'core', ['jq.exe'], ['jq-windows-amd64.exe', 'jq-1.8.2.tar.gz']),
        'openssh': ('10.3p1', 'core', ['ssh.exe', 'scp.exe', 'sftp.exe', 'ssh-keygen.exe', 'ssh-keyscan.exe', 'ssh-agent.exe', 'ssh-add.exe'], []),
        'python': ('3.13.7', 'optional', ['python.exe'], []),
        'git': ('2.55.0.windows.3', 'optional', ['cmd/git.exe'], []),
    },
    'android-arm64-v8a': {
        'busybox': ('1.38.0', 'core', ['busybox'], ['busybox-1.38.0.tar.bz2']),
        'curl': ('8.22.0', 'core', ['curl'], ['curl-8.22.0.tar.xz', 'openssl-3.5.9.tar.gz', 'zlib-1.3.1.tar.gz', 'cacert.pem']),
        'jq': ('1.8.2', 'core', ['jq'], ['jq-1.8.2.tar.gz']),
        'openssh': ('10.3p1', 'core', ['ssh', 'scp', 'sftp', 'ssh-keygen', 'ssh-keyscan'], ['openssh-10.3p1.tar.gz', 'openssl-3.5.9.tar.gz', 'zlib-1.3.1.tar.gz']),
        'python': ('3.14.8', 'optional', ['bin/python3'], ['python-3.14.8-aarch64-linux-android.tar.gz', 'cacert.pem']),
    },
}

def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()

def executable(path, entrypoints):
    return path.as_posix() in entrypoints or path.suffix.lower() in ('.exe', '.dll', '.so', '.pyd')

packages = ROOT / 'packages'
packages.mkdir(exist_ok=True)
catalog = {'schema_version': 1, 'release': '2026-10-06', 'status': 'prepared-local',
           'deployment_base_url': None, 'archive_layout': '<tool>/<relative-file>',
           'android_execution': 'APK-installed native executables required for app UID; ZIPs are staging artifacts.',
           'source_archives': [], 'tools': []}
for name in ('busybox-1.38.0.tar.bz2', 'busybox-w32-FRP-6075-g169694ebd.tgz'):
    path = ROOT / 'sources' / name
    if not path.is_file():
        raise FileNotFoundError(path)
    catalog['source_archives'].append({'path': path.relative_to(ROOT).as_posix(),
                                       'bytes': path.stat().st_size, 'sha256': sha256(path),
                                       'upstream': SOURCE_MAP[name]})
for platform, tools in TOOLS.items():
    for tool, (version, group, entrypoints, sources) in tools.items():
        directory = ROOT / platform / tool
        for entry in entrypoints:
            if not (directory / entry).is_file():
                raise FileNotFoundError(directory / entry)
        files = []
        archive_path = packages / f'{tool}-{version}-{platform}.zip'
        with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(directory.rglob('*')):
                if not path.is_file():
                    continue
                relative = path.relative_to(directory)
                if relative.name in ('fixture_ed25519', 'ed25519', 'known_hosts'):
                    raise ValueError('Test credential in runtime: ' + str(path))
                mode = 0o755 if executable(relative, entrypoints) else 0o644
                info = zipfile.ZipInfo(tool + '/' + relative.as_posix(), date_time=(2026, 10, 6, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (0o100000 | mode) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, path.read_bytes(), compresslevel=9)
                files.append({'path': relative.as_posix(), 'bytes': path.stat().st_size,
                              'sha256': sha256(path), 'mode': oct(mode)})
        item = {'id': tool, 'platform': platform, 'version': version, 'group': group,
                'directory': directory.relative_to(ROOT).as_posix(), 'entrypoints': entrypoints,
                'unpacked_bytes': sum(x['bytes'] for x in files), 'files': files,
                'archive': {'path': archive_path.relative_to(ROOT).as_posix(),
                            'bytes': archive_path.stat().st_size, 'sha256': sha256(archive_path)},
                'sources': [SOURCE_MAP[name] for name in sources]}
        if platform.startswith('android'):
            item['build_api'] = 26
            item['tested_api'] = [36]
            item['minimum_api_verified'] = False
            item['linkage'] = 'static-bionic' if tool in ('busybox', 'jq') else 'dynamic-bionic-static-third-party'
            if tool == 'python':
                item['environment'] = {'MDO_PYTHON_HOME': '<python-root>', 'LD_LIBRARY_PATH': '<python-root>/lib',
                                       'SSL_CERT_FILE': '<python-root>/cacert.pem'}
                item['system_libraries'] = ['libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libz.so']
            if tool == 'openssh':
                item['scp_sftp_arguments'] = ['-S', '<absolute-ssh-path>']
                item['features_disabled'] = ['Kerberos', 'PAM', 'PKCS11-helper', 'security-key-helper', 'ssh-agent']
        elif tool in ('curl', 'python', 'git'):
            item['provenance'] = 'User-provided runtime preserved; file hashes recorded here.'
        elif tool == 'openssh':
            item['provenance'] = 'Matching Git for Windows 2.55.0 installation, clients plus recursively resolved MSYS DLL dependencies.'
            item['source_url'] = 'https://github.com/git-for-windows/MSYS2-packages'
        catalog['tools'].append(item)
        print(f'{platform}/{tool}: {archive_path.stat().st_size / 1048576:.2f} MiB ZIP')
(ROOT / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
lines = [f"{item['archive']['sha256']}  {Path(item['archive']['path']).name}" for item in catalog['tools']]
(packages / 'SHA256SUMS').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'Catalog ready: {len(catalog["tools"])} packages')
