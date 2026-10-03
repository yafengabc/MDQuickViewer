"""把 32bpp BI_RGB 的 BMP 转成 PNG，用于渲染自检与界面截图的可视化。
纯标准库实现，避免引入 PIL 依赖。

兼容两种高度写法：
  - 正高度：BMP 规范的自底向上存储，需倒序读行
  - 负高度：自顶向下存储（GDI DIB 常见），直接顺序读行
"""
import struct, zlib, sys

def convert(src, dst):
    d = open(src, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    hdr = struct.unpack_from('<I', d, 14)[0]
    w = struct.unpack_from('<i', d, 18)[0]
    h_raw = struct.unpack_from('<i', d, 22)[0]
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert hdr == 40 and bpp == 32, f'只支持 32bpp BITMAPINFOHEADER，实际 bpp={bpp} hdr={hdr}'
    top_down = h_raw < 0
    h = abs(h_raw)
    stride = w * 4

    def bgra_to_rgb(r):
        return bytes(b for i in range(0, len(r), 4) for b in (r[i + 2], r[i + 1], r[i]))

    rows = []
    if top_down:
        for y in range(h):
            r = d[off + y * stride: off + (y + 1) * stride]
            rows.append(b'\x00' + bgra_to_rgb(r))
    else:
        for y in range(h - 1, -1, -1):
            r = d[off + y * stride: off + (y + 1) * stride]
            rows.append(b'\x00' + bgra_to_rgb(r))
    raw = b''.join(rows)

    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))
    open(dst, 'wb').write(png)
    print(f'{dst} {w}x{h} ({"top-down" if top_down else "bottom-up"})')

if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
