"""Only physical-size/default guard cases plus an old-getter negative control; no broad suite."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', required=True)
args = parser.parse_args()
out = Path(args.output).resolve()
out.mkdir(exist_ok=False)
app = Path(__file__).resolve().parents[1]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
flags = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Itests/target_stubs', '-I.']
source = app / 'txn_littlefs.c'
current = source.read_text()
assert current.count('esp_flash_get_physical_size(NULL,&flash)') == 1
negative = out / 'old-configured-getter.c'
negative.write_text(current.replace('esp_flash_get_physical_size(NULL,&flash)', 'esp_flash_get_size(NULL,&flash)'))
checks = {}
for name, adapter in [('physical', source), ('old-configured-negative', negative)]:
    binary = out / name
    command = flags + ['txn_json.c', 'txn_geometry.c', str(adapter),
                       'tests/test_physical_flash_guard.c', '-o', str(binary)]
    compiled = subprocess.run(command, cwd=app, capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(compiled.stdout + compiled.stderr)
    assert compiled.returncode == 0 and 'warning:' not in compiled.stderr
    result = subprocess.run([str(binary)], cwd=app, capture_output=True, text=True)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    checks[name] = dict(compile_command=command, compile_exit=compiled.returncode, run_exit=result.returncode)
    if name == 'physical':
        assert result.returncode == 0
    else:
        assert result.returncode != 0 and 'AM_DISABLED' in result.stderr
    binary.unlink()  # No test executable or module binary in the frozen evidence.
record = dict(status='PASS focused physical-size guard cases; old configured getter fails negative control',
              source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), checks=checks,
              scope='Actual app adapter/geometry with SDK doubles; no real flash reads/mount/write or target firmware build')
(out / 'checks.json').write_text(json.dumps(record, indent=2) + '\n')
print(record['status'])
