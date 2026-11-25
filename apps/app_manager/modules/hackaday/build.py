"""Build the standalone unsigned Hackaday ELF using the frozen ABI2 headers."""

import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
APP_ID = 'hackaday'
MAPPED = ('.text', '.bss', '.data', '.rodata', '.data.rel.ro', '.got.loc')
HEADER_HASHES = {
    'badge_native_api.h': '3c26055cc13984c6bd1aca086df442c74151509e7ae9334fbc41852522833e86',
    'badge_hackaday_api.h': '49f114241444af0eb4b3cf270a89d7b47e0a7e0394fabb0602d815ce9f6eb392',
}
SOURCES = ['hackaday_module', 'had_native_controller', 'had_feed_port', 'had_format', 'module_memory']
parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
cc = args.cc.resolve()
objcopy = cc.with_name(cc.name.replace('gcc', 'objcopy'))
strip = cc.with_name(cc.name.replace('gcc', 'strip'))
nm = cc.with_name(cc.name.replace('gcc', 'nm'))
version = re.search(r'#define HACKADAY_VERSION "([^"]+)"',
                    (ROOT / 'hackaday_module.c').read_text())[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*command):
    subprocess.run(command, check=True)


def sections(payload):
    assert payload[:7] == b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH', payload, 16) == (3, 94)
    offset = struct.unpack_from('<I', payload, 32)[0]
    stride, count, names = struct.unpack_from('<HHH', payload, 46)
    assert stride == 40 and offset + count * stride <= len(payload)
    fields = 'name_index type flags address offset size link info align entsize'.split()
    entries = [dict(zip(fields, struct.unpack_from('<10I', payload, offset + i * stride)))
               for i in range(count)]
    string_table = entries[names]
    start = string_table['offset']
    strings = payload[start:start + string_table['size']]
    return {strings[s['name_index']:].split(b'\0', 1)[0].decode(): s for s in entries}


assert all(digest(ROOT / name) == expected for name, expected in HEADER_HASHES.items())
objects = []
for name in SOURCES:
    obj = out / f'{name}.o'
    run(cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Os', '-fno-builtin',
        '-fno-tree-loop-distribute-patterns', '-fPIC', '-mlongcalls', '-mno-target-align',
        '-ffunction-sections', '-fdata-sections', '-fvisibility=hidden', '-fstack-usage',
        '-I', ROOT, '-c', ROOT / f'{name}.c', '-o', obj)
    objects.append(obj)

elf = out / f'{APP_ID}-{version}.app.elf'
metadata = out / 'module-info.bin'
run(objcopy, '--dump-section', f'.badge_app={metadata}', objects[0])
note = struct.pack('<II16s24s', 0x32504142, 2, APP_ID.encode(), version.encode())
assert metadata.read_bytes() == note
run(cc, '-nostartfiles', '-nostdlib', '-fPIC', '-shared', '-e', 'app_main',
    '-Wl,--gc-sections,--strip-all,--strip-debug,--strip-discarded', '-Wl,-T,' + str(ROOT / 'native-layout.ld'),
    *objects, '-o', elf)
if '.badge_app' not in sections(elf.read_bytes()):
    run(objcopy, '--add-section', f'.badge_app={metadata}', elf)
run(objcopy, '--set-section-flags', '.badge_app=readonly,contents',
    '--set-section-alignment', '.badge_app=4', elf)
run(strip, '--strip-unneeded', '--remove-section=.comment', '--remove-section=.dynamic',
    '--remove-section=.xt.lit', '--remove-section=.xt.prop', '--remove-section=.xtensa.info', elf)

payload = elf.read_bytes()
entries = sections(payload)
info = entries['.badge_app']
assert (info['type'], info['flags'], info['size'], info['align']) == (1, 0, 48, 4)
assert info['offset'] % 4 == 0 and payload[info['offset']:info['offset'] + 48] == note
imports = subprocess.check_output([nm, '-D', '--undefined-only', elf], text=True).split()
assert imports == ['U', 'badge_app_get_api'], imports
entry = struct.unpack_from('<I', payload, 24)[0]
text = entries['.text']
assert entry % 4 == 0 and text['address'] <= entry < text['address'] + text['size']
rounded_text = (text['size'] + 3) & ~3
mapped = {name: entries[name] for name in MAPPED if name in entries}
for name, s in mapped.items():
    if name == '.text' or not s['size']:
        continue
    assert s['address'] % 4 == 0
    assert not (text['address'] < s['address'] + s['size'] and s['address'] < text['address'] + rounded_text)
got = mapped.get('.got.loc')
span = (got['address'] + got['size'] - text['address'] + 3) & ~3 if got else rounded_text
allocation = span + sum(s['size'] + s['align'] - 1 for name, s in mapped.items()
                        if name not in ('.text', '.got.loc') and s['size'])
assert len(payload) <= 65536 and allocation <= 131072

manifest = dict(schema=1, id=APP_ID, version=version, target='esp32s3', abi=2, sdk='6.0.2',
                loader='2.1.0', bytes=len(payload), sha256=digest(elf))
manifest_text = json.dumps(manifest, sort_keys=True, indent=2) + '\n'
assert len(manifest_text.encode()) <= 512
(out / 'unsigned-manifest.json').write_text(manifest_text)
source_hashes = {str(f.relative_to(ROOT)): digest(f) for f in sorted(ROOT.rglob('*'))
                 if f.suffix in ('.c', '.h', '.inc', '.ld', '.py')}
(out / 'source-sha256.json').write_text(json.dumps(source_hashes, sort_keys=True, indent=2) + '\n')
report = dict(display_name='Hackaday', credits='Badge OS / Hackaday', manifest=manifest, elf=str(elf),
              entry=entry, imports=imports[1:], metadata_bytes=info['size'], metadata_alignment=info['align'],
              metadata_flags=info['flags'], loader_allocation_bound=allocation,
              loader_caps=dict(payload=65536, mapped=131072),
              mapped_sections={name: {k: s[k] for k in ('address', 'size', 'align')} for name, s in mapped.items()},
              source_checksum_file_sha256=digest(out / 'source-sha256.json'),
              compiler=subprocess.check_output([cc, '--version'], text=True).splitlines()[0])
(out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
