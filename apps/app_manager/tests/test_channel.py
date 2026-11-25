"""Generate public/local channel inputs and link the real config through a static archive."""
import os
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='badge-channel-') as name:
    tmp = Path(name);private = tmp / 'private.pem';public = tmp / 'public.pem';ca = tmp / 'ca.pem'
    subprocess.run(['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', str(private)], check=True, capture_output=True)
    subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)], check=True, capture_output=True)
    subprocess.run(['openssl', 'req', '-new', '-x509', '-key', str(private), '-subj', '/CN=channel.example', '-days', '1', '-out', str(ca)], check=True, capture_output=True)
    declarations = '\n'.join(line for line in (app / 'controller_target.c').read_text().splitlines() if 'const am_source_config_t am_' in line and '__attribute__((weak))' in line)
    weak = tmp / 'weak.c';weak.write_text('#include "package_source.h"\n' + declarations + '\n')
    source = tmp / 'check.c';source.write_text('''#include <assert.h>
#include <string.h>
#include "video_source.h"
int main(void){
const am_source_config_t *sources[]={&am_app_source_config,&am_rocks_source_config,am_video_source_config.code,
&am_codex_source_config,&am_hackaday_source_config,&am_messages_source_config,&am_doom_source_config};
for(unsigned i=0;i<7;i++){
assert(am_source_config_valid(sources[i]));assert(sources[i]->public_key==sources[0]->public_key);
}
assert(am_source_url_allowed(sources[2],am_video_source_config.receipt_url));
assert(am_source_url_allowed(sources[2],am_video_source_config.signature_url));
assert(am_source_url_allowed(sources[2],am_video_source_config.clip_url));
assert(strstr(am_video_source_config.clip_url,"/video/assets/clip.vid"));
return 0;}
''')
    flags = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined', '-I.', '-I' + str(tmp)]
    for base, extra in [('https://channel.example/apps', []), ('https://192.0.2.1:8443/apps', ['--ca', str(ca)])]:
        header = tmp / 'am_package_channel.h'
        subprocess.run(['python3', 'tools/configure_channel.py', '--base-url', base, '--public-key', str(public), '--output', str(header), *extra], cwd=app, check=True)
        assert b'PRIVATE KEY' not in header.read_bytes()
        obj = tmp / 'config.o';archive = tmp / 'channel.a';exe = tmp / 'check'
        subprocess.run(flags + ['-DAM_PACKAGE_CHANNEL_CONFIGURED=1', '-c', 'package_config.c', '-o', str(obj)], cwd=app, check=True)
        subprocess.run(['ar', 'rcs', str(archive), str(obj)], check=True, capture_output=True)
        subprocess.run(flags + [str(source), str(weak), str(archive), 'package_source.c', 'txn_json.c', 'txn_package.c', 'native_elf.c', '-o', str(exe)], cwd=app, check=True)
        subprocess.run([str(exe)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    for base, key in [('http://channel.example/apps', public), ('https://channel.example/apps', private)]:
        result = subprocess.run(['python3', 'tools/configure_channel.py', '--base-url', base, '--public-key', str(key), '--output', str(tmp / 'invalid.h')], cwd=app, capture_output=True)
        assert result.returncode and not (tmp / 'invalid.h').exists()
print('PASS seven-source public/local generator, real parser and static-archive linkage; private PEM/HTTP refused; no network')
