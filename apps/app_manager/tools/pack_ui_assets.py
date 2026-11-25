"""Pack existing source-rendered monochrome PNGs as native 1-bit C assets."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import zlib


def pixels(path):
    raw = path.read_bytes()
    assert raw[:8] == b'\x89PNG\r\n\x1a\n'
    width, height, depth, colour, compression, filtering, interlace = struct.unpack('>IIBBBBB', raw[16:29])
    assert depth in (1, 8) and colour == 0 and not (compression or filtering or interlace)
    data, pos = b'', 8
    while pos < len(raw):
        size = struct.unpack_from('>I', raw, pos)[0]
        if raw[pos + 4:pos + 8] == b'IDAT':
            data += raw[pos + 8:pos + 8 + size]
        pos += size + 12
    scan = zlib.decompress(data)
    stride = (width * depth + 7) // 8
    assert len(scan) == height * (stride + 1)
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = scan[y * (stride + 1)]
        row = bytearray(scan[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a, b, c = row[x - 1] if x else 0, previous[x], previous[x - 1] if x else 0
            p = a + b - c
            near = min((abs(p - a), a), (abs(p - b), b), (abs(p - c), c), key=lambda pair: pair[0])[1]
            row[x] = (row[x] + (0, a, b, (a + b) // 2, near)[kind]) & 255
        rows.append(row)
        previous = row
    return width, height, depth, rows


def pack(path, width, height):
    source_w, source_h, depth, rows = pixels(path)
    result = bytearray(width * height // 8)
    for y in range(height):
        for x in range(width):
            sx, sy = x * source_w // width, y * source_h // height
            value = rows[sy][sx] if depth == 8 else ((rows[sy][sx // 8] >> (7 - sx % 8)) & 1) * 255
            if value < 128:
                result[y * (width // 8) + x // 8] |= 0x80 >> (x % 8)
    return result


source = Path(sys.argv[1])
output = Path(__file__).resolve().parents[1]
icons = ['codex_board', 'messaging', 'doom', 'aliens', 'rocks', 'hackaday', 'video']
shots = ['codex-board-demo', 'messaging-screenshot']
parts = ['// Source-rendered assets; screenshots use fictional DEMO fixtures.', '#include "manager_ui.h"']
provenance = {}
for symbol, names, size in [('am_icons', ['icon-' + name for name in icons], (32, 32)),
                             ('am_shots', shots, (160, 112))]:
    parts.append(f'const uint8_t {symbol}[{len(names)}][{size[0] * size[1] // 8}] = {{')
    for name in names:
        path = source / (name + '.png')
        data = pack(path, *size)
        parts.append('    { // ' + name)
        parts += ['        ' + ','.join('0x%02x' % byte for byte in data[i:i + 16]) + ','
                  for i in range(0, len(data), 16)]
        parts.append('    },')
        provenance[name] = {'source_png_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                            'packed_sha256': hashlib.sha256(data).hexdigest(), 'size': size}
    parts.append('};')
(output / 'manager_assets.c').write_text('\n'.join(parts) + '\n')
# SpaceInv is a separate tiny asset so neither source file grows beyond the local style limit.
path=source/'spaceinv-v1-source-render.png'
data=pack(path,160,112)
native=['// Actual SpaceInv v1 software body capture; OS top bar omitted.', '#include "manager_ui.h"',
        'const uint8_t am_native_shots[1][AM_SHOT_BYTES] = {{']
native += ['    '+','.join('0x%02x'%byte for byte in data[i:i+16])+',' for i in range(0,len(data),16)]
native += ['}};']
(output/'manager_native_asset.c').write_text('\n'.join(native)+'\n')
provenance['spaceinv-v1-source-render']={'source_png_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
    'packed_sha256':hashlib.sha256(data).hexdigest(),'size':[160,112],
    'module_source_commit':'60f15469d5348697da9ef5995568ee103973a9d6',
    'scope':'Actual module software render, not a hardware screenshot.'}
(output / 'ui_asset_provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
print('Packed 7 app icons and 3 source screenshots; no optional app code references.')
