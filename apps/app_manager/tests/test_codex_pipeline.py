"""Usage: test_codex_pipeline.py CODEX_ASSETS PUBLIC_PEM SPACEINV_ASSETS.

Real signed code, source, filesystem transaction, launcher, module and target I/O.
SDK I/O and ELF execution are doubled; no network/device/target build.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile
import test_codex_runtime as cb
app=cb.app;code,public,spaceinv=[Path(p).resolve() for p in sys.argv[1:4]]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
if (prefix/'include/openssl/evp.h').exists():cb.flags+=['-I',str(prefix/'include')]
cb.flags+=['-DCONFIG_ELF_LOADER=1','-DCONFIG_ELF_LOADER_LOAD_PSRAM=1']
libraries=['-lcrypto','-lm']
if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
with tempfile.TemporaryDirectory(prefix='badge-codex-pipeline-') as name:
    tmp=Path(name);root=tmp/'volume';root.mkdir();raw=tmp/'public.raw'
    der=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER']);assert der[-65]==4;raw.write_bytes(der[-65:])
    extra=['controller.c','manager_catalogue.c','app_manager.c','package_source.c','txn_json.c','txn_package.c',
           'txn_store.c','txn_vfs.c','native_elf.c','host_c/io_posix.c','host_c/crypto_openssl.c','tests/codex_pipeline_driver.c']
    binary=tmp/'test';subprocess.run(cb.flags+cb.compile_objects(tmp,extra)+libraries+['-o',str(binary)],cwd=app,check=True)
    for ident in ['codex_board','aliens']:
        folder=root/ident/'data';folder.mkdir(parents=True);(folder/'protected').write_text('keep '+ident)
    def snapshot(folder):
        return {str(p.relative_to(folder)):(p.stat().st_size,hashlib.sha256(p.read_bytes()).hexdigest())
                for p in folder.rglob('*') if p.is_file()}
    def call(action,assets=code,ident='codex_board',transport='USB',expected=0):
        p=subprocess.run([str(binary),str(root),str(public),str(raw),str(assets),action,ident,transport],
            capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert p.returncode==0,p.stdout+p.stderr
        result=json.loads(p.stdout);assert result['result']==expected,result
        for name in ['codex_board','aliens']:assert (root/name/'data/protected').read_text()=='keep '+name
        return result
    state=call('install');assert state['installed'] and state['payload_reads']==1
    before=snapshot(root);state=call('update',expected=16);assert state['installed'] and snapshot(root)==before
    for transport in ['USB','LAN']:
        state=call('open',transport=transport);assert state['launched']==state['loaders']==1 and not state['fetches'] and snapshot(root)==before
    state=call('update',assets=spaceinv,expected=1);assert not state['payload_reads'] and snapshot(root)==before
    call('install',assets=spaceinv,ident='aliens');other=snapshot(root/'aliens');own=snapshot(root/'codex_board')
    state=call('update',assets=code,ident='aliens',expected=1);assert not state['payload_reads'] and snapshot(root/'aliens')==other and snapshot(root/'codex_board')==own
    state=call('uninstall');assert not state['installed'] and snapshot(root/'aliens')==other
    print('PASS genuine Codex Board1.0.0 signed source/install/same-version/update, independent-process offline USB/LAN Open through exact module+real host, cross-ID rejection before payload, namespace/data isolation and uninstall; SDK/ELF execution doubled.')
