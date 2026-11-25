"""Audit the produced native ELF layout without relaxing current firmware limits."""
from collections import Counter
import argparse
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf', type=Path)
parser.add_argument('--version', default='1.0.1', help='Expected package version')
parser.add_argument('--output', type=Path, help='Optional audit JSON output')
args = parser.parse_args()
path = args.elf.resolve()
elf = path.read_bytes()
assert elf[:16] == b'\x7fELF\x01\x01\x01' + bytes(9)
assert struct.unpack_from('<HHI', elf, 16) == (3, 94, 1)
entry, phoff, shoff, flags = struct.unpack_from('<IIII', elf, 24)
ehsize, phsize, phnum, shsize, shnum, shstr = struct.unpack_from('<6H', elf, 40)
assert (ehsize, phsize, shsize, flags) == (52, 32, 40, 0x300)
phdr = [struct.unpack_from('<8I', elf, phoff + i * phsize) for i in range(phnum)]
assert [(p[0], p[6]) for p in phdr] == [(1, 5), (1, 6), (2, 6)]
for kind, off, addr, _, filesz, memsz, _, alignment in phdr:
    assert filesz <= memsz and off + filesz <= len(elf) and alignment & (alignment - 1) == 0
    assert (kind == 1 and (addr - off) % alignment == 0) or (kind == 2 and filesz == memsz == 0)

headers = [struct.unpack_from('<10I', elf, shoff + i * shsize) for i in range(shnum)]
names_at, names_n = headers[shstr][4:6]
names = elf[names_at:names_at + names_n]
sections = {names[s[0]:].split(b'\0', 1)[0].decode(): s for s in headers[1:]}
mapped = {key: sections[key] for key in ('.text', '.got.loc', '.rodata', '.data.rel.ro', '.data', '.bss')}
for s in headers:
    _, kind, flags, addr, off, count, _, _, align, _ = s
    assert not flags & 1024 and flags & 5 != 5
    assert off <= len(elf) and (kind == 8 or off + count <= len(elf))
    assert not align or off % align == 0 and (not flags & 2 or addr % align == 0)
for name, s in mapped.items():
    assert s[2] == (6 if name == '.text' else 2 if name in ('.rodata', '.got.loc') else 3)
    assert s[1] == (8 if name == '.bss' else 1)
    assert s[8] >= 4
    assert not any(other[3] < s[3] + s[5] and s[3] < other[3] + other[5]
                   for n, other in mapped.items() if n != name)
base = mapped['.text'][3]
assert entry % 4 == 0 and base <= entry < base + mapped['.text'][5]
info = sections['.badge_app']
assert (info[1], info[2], info[5], info[8]) == (1, 0, 48, 4)
assert elf[info[4]:info[4] + 48] == struct.pack('<II16s24s', 0x32504142, 2, b'doom', args.version.encode())
assert not any(prefix in elf for prefix in (b'/Users/', b'/home/')), 'Personal build path in module'

dyn = sections['.dynsym']
assert dyn[9] == 16 and dyn[5] % 16 == 0
dynstr = sections['.dynstr']
strings = elf[dynstr[4]:dynstr[4] + dynstr[5]]
imports = {}
for i in range(1, dyn[5] // 16):
    name, _, _, _, _, section = struct.unpack_from('<IIIBBH', elf, dyn[4] + 16 * i)
    if not section:
        imports[i] = strings[name:].split(b'\0', 1)[0].decode()
assert list(imports.values()) == ['badge_app_get_api']


def mapped_at(addr, length):
    for key, (_, _, _, va, off, size, *_) in mapped.items():
        if va <= addr and addr + length <= va + size:
            return key, off + addr - va
    return None, None


counts = Counter()
offsets = set()
for name in ('.rela.dyn', '.rela.plt'):
    table = sections[name]
    assert table[9] == 12 and table[5] % 12 == 0
    for i in range(table[5] // 12):
        addr, info, addend = struct.unpack_from('<III', elf, table[4] + i * 12)
        which, file = mapped_at(addr, 4)
        assert which and which != '.bss' and addr % 4 == 0 and addr not in offsets
        offsets.add(addr)
        kind, symbol = info & 255, info >> 8
        counts[kind] += 1
        word = struct.unpack_from('<I', elf, file)[0]
        if kind == 5:
            assert symbol == addend == 0 and mapped_at(word, 1)[0], (name, i, addr, word, addend)
        elif kind == 2:
            assert which == '.text' and symbol == word == 0 and addend in (1, 2) and addr == base + (addend - 1) * 4
        else:
            assert kind == 4 and addend == 0 and symbol in imports
assert counts[2] == 2 and counts[4] == 1

got = mapped['.got.loc']
span = (got[3] + got[5] - base + 3) & ~3
assert got[3] >= base + mapped['.text'][5] and got[5] % 4 == 0
memory = span + sum(s[5] + s[8] - 1 for name, s in mapped.items() if name not in ('.text', '.got.loc'))
result = dict(elf=str(path), sha256=hashlib.sha256(elf).hexdigest(), file_bytes=len(elf),
              mapped_loader_policy_bytes=memory, relocation_count=len(offsets), relocation_types=dict(counts),
              imports=list(imports.values()), entry_point=entry, no_tls_or_wx=True,
              abi_header_sha256=hashlib.sha256((root / 'badge_doom_api.h').read_bytes()).hexdigest(),
              exceeds_current_limits_by=dict(elf_bytes=len(elf) - 65536, mapped_bytes=memory - 131072,
                                              relocations=len(offsets) - 2048),
              scope='Unsigned native ELF layout audit; no execution, WAD, save or device test.')
if args.output:
    args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
