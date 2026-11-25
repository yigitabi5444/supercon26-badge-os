"""One offline installed-Home proof, using actual trust/store/target/normal Open.

Usage: test_home_apps.py SPACEINV_ASSETS PUBLIC_PEM
SDK mount/flash/loader are doubled; no shared Home, network, build or device.
"""
from pathlib import Path
import hashlib,os,shutil,subprocess,sys,tempfile
app=Path(__file__).resolve().parents[1];repo=app.parents[1]
assets,public=[Path(p).resolve() for p in sys.argv[1:3]]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DCONFIG_ELF_LOADER=1','-DCONFIG_ELF_LOADER_LOAD_PSRAM=1',
       '-Wall','-Wextra','-Werror','-pedantic','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
       '-Itests/target_stubs','-Itests/stubs','-I.','-I'+str(repo/'components/os/include')]
if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
libraries=['-lcrypto']
if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
sources=['controller_target.c','controller.c','manager_ui.c','manager_catalogue.c','manager_assets.c','manager_native_asset.c','app_manager.c',
         'package_source.c','txn_json.c','txn_package.c','txn_store.c','txn_vfs.c','txn_geometry.c','native_elf.c',
         'host_c/io_posix.c','host_c/crypto_openssl.c','tests/home_littlefs_host.c','tests/home_apps_driver.c']
with tempfile.TemporaryDirectory(prefix='badge-home-apps-') as name:
    tmp=Path(name);volume=tmp/'volume';volume.mkdir();objects=[]
    der=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER']);assert der[-65]==4
    array=lambda b:','.join(f'0x{x:02x}' for x in b)
    config=tmp/'home_fixture_config.h'
    config.write_text('static const uint8_t test_public[65]={'+array(der[-65:])+'};\n'
        'const am_source_config_t am_app_source_config={.public_key=test_public,.key_size=65,'
        '.manifest_url="https://packages.example/manifest.json",.signature_url="https://packages.example/manifest.sig",.payload_url="https://packages.example/module.elf"};\n'
        'const am_layout_policy_t am_app_layout_policy={.qualified=true,.address=0x520000,.size=0x100000,.flash_size=0x800000,.reserve_bytes=4096,.table_sector_sha256={'
        +array(hashlib.sha256(bytes([0x55])*4096).digest())+'}};\n')
    for i,source in enumerate(sources):
        options=[]
        if source=='txn_vfs.c':options=[f'-D{n}=home_test_{n}' for n in ['open','write','mkdir','unlink','rename','fsync']]
        obj=tmp/f'{i}.o';subprocess.run(flags+['-I',str(tmp)]+options+['-c',source,'-o',str(obj)],cwd=app,check=True);objects.append(obj)
    exe=tmp/'test';subprocess.run(flags+list(map(str,objects))+libraries+['-o',str(exe)],cwd=app,check=True)
    data=volume/'aliens/data';data.mkdir(parents=True);(data/'keep').write_text('protected app data')
    subprocess.run([str(exe),str(volume),str(public),str(assets)],check=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    assert (data/'keep').read_text()=='protected app data'
    assert {p.name for p in volume.iterdir()}=={'aliens'} # Missing optional namespaces were never created.
