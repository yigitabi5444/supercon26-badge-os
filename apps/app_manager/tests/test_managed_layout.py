"""Check both exact managed sectors and an unknown sector against the actual policy."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('old_sector', type=Path)
parser.add_argument('fresh_sector', type=Path)
args = parser.parse_args()
app = Path(__file__).resolve().parents[1]
expected = ['1541619f55198d62ffc47ab8afa1e40f9634f88aa293cbb0a677ebeb25511bb8',
            '56f1798931b20c4f3d4f7f218c9d41319068167718c2586a129a7d336c6262e0']

def entries(sector):
    result = []
    for off in range(0, 3072, 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', sector, off)
        if magic != 0x50aa:
            break
        assert flags == 0
        result.append((address, size, kind, subtype, label.rstrip(b'\0').decode()))
    return result

sectors = [path.read_bytes() for path in (args.old_sector, args.fresh_sector)]
assert all(len(data) == 4096 for data in sectors)
assert [hashlib.sha256(data).hexdigest() for data in sectors] == expected
tables = [entries(data) for data in sectors]
assert len(tables[0]) == 6 and len(tables[1]) == 8 and tables[0] == tables[1][:6]
unknown = bytearray(sectors[0]);unknown[-1] ^= 1
array = lambda data: ','.join(str(x) for x in data)
lines = ['#include <assert.h>', '#include <string.h>', '#include "controller_target.h"', 'int main(void){']
for i, table in enumerate(tables):
    fields = ['{.address=%d,.size=%d,.type=%d,.subtype=%d,.label="%s"}' % row for row in table]
    lines += ['am_partition_view_t p%d[]={%s};' % (i, ','.join(fields)),
              'uint8_t d%d[]={%s};' % (i, array(hashlib.sha256(sectors[i]).digest())),
              'assert(am_geometry_check(&am_app_layout_policy,p%d,%d,0x800000,d%d)==AM_OK);' % (i, len(table), i),
              'assert(am_geometry_check(&am_app_layout_policy,p%d,%d,0x400000,d%d)==AM_DISABLED);' % (i, len(table), i)]
lines += ['uint8_t unknown[]={%s};' % array(hashlib.sha256(unknown).digest()),
          'assert(am_geometry_check(&am_app_layout_policy,p0,6,0x800000,unknown)==AM_DISABLED);',
          'p0[%d].readonly=true;assert(am_geometry_check(&am_app_layout_policy,p0,6,0x800000,d0)==AM_DISABLED);' % next(i for i, row in enumerate(tables[0]) if row[4]=='appstore'),
          'return 0;}']
with tempfile.TemporaryDirectory(prefix='badge-managed-layout-') as name:
    tmp = Path(name);source = tmp / 'check.c';source.write_text('\n'.join(lines))
    exe = tmp / 'check'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
                    '-DCONFIG_BADGE_APP_MANAGER_FRESH_LAYOUT=1', '-Itests/target_stubs', '-I.',
                    'txn_geometry.c', 'target_profile.c', str(source), '-o', str(exe)], cwd=app, check=True)
    subprocess.run([str(exe)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
print('PASS actual managed sectors: old/fresh accepted; unknown, wrong flash and read-only appstore rejected; no flash I/O')
