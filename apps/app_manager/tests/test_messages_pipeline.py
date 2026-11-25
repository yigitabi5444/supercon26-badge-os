"""One true signed Messages join; SDK/NVS/ELF execution doubled, no network/device.

Usage: test_messages_pipeline.py MESSAGES_ASSETS PUBLIC_PEM
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile
app=Path(__file__).resolve().parents[1];repo=app.parents[1]
assets,public=[Path(p).resolve() for p in sys.argv[1:3]]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DCONFIG_ELF_LOADER=1','-DCONFIG_ELF_LOADER_LOAD_PSRAM=1',
       '-Wall','-Wextra','-Werror','-pedantic','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
       '-Itests/msg_stubs','-Itests/native_host_stubs','-Itests/stubs','-I.',
       '-Imodules/messaging/include','-Imodules/messaging/vendor','-I'+str(repo/'components/os/include'),'-I'+str(repo/'components/os/messages')]
if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
libraries=['-lcrypto','-lm']
if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
sources=['native_host.c','native_codex.c','native_hackaday.c','native_messages.c','native_doom.c','native_messages_target.c',
         'controller.c','manager_catalogue.c','app_manager.c','package_source.c','txn_json.c','txn_package.c','txn_store.c','txn_vfs.c','native_elf.c',
         'host_c/io_posix.c','host_c/crypto_openssl.c','tests/native_host_double.c','tests/messages_module_host.c',
         'tests/messages_portal_host.c','tests/messages_sdk_double.c','tests/messages_pipeline_driver.c',
         'modules/messaging/src/controller.c','modules/messaging/src/render.c','modules/messaging/src/private_memory.c',
         'modules/messaging/vendor/qrcodegen.c',str(repo/'components/os/messages/message_store.c')]
with tempfile.TemporaryDirectory(prefix='badge-messages-join-') as name:
    tmp=Path(name);root=tmp/'volume';root.mkdir();shared=tmp/'resident-messages.dat';raw=tmp/'public.raw';objects=[]
    der=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER']);assert der[-65]==4;raw.write_bytes(der[-65:])
    for i,source in enumerate(sources):
        options=[];includes=[]
        if source=='tests/messages_module_host.c' or source.startswith('modules/messaging/'):
            includes+=['-Imodules/messaging/include']
        if source=='tests/native_host_double.c':options+=['-Dbtn_get=nh_core_btn_get']
        if source=='tests/messages_portal_host.c':options+=['-Dtime=messages_test_time','-Wno-unused-parameter','-Wno-sign-compare','-Wno-overlength-strings'] # unchanged original source, existing SDK warning profile
        if source.endswith('vendor/qrcodegen.c'):options+=['-DNDEBUG','-Wno-unused-parameter','-Dstrlen=msg_strlen','-Dstrchr=msg_strchr','-Dmemcpy=msg_memcpy','-Dmemset=msg_memset','-Dmemmove=msg_memmove','-Dabs=msg_abs','-Dlabs=msg_labs']
        obj=tmp/f'{i}.o';subprocess.run(flags[:1]+includes+flags[1:]+options+['-c',source,'-o',str(obj)],cwd=app,check=True);objects.append(str(obj))
    binary=tmp/'test';subprocess.run(flags+objects+libraries+['-o',str(binary)],cwd=app,check=True)
    protected=root/'messaging/data';protected.mkdir(parents=True);(protected/'protected').write_text('keep app data')
    def files(folder):return {str(p.relative_to(folder)):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*') if p.is_file()}
    def call(action,mode='unavailable',expected=0):
        p=subprocess.run([str(binary),str(root),str(public),str(raw),str(assets),action,mode,str(shared),'messaging'],
            capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert p.returncode==0,p.stdout+p.stderr
        r=json.loads(p.stdout);assert r['result']==expected,r;assert (protected/'protected').read_text()=='keep app data';return r
    assert call('install')['installed'];before=files(root);stored=shared.read_bytes()
    assert call('update',expected=16)['installed'] and files(root)==before and shared.read_bytes()==stored
    assert call('open')['launched']==1 and files(root)==before and shared.read_bytes()==stored
    for mode in ['phone','failure']:
        r=call('open',mode,9);assert r['reset'] and r['pinned'] and files(root)==before
    stored=shared.read_bytes();assert not call('uninstall')['installed'] and shared.read_bytes()==stored
    print('PASS genuine Messages signed install/source/store/loader + original module/store/host portal: copied snapshots/read/control/QR/Info, safe unavailable B, phone/reply/menu reuse and authenticated host page/state, partial setup retains ELF/copy/lease until simulated Home reset, app/shared data preserved; SDK/NVS/ELF execution doubled.')
