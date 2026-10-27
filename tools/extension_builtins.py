"""Build deterministic, ordinary ZIPs from the product's extension directories."""
import hashlib
import io
import json
import stat
import zipfile

KINDS = ('agents', 'subagents', 'tools', 'mcp', 'skills', 'commands',
         'external-tools', 'knowledge', 'plugins')


def generate(root):
    target = root / 'app/generated/builtin-extensions'
    target.mkdir(parents=True, exist_ok=True)
    rows = []
    for kind in KINDS:
        parent = root / 'app/default-home' / kind
        for directory in sorted(parent.iterdir()) if parent.exists() else ():
            if not directory.is_dir() or not (directory / 'extension.json').is_file():
                continue
            meta = json.loads((directory / 'extension.json').read_text(encoding='utf-8'))
            if meta['kind'] != kind or not (directory / meta['entry']).is_file():
                raise ValueError('Invalid builtin extension: ' + str(directory))
            data = io.BytesIO()
            with zipfile.ZipFile(data, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
                for path in sorted(directory.rglob('*')):
                    if path.is_symlink():
                        raise ValueError('Builtin extension links are forbidden: ' + str(path))
                    name = path.relative_to(directory).as_posix() + ('/' if path.is_dir() else '')
                    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    mode = stat.S_IFDIR | 0o755 if path.is_dir() else stat.S_IFREG | (0o755 if path.stat().st_mode & 0o111 else 0o644)
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

