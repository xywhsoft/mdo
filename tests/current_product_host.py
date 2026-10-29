"""Run current application sources over HTTP without creating a GUI or copy.

XS resolves its application root from the configuration directory. Keep a tiny
owned configuration beside the sources, and remove only our unchanged file.
"""
from contextlib import contextmanager
import json
from pathlib import Path
import tempfile

APP = Path(__file__).resolve().parents[1] / 'app'


@contextmanager
def product_http_config(port: int):
    value = json.loads((APP / 'xs.json').read_text(encoding='utf-8'))
    for service in value['services']:
        service.update({'class': 'http', 'ip': '127.0.0.1', 'port': port})
        service.pop('window', None)
    payload = json.dumps(value, ensure_ascii=False).encode('utf-8')
    with tempfile.NamedTemporaryFile(prefix='.qa-http-', suffix='.json', dir=APP,
                                     delete=False) as stream:
        path = Path(stream.name)
        stream.write(payload)
    try:
        yield path
    finally:
        if path.parent != APP or path.read_bytes() != payload:
            raise RuntimeError('Test HTTP configuration changed; refusing to remove it: ' + str(path))
        path.unlink()
