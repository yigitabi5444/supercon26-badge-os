"""Actual SDK public HTTP/cert/timer declarations, with dependency types stubbed; not a target build."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

app = Path(__file__).resolve().parents[1]
idf = Path(sys.argv[1]).resolve() / 'components'
headers = ['esp_http_client/include/esp_http_client.h', 'mbedtls/esp_crt_bundle/include/esp_crt_bundle.h',
           'esp_timer/include/esp_timer.h']
with tempfile.TemporaryDirectory(prefix='badge-http-public-') as name:
    stub = Path(name)
    (stub / 'mbedtls').mkdir()
    (stub / 'esp_event.h').write_text('#pragma once\n#define ESP_EVENT_DECLARE_BASE(x) extern const char *x\n')
    (stub / 'esp_etm.h').write_text('#pragma once\ntypedef struct event *esp_etm_event_handle_t;\n')
    (stub / 'mbedtls/ssl.h').write_text('#pragma once\ntypedef struct ssl mbedtls_ssl_config;\n'
                                      'typedef struct crt mbedtls_x509_crt;\n')
    command = ['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror', '-fsyntax-only']
    for header in headers:
        command += ['-I', str((idf / header).parent)]
    command += ['-I', str(idf / 'esp_common/include'), '-I', str(stub),
                '-Itests/fetch_stubs', '-Itests/target_stubs', '-Itests/stubs', 'package_http.c']
    subprocess.run(command, cwd=app, check=True)
print(json.dumps({'status': 'PASS SDK6.0.2 public HTTP/certificate/timer header syntax only; dependency types stubbed',
                  'headers': {p: hashlib.sha256((idf / p).read_bytes()).hexdigest() for p in headers}}, indent=2))
