"""One ordinary valid PNG with deterministic, poorly compressible RGB pixels."""
from __future__ import annotations

import binascii
import random
import struct
import zlib


def ordinary_png() -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff))

    width, height = 768, 1024
    pixels = random.Random(29).randbytes(width * height * 3)
    raw = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3]
                   for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b""))
