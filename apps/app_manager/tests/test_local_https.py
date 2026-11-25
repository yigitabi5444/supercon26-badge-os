"""Only scoped local HTTPS/source boundaries; no keys, network, build or device."""
import os
from pathlib import Path
import subprocess
import tempfile
import sys

app = Path(__file__).resolve().parents[1]
includes = (app / 'package_http.c').read_text().split('\n\n', 1)[0]
assert includes.index('#include "badge/badge.h"') > includes.index('#include "esp_timer.h"')
assert includes.index('#include "badge/badge.h"') > includes.index('#include "esp_http_client.h"')
with tempfile.TemporaryDirectory(prefix='badge-local-https-') as directory:
    for date_check in [False, True]:
        binary = Path(directory) / ('local-https-date-' + str(date_check))
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                   '-Dtime=am_test_time',
                   '-Wall', '-Wextra', '-Werror', '-pedantic', '-g', '-O1',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-Itests/local_https_stubs', '-Itests/fetch_stubs', '-Itests/target_stubs', '-Itests/stubs',
                   'package_source.c', 'package_config.c', 'package_http.c', 'txn_json.c', 'txn_package.c',
                   'tests/test_local_https.c', '-o', str(binary)]
        if date_check:
            command.insert(1, '-DCONFIG_MBEDTLS_HAVE_TIME_DATE=1')
        subprocess.run(command, cwd=app, check=True)
        subprocess.run([str(binary)]+(['--readiness'] if '--readiness' in sys.argv else []), check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
                            'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
