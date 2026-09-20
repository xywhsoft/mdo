#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 xx_exe.ico 解出 32bpp 图像并导出 PNG（多尺寸，网页品牌位用）"""
import io, struct, zlib, os

ICO = r"D:\GIT\xserver\res\xx_exe.ico"
OUT = r"D:\GIT\xserver\release\mdo\app\wwwroot\res"

data = open(ICO, "rb").read()
n = struct.unpack("<H", data[4:6])[0]

frames = []
for i in range(n):
    off = 6 + i * 16
    w, h, colors, _, planes, bpp, size, offset = struct.unpack("<BBBBHHII", data[off:off + 16])
    w = w or 256; h = h or 256
    frames.append((w, h, bpp, size, offset))

def dib_to_rgba(dib, w, h):
    # DIB: BITMAPINFOHEADER(40) + BGRA 像素（自底向上）+ AND mask
    hdr = struct.unpack("<IiiHHIIiiII", dib[:40])
    height2 = hdr[1]
    pixel_h = height2 // 2
    body = dib[40:]
    row = w * 4
    rows = []
    for y in range(pixel_h):
        src = (pixel_h - 1 - y) * row
        px = body[src:src + row]
        rgba = bytearray(row)
        for x in range(w):
            b, g, r, a = px[x*4:x*4+4]
            rgba[x*4:x*4+4] = bytes((r, g, b, a))
        rows.append(bytes(rgba))
    return b"".join(rows)

def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y*w*4:(y+1)*w*4] for y in range(h))
    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)
    print("wrote", path, w, "x", h, len(png), "bytes")

os.makedirs(OUT, exist_ok=True)

done = set()
for w, h, bpp, size, offset in frames:
    if w in done: continue
    body = data[offset:offset + size]
    if body[:4] == b"\x89PNG":
        open(os.path.join(OUT, f"mdo-icon-{w}.png"), "wb").write(body)
        print("wrote (png passthrough)", w)
    else:
        rgba = dib_to_rgba(body, w, h)
        write_png(os.path.join(OUT, f"mdo-icon-{w}.png"), w, h, rgba)
    done.add(w)

# 256 原生 PNG 另存一份高清源
print("done:", sorted(done))
