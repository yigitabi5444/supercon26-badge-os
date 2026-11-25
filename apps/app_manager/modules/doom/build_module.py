"""Build the unsigned Doom module alone, using the pinned clean-pilot compile inputs."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

root = Path(__file__).resolve().parent
version = re.search(r'#define DOOM_APP_VERSION "([^"]+)"', (root / 'doom_module.c').read_text())[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pilot', required=True, type=Path, help='Existing SDK build with original Doom compile_commands.json')
parser.add_argument('--toolroot', required=True, type=Path, help='Pinned esp-15.2.0_20251204 xtensa-esp-elf root')
parser.add_argument('--output', type=Path, default=root / 'build', help='Standalone module output directory')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
repo = root.parents[3]
doom = repo / 'components/doomgeneric'
native = root.parents[1]
pilot = args.pilot.resolve()
toolroot = args.toolroot.resolve()
prefix = toolroot / 'bin/xtensa-esp32s3-elf'
lib = toolroot / 'picolibc/xtensa-esp-elf/lib/esp32s3/no-rtti'
gcc, ld, objcopy, strip, nm = [f'{prefix}-{name}' for name in ('gcc', 'ld', 'objcopy', 'strip', 'nm')]


def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)


def source_inputs():
    commands = [x for x in json.loads((pilot / 'compile_commands.json').read_text()) if '/components/doomgeneric/' in x['file']]
    assert len(commands) == 79
    for item in commands:
        old = Path(item['file'])
        assert old.read_bytes() == (doom / old.name).read_bytes(), old
    adapter = (repo / 'apps/doom/doom.c').read_text()
    assert 'CHORD_MS 120' in adapter and 'KNOB_DIR 1' in adapter
    start = 'static int keyq[16]'
    text = adapter.split(start, 1)[1].split('// ---- Sound:', 1)[0]
    assert (root / 'original_controls.inc').read_text() == start + text, 'Original controls changed'
    return commands


def compile_input(item, source, engine=False):
    obj = out / (source.stem + '.o')
    if source.name == 'info.c':
        text = source.read_text()
        for name in ('doom_states', 'doom_mobjinfo'):
            old = f'section(".rodata.{name}")'
            assert text.count(old) == 1
            text = text.replace(old, f'section(".data.{name}")')
        source = out / 'info.c'
        source.write_text(text)
    args = shlex.split(item['command'])
    assert args[0] == gcc
    args[args.index('-o') + 1] = str(obj)
    args[args.index('-c') + 1] = str(source)
    args += ['-Os', '-g0', '-fPIC', '-fvisibility=hidden', '-mno-target-align', '-fno-builtin',
             f'-ffile-prefix-map={repo}/=', f'-fmacro-prefix-map={repo}/=',
             f'-ffile-prefix-map={out}/=components/doomgeneric/', f'-fmacro-prefix-map={out}/=components/doomgeneric/',
             '-iquote', doom, '-I', root, '-I', native, '-include', root / 'local_errno.h']
    if engine:
        args += ['-DDOOM_ENGINE_INTERCEPT', '-include', root / 'doom_glue.h']
    run(*args, cwd=item['directory'])
    return obj


commands = source_inputs()
with ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(lambda item: compile_input(item, doom / Path(item['file']).name, True), commands))
for name in ('doom_module', 'doom_save', 'doom_console', 'doom_format', 'atoi_local'):
    objects.append(compile_input(commands[0], root / f'{name}.c'))

verbose = subprocess.check_output([ld, '-m', 'elf32xtensa', '-shared', '--verbose'], text=True)
linker = verbose.split('==================================================\n')[1]
got = '  .got.loc        : { *(.got.loc) }\n'
assert linker.count(got) == 1 and linker.count('  .rodata         :') == 1
assert linker.count('    SORT(CONSTRUCTORS)\n') == 1
linker_script = out / 'doom-layout.xs'
linker_script.write_text(linker.replace(got, '').replace('  .rodata         :', got + '  .rodata         :')
                         .replace('    SORT(CONSTRUCTORS)\n', '    SORT(CONSTRUCTORS)\n    . = ALIGN(8);\n'))
libgcc = subprocess.check_output([gcc, '-print-libgcc-file-name'], text=True).strip()
raw = out / 'doom-link.elf'
run(gcc, '-nostartfiles', '-nostdlib', '-fPIC', '-shared', '-Wl,--gc-sections,-Bsymbolic,--exclude-libs,ALL',
    '-Wl,-Map=' + str(out / 'doom-link.map'), '-T', linker_script, '-e', 'app_main', *objects,
    '-Wl,--start-group', lib / 'libc.a', lib / 'libm.a', libgcc, '-Wl,--end-group', '-o', raw)
assert subprocess.check_output([nm, '-D', '-u', raw], text=True).split() == ['U', 'badge_app_get_api']

elf = out / f'doom-{version}.app.elf'
metadata = out / 'module-info.bin'
run(objcopy, '--dump-section', f'.badge_app={metadata}', out / 'doom_module.o')
assert metadata.read_bytes() == struct.pack('<II16s24s', 0x32504142, 2, b'doom', version.encode())
run(strip, '--strip-unneeded', '--remove-section=.comment', '--remove-section=.dynamic',
    '--remove-section=.eh_frame', '--remove-section=.xt.lit', '--remove-section=.xt.prop',
    '--remove-section=.xtensa.info', '-o', elf, raw)
run(objcopy, '--add-section', f'.badge_app={metadata}', elf)
run(objcopy, '--set-section-flags', '.badge_app=readonly,contents', '--set-section-alignment', '.badge_app=4', elf)

bits = bytearray(elf.read_bytes())
shoff = struct.unpack_from('<I', bits, 32)[0]
stride, count, strings = struct.unpack_from('<HHH', bits, 46)
assert stride == 40
headers = [struct.unpack_from('<10I', bits, shoff + i * stride) for i in range(count)]
name_off, name_size = headers[strings][4:6]
names = bits[name_off:name_off + name_size]
sections = {names[s[0]:].split(b'\0', 1)[0].decode(): (i, s) for i, s in enumerate(headers)}
index, relas = sections['.rela.dyn']
off, original = relas[4:6]
assert relas[1] == 4 and relas[9] == 12 and original % 12 == 0
# GNU ld reserves a single all-zero R_XTENSA_NONE record. Drop it while preserving all
# active records' bytes and order, and keep the original section span as file padding.
records = [bits[pos:pos + 12] for pos in range(off, off + original, 12)]
assert records.count(bytes(12)) <= 1
active = b''.join(rec for rec in records if rec != bytes(12))
used = len(active)
bits[off:off + original] = active + bytes(original - used)
struct.pack_into('<I', bits, shoff + index * stride + 20, used)
elf.write_bytes(bits)
assert not any(prefix in bits for prefix in (b'/Users/', b'/home/')), 'Personal build path in module'
manifest = dict(schema=1, id='doom', version=version, target='esp32s3', abi=2,
                sdk='6.0.2', loader='2.1.0', bytes=len(bits), sha256=hashlib.sha256(bits).hexdigest())
(out / 'unsigned-manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
print(f'{elf}: {len(bits)} bytes; sha256 {hashlib.sha256(bits).hexdigest()}; sole import badge_app_get_api')
