"""Build only the unsigned standalone Video ELF against the reviewed host header."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).parent.resolve()
VERSION = '1.0.0'
parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True, type=Path)
parser.add_argument('--api-dir', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
cc = args.cc.resolve()
objcopy = cc.with_name(cc.name.replace('gcc', 'objcopy'))
strip = cc.with_name(cc.name.replace('gcc', 'strip'))
nm = cc.with_name(cc.name.replace('gcc', 'nm'))
header_hashes = {
    'badge_video_api.h': 'd88afeecdd7f8498889321f798ad9d20fc6dc2a544d233decc4ac05f0e8d62a0',
    'badge_native_api.h': '3c26055cc13984c6bd1aca086df442c74151509e7ae9334fbc41852522833e86',
}
for name, digest in header_hashes.items():
    assert hashlib.sha256((args.api_dir / name).read_bytes()).hexdigest() == digest, name


def run(*cmd):
    subprocess.run(cmd, check=True)


objects = []
for name in ('video_module', 'vid2', 'video_playback'):
    obj = out / f'{name}.o'
    run(cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Os', '-fno-builtin', '-fPIC',
        '-mlongcalls', '-mno-target-align', '-ffunction-sections', '-fdata-sections',
        '-fvisibility=hidden', '-I', args.api_dir, '-c', ROOT / f'{name}.c', '-o', obj)
    objects.append(obj)

elf = out / f'video-{VERSION}.app.elf'
metadata = out / 'module-info.bin'
run(objcopy, '--dump-section', f'.badge_app={metadata}', objects[0])
assert metadata.read_bytes() == struct.pack('<II16s24s', 0x32504142, 2, b'video', VERSION.encode())
run(cc, '-nostartfiles', '-nostdlib', '-fPIC', '-shared', '-e', 'app_main',
    '-Wl,--gc-sections,--strip-all,--strip-debug,--strip-discarded', *objects, '-o', elf)
run(objcopy, '--add-section', f'.badge_app={metadata}', elf)
run(objcopy, '--set-section-flags', '.badge_app=readonly,contents',
    '--set-section-alignment', '.badge_app=4', elf)
run(strip, '--strip-unneeded', '--remove-section=.comment', '--remove-section=.dynamic',
    '--remove-section=.xt.lit', '--remove-section=.xt.prop', '--remove-section=.xtensa.info', elf)
imports = subprocess.check_output([nm, '-D', '--undefined-only', elf], text=True).split()
assert imports == ['U', 'badge_app_get_api'], imports
payload = elf.read_bytes()
assert len(payload) <= 8192, len(payload)
manifest = dict(schema=1, id='video', version=VERSION, target='esp32s3', abi=2, sdk='6.0.2',
                loader='2.1.0', bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
(out / 'unsigned-manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
print(f'{elf}: {len(payload)} bytes; sole import badge_app_get_api; {manifest["sha256"]}')
