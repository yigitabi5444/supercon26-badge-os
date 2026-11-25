"""Usage: test_had_pipeline.py HACKADAY_ASSETS PUBLIC_PEM SPACEINV_ASSETS.

Genuine signed module, production source/store/launcher and host jobs. SDK/ELF
execution doubled, no listener, target build, live network, device or flash.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile
import test_had_runtime as had
app=had.app;code,public,spaceinv=[Path(p).resolve() for p in sys.argv[1:4]]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
if (prefix/'include/openssl/evp.h').exists():had.flags+=['-I',str(prefix/'include')]
had.flags+=['-DCONFIG_ELF_LOADER=1','-DCONFIG_ELF_LOADER_LOAD_PSRAM=1']
libraries=['-lcrypto','-lm']
if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
with tempfile.TemporaryDirectory(prefix='badge-had-pipeline-') as name:
    tmp=Path(name);root=tmp/'volume';root.mkdir();raw=tmp/'public.raw';inputs=had.fixtures(tmp)
    der=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER']);assert der[-65]==4;raw.write_bytes(der[-65:])
    extra=['controller.c','manager_catalogue.c','app_manager.c','txn_store.c','txn_vfs.c',
           'host_c/io_posix.c','host_c/crypto_openssl.c','tests/had_pipeline_driver.c']
    binary=tmp/'test';subprocess.run(had.flags+had.compile_objects(tmp,extra)+libraries+['-o',str(binary)],cwd=app,check=True)
    for ident in ['hackaday','aliens']:
        folder=root/ident/'data';folder.mkdir(parents=True);(folder/'protected').write_text('keep '+ident)
    def snapshot(folder):
        return {str(p.relative_to(folder)):(p.stat().st_size,hashlib.sha256(p.read_bytes()).hexdigest())
                for p in folder.rglob('*') if p.is_file()}
    def call(action,assets=code,ident='hackaday',mode='normal',expected=0):
        p=subprocess.run([str(binary),str(root),str(public),str(raw),str(assets),action,ident,mode,*map(str,inputs)],
            capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert p.returncode==0,p.stdout+p.stderr
        result=json.loads(p.stdout);assert result['result']==expected,result
        for name in ['hackaday','aliens']:assert (root/name/'data/protected').read_text()=='keep '+name
        return result
    state=call('install');assert state['installed'] and state['payload_reads']==1
    before=snapshot(root);state=call('update',expected=16);assert state['installed'] and snapshot(root)==before
    state=call('open');assert state['launched']==1 and not state['fetches'] and snapshot(root)==before
    state=call('open',mode='busy',expected=9);assert state['retained'] and snapshot(root)==before
    state=call('update',assets=spaceinv,expected=1);assert not state['payload_reads'] and snapshot(root)==before
    call('install',assets=spaceinv,ident='aliens');other=snapshot(root/'aliens');own=snapshot(root/'hackaday')
    state=call('update',ident='aliens',expected=1);assert not state['payload_reads'] and snapshot(root/'aliens')==other and snapshot(root/'hackaday')==own
    state=call('uninstall');assert not state['installed'] and snapshot(root/'aliens')==other
    print('PASS genuine Hackaday1.0.0 signed install/same-version/source isolation, independent-process Open through exact module+HTTP/gzip/image host, cancel retains ELF/copy/lease until worker drains, namespace/data isolation and uninstall; SDK/ELF execution doubled.')
