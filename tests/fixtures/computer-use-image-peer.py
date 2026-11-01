"""Offline MCP image boundary fixture; never controls the desktop."""
import base64
import binascii
import json
import random
import struct
import sys
import zlib


def image():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data))
    rng = random.Random(17)
    pixels = b''.join(b'\0' + rng.randbytes(1250 * 3) for _ in range(512))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1250, 512, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    for line in sys.stdin:
        request = json.loads(line)
        if 'id' not in request:
            continue
        method = request['method']
        if method == 'initialize':
            result = {'protocolVersion': request['params']['protocolVersion'],
                      'capabilities': {'tools': {}}, 'serverInfo': {'name': 'large-image', 'version': '1'}}
        elif method == 'tools/call':
            result = {'content': [{'type': 'image', 'mimeType': 'image/png',
                                   'data': base64.b64encode(image()).decode('ascii')}]}
        else:
            result = {'tools': []}
        print(json.dumps({'jsonrpc': '2.0', 'id': request['id'], 'result': result}), flush=True)
