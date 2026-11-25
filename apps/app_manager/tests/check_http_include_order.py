"""Actual target header order only; reuse a supplied compile database without building/writing objects."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--compile-commands', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
database = Path(args.compile_commands).resolve()
out = Path(args.output).resolve()
out.mkdir(exist_ok=False)
app = Path(__file__).resolve().parents[1]
source = app / 'package_http.c'
entries = [x for x in json.loads(database.read_text())
           if x['file'].endswith('/apps/app_manager/package_http.c')]
assert len(entries) == 1, 'Need the exact App Manager HTTP compile entry'
entry = entries[0]
original = entry.get('arguments') or shlex.split(entry['command'])
expanded, response_files = [], {}
for word in original:
    if word.startswith('@'):
        path = Path(word[1:])
        if not path.is_absolute():
            path = Path(entry['directory']) / path
        response_files[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        expanded += shlex.split(path.read_text())
    else:
        expanded.append(word)
# Drop object/dependency outputs. Retain actual defines, SDK/config include paths
# and inherited flags; -fsyntax-only emits neither a firmware object nor a link.
command, i = [], 0
while i < len(expanded):
    word = expanded[i]
    if word in ['-o', '-MF', '-MT', '-MQ']:
        i += 2
        continue
    if word in ['-c', '-MD', '-MMD', '-MP', '-fstack-usage'] or word == entry['file']:
        i += 1
        continue
    command.append(word)
    i += 1
assert not any(word.startswith('-o') and word != '-Og' for word in command)
command += ['-I', str(app), '-fsyntax-only', '-H', '-fdiagnostics-color=never']
includes = source.read_text().split('\n\n', 1)[0]
badge = '#include "badge/badge.h"'
assert includes.count(badge) == 1
assert includes.index(badge) > includes.index('#include "esp_timer.h"')
negative = includes.replace(badge + '\n', '')
negative = negative.replace('#include "esp_http_client.h"', badge + '\n#include "esp_http_client.h"')
suffix = '\n_Static_assert(H == 672, "Actual Badge OS display macro required");\n'
results = {}
for name, prefix in [('fixed', includes), ('old-order-negative', negative)]:
    probe = out / (name + '.c')
    probe.write_text(prefix + suffix)
    invocation = command + [str(probe)]
    result = subprocess.run(invocation, cwd=entry['directory'], capture_output=True, text=True)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    paths = sorted(set(re.findall(r'^\.+ (/[^\n]+)', result.stderr, re.MULTILINE)))
    wanted = ['badge/badge.h', 'badge/display.h', 'mbedtls/private/gcm.h']
    actual = {}
    for ending in wanted:
        matches = [Path(p) for p in paths if p.endswith('/' + ending)]
        assert len(matches) == 1 and '/tests/' not in str(matches[0]), ending
        actual[ending] = {'path': str(matches[0]),
                          'sha256': hashlib.sha256(matches[0].read_bytes()).hexdigest()}
    results[name] = {'exit_code': result.returncode, 'command': invocation, 'actual_headers': actual}
    if name == 'fixed':
        assert result.returncode == 0 and not re.search(r'\bwarning:', result.stderr)
    else:
        assert result.returncode != 0 and 'gcm.h:64:' in result.stderr
        assert "macro 'H'" in result.stderr or 'macro ‘H’' in result.stderr
record = dict(status='PASS actual SDK + actual Badge OS header order; old order fails as negative control',
              source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              compile_database_sha256=hashlib.sha256(database.read_bytes()).hexdigest(),
              response_files=response_files, cwd=entry['directory'], checks=results,
              scope='Include-only syntax probes using actual target compiler/config/headers. No object, dependency, firmware build/link, SDK/OS/flag mutation or device action.')
(out / 'checks.json').write_text(json.dumps(record, indent=2) + '\n')
print(record['status'])
