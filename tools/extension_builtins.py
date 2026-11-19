"""Build deterministic, ordinary ZIPs from the product's extension directories."""
import hashlib
import io
import json
import os
import stat
import subprocess
import zipfile

KINDS = ('agents', 'subagents', 'tools', 'mcp', 'skills', 'commands',
         'external-tools', 'knowledge', 'plugins')


def tracked_modes(root):
    # DrvFs may report every source file as executable. Git records the
    # intended bit regardless of whether the build runs in Windows or WSL.
    try:
        entries = subprocess.check_output(
            ['git', 'ls-files', '--stage', '-z', '--', 'app/default-home'],
            cwd=root, stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return {}
    modes = {}
    for entry in entries.split(b'\0'):
        if not entry:
            continue
        fields, name = entry.split(b'\t', 1)
        mode, _, stage = fields.split()
        if stage == b'0' and mode in (b'100644', b'100755'):
            modes[os.fsdecode(name)] = int(mode, 8)
    return modes


def generate(root):
    target = root / 'app/generated/builtin-extensions'
    target.mkdir(parents=True, exist_ok=True)
    modes = tracked_modes(root)
    rows = []
    for kind in KINDS:
        parent = root / 'app/default-home' / kind
        for directory in sorted(parent.iterdir(), key=lambda path: path.name) if parent.exists() else ():
            if not directory.is_dir() or not (directory / 'extension.json').is_file():
                continue
            meta = json.loads((directory / 'extension.json').read_text(encoding='utf-8'))
            if meta['kind'] != kind or not (directory / meta['entry']).is_file():
                raise ValueError('Invalid builtin extension: ' + str(directory))
            data = io.BytesIO()
            with zipfile.ZipFile(data, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for path in sorted(directory.rglob('*'), key=lambda path: path.as_posix()):
                    if path.is_symlink():
                        raise ValueError('Builtin extension links are forbidden: ' + str(path))
                    name = path.relative_to(directory).as_posix() + ('/' if path.is_dir() else '')
                    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    if path.is_dir():
                        mode = stat.S_IFDIR | 0o755
                    else:
                        mode = modes.get(path.relative_to(root).as_posix())
                        if mode is None:
                            mode = stat.S_IFREG | (0o755 if path.stat().st_mode & 0o111 else 0o644)
                    info.external_attr = mode << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, b'' if path.is_dir() else path.read_bytes())
            content = data.getvalue()
            name = kind + '-' + directory.name + '.zip'
            output = target / name
            if not output.exists() or output.read_bytes() != content:
                output.write_bytes(content)
            rows.append((kind, directory.name, name, hashlib.sha256(content).hexdigest()))
    index = '/* Generated from directory contents, never from user Home. */\n'
    index += 'static const struct {const char *Kind,*Id,*File,*Sha256;} MdoBuiltinExtensions[]={\n'
    index += ''.join('    {' + ','.join(json.dumps(field) for field in row) + '},\n' for row in rows)
    index += '};\n'
    path = target / 'index.h'
    if not path.exists() or path.read_text(encoding='utf-8') != index:
        path.write_text(index, encoding='utf-8')

