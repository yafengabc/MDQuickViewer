"""把 32bpp BI_RGB 的 BMP 转成 PNG，用于渲染自检的可视化。
纯标准库实现，避免引入 PIL 依赖。"""
import struct, zlib, sys

def convert(src, dst):
    d = open(src, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    hdr = struct.unpack_from('<I', d, 14)[0]
    w = struct.unpack_from('<i', d, 18)[0]
    h = struct.unpack_from('<i', d, 22)[0]
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert hdr == 40 and bpp == 32, f'只支持 32bpp BITMAPINFOHEADER，实际 bpp={bpp} hdr={hdr}'
    stride = w * 4
    rows = b''
    # BMP 正高度 = 自底向上，逐行倒序读
    for y in range(h - 1, -1, -1):
        r = d[off + y * stride: off + (y + 1) * stride]
        rows += b'\x00' + bytes(b for i in range(0, len(r), 4) for b in (r[i + 2], r[i + 1], r[i]))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(rows, 6))
           + chunk(b'IEND', b''))
    open(dst, 'wb').write(png)
    print(f'{dst} {w}x{h}')

if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
