"""One signed Doom join with real engine/WAD; no network, target build or badge.

Usage: test_doom_pipeline.py ASSETS PUBLIC_PEM OWNER_SCRATCH WAD_Z
Reuses the owner's verified engine host objects and startup/save proof.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile
app=Path(__file__).resolve().parents[1];repo=app.parents[1]
assets,public,owner,wad=[Path(x).resolve() for x in sys.argv[1:5]]
assert hashlib.sha256(wad.read_bytes()).hexdigest()=='d37d78d341bad67886d26aa871cbd18a9cffc20d59e6e0bf034a5d0e8486ecab'
assert hashlib.sha256((assets/'module.elf').read_bytes()).hexdigest()=='36edd9861fecb859a1f6b455cad152b61847d3ec5f762f514596ade211143082'
objects=list(owner.glob('*.host.o'));assert len(objects)==79
prefix=Path(shutil.which('openssl')).resolve().parents[1]
flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DCONFIG_ELF_LOADER=1','-DCONFIG_ELF_LOADER_LOAD_PSRAM=1',
       '-Wall','-Wextra','-Werror','-pedantic','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
       '-Itests/doom_stubs','-Itests/native_host_stubs','-Itests/target_stubs','-Itests/stubs','-I.',
       '-I'+str(repo/'components/os/include'),'-I'+str(repo/'components/stb'),'-I'+str(repo/'components/doomgeneric')]
if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
libraries=['-lcrypto','-lm']
if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
sources=['native_host.c','native_codex.c','native_hackaday.c','native_messages.c','native_doom.c','native_doom_target.c',
         'controller.c','manager_catalogue.c','app_manager.c','package_source.c','txn_json.c','txn_package.c','txn_store.c','txn_vfs.c','native_elf.c',
         'host_c/io_posix.c','host_c/crypto_openssl.c','tests/native_host_double.c','tests/doom_module_host.c','tests/doom_pipeline_driver.c',
         'modules/doom/doom_save.c','modules/doom/doom_format.c',str(repo/'components/stb/stb_image.c')]
with tempfile.TemporaryDirectory(prefix='badge-doom-join-') as name:
    tmp=Path(name);root=tmp/'volume';root.mkdir();raw=tmp/'public.raw'
    der=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER']);assert der[-65]==4;raw.write_bytes(der[-65:])
    for i,source in enumerate(sources):
        options=['-Dbtn_held=nh_core_btn_held','-Desp_timer_get_time=nh_core_timer'] if source=='tests/native_host_double.c' else []
        if source in ['tests/doom_module_host.c','tests/doom_pipeline_driver.c','modules/doom/doom_save.c']:
            options+=['-Wno-strict-prototypes'] # Unchanged original engine headers.
        obj=tmp/f'{i}.o';subprocess.run(flags+options+['-c',source,'-o',str(obj)],cwd=app,check=True);objects.append(obj)
    binary=tmp/'test';subprocess.run(flags+list(map(str,objects))+libraries+['-o',str(binary)],cwd=app,check=True)
    protected=root/'doom/data';protected.mkdir(parents=True);(protected/'protected').write_text('keep app data')
    def files():return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
    def call(action,folder=assets,expected=0):
        try:
            p=subprocess.run([str(binary),str(root),str(public),str(folder),action,str(raw),str(wad)],capture_output=True,text=True,timeout=20,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        except subprocess.TimeoutExpired as error:
            raise AssertionError((error.stdout,error.stderr)) from error
        assert p.returncode==0,p.stdout+p.stderr
        r=json.loads(next(x[5:] for x in p.stdout.splitlines() if x.startswith('JOIN ')));assert r['result']==expected,r
        assert (protected/'protected').read_text()=='keep app data';return r
    installed=call('install');assert installed['installed'];before=files()
    assert call('update',expected=16)['installed'] and files()==before
    bad=tmp/'bad';shutil.copytree(assets,bad);data=bytearray((bad/'module.elf').read_bytes());data[100]^=1;(bad/'module.elf').write_bytes(data)
    assert call('update',bad,5)['installed'] and files()==before
    opened=call('open');assert opened['launched']==1 and opened['source_reads']>1 and opened['frames']>=10
    saved=(protected/'doomsav0.dsg').read_bytes();assert saved.startswith(b'LOCAL JOIN') and saved[-1]==0x1d
    assert not call('uninstall')['installed'] and (protected/'doomsav0.dsg').read_bytes()==saved
    print('PASS genuine signed Doom install/source/store/full preflight + leased relocate_source + real module/engine/WAD, frames, original controls/Home, scoped real save, cleanup, bad-hash and data preservation; SDK/MMU/ELF machine execution doubled.')
    print(json.dumps({'install':installed,'open':opened,'fixed_small_workspace':70280,'fixed_small_source_cache':69760,
                      'doom_acquisition_buffer':328676,'wad_runtime':4196020,'loader_bound':529227,'full_elf_execution_copy':0}))
