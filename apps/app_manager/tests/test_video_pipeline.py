"""Joined real signed Video code/asset/controller/HTTPS/launcher path with SDK and ELF-execution doubles.

Usage: test_video_pipeline.py CODE_ASSETS CLIP_ASSETS PUBLIC_PEM [SPACEINV_ASSETS]
No socket, listener, target build, Xtensa execution or device access.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile

app=Path(__file__).resolve().parents[1];repo=app.parents[1]
code,assets,public=[Path(p).resolve() for p in sys.argv[1:4]]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='badge-video-pipeline-') as tmp:
    tmp=Path(tmp);raw=tmp/'public.raw'
    der=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER'])
    assert der[-65]==4;raw.write_bytes(der[-65:])
    binary=tmp/'test'
    flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Dtime=am_test_time',
           '-DCONFIG_MBEDTLS_HAVE_TIME_DATE=1','-DCONFIG_ELF_LOADER=1','-DCONFIG_ELF_LOADER_LOAD_PSRAM=1',
           '-Wall','-Wextra','-Werror','-pedantic','-g','-O1',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
           '-Itests/local_https_stubs','-Itests/native_host_stubs','-Itests/fetch_stubs','-Itests/target_stubs',
           '-Itests/stubs','-I.','-Imodules/video','-I'+str(repo/'components/os/include'),'-I'+str(repo/'components/stb')]
    if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
    libraries=['-lcrypto']
    if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
    sources=['controller.c','manager_catalogue.c','app_manager.c','native_host.c','native_codex.c','native_hackaday.c','native_messages.c','native_doom.c','native_video_target.c',
        'video_asset.c','video_source.c','package_source.c','package_http.c','txn_json.c','txn_package.c',
        'txn_store.c','txn_vfs.c','native_elf.c','host_c/io_posix.c','host_c/crypto_openssl.c',
        'modules/video/vid2.c','modules/video/video_playback.c',
        'tests/native_host_double.c','tests/video_https_double.c','tests/video_module_host.c','tests/video_pipeline_driver.c',
        str(repo/'components/stb/stb_image.c')]
    subprocess.run(flags+sources+libraries+['-lm','-o',str(binary)],cwd=app,check=True)
    def new_store(name):
        root=tmp/name;root.mkdir();(root/'video/data').mkdir(parents=True)
        (root/'video/data/protected').write_bytes(b'keep Video user data')
        return root
    def snapshot(root):
        result={}
        for p in root.rglob('*'):
            if p.is_file():
                with p.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
                result[str(p.relative_to(root))]=(p.stat().st_size,digest)
        return result
    def call(root,action='install',mode='good',expect=0,code_dir=code):
        r=subprocess.run([str(binary),str(root),str(public),str(raw),str(code_dir),str(assets),action,mode],
            capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert r.returncode==0,r.stdout+r.stderr
        data=json.loads(r.stdout);assert data['result']==expect,(action,mode,data,expect)
        assert data['max_read']<=4096
        assert (root/'video/data/protected').read_bytes()==b'keep Video user data'
        return data
    failures=[('missing',7),('short',7),('length',12),('chunked',7),('gzip',7),
              ('redirect',1),('tamper',5),('cancel',10),('timeout',7),('bad-receipt',4)]
    for mode,result in failures:
        root=new_store('failure-'+mode);state=call(root,mode=mode,expect=result)
        assert not state['installed'] and not state['cached'] and not (root/'video/active/app.json').exists()
        assert not (root/'video/stage/asset').exists()
        if mode=='bad-receipt':assert not state['clip_opens']
        if mode in ['length','chunked','gzip','redirect','missing']:assert not state['clip_reads']
    root=new_store('real-install');state=call(root)
    assert state['installed'] and state['cached'] and state['clip_opens']==1 and state['clip_reads']>200
    assert state['joins']==2 and state['syncs']==1 # Two links, same successful Clock freshness stamp.
    before=snapshot(root);state=call(root,'update',expect=16)
    assert not state['clip_opens'] and state['opens']==3 and snapshot(root)==before
    state=call(root,'open');assert state['launched']==1 and state['loader_inits']==1 and not state['opens'] and not state['joins']
    assert snapshot(root)==before
    if len(sys.argv)>4:
        state=call(root,'update',expect=1,code_dir=Path(sys.argv[4]).resolve())
        assert state['opens']==2 and not state['clip_opens'] and snapshot(root)==before
    asset_root=root/'video/assets';saved=tmp/'saved-assets';shutil.copytree(asset_root,saved)
    shutil.rmtree(asset_root);active=(root/'video/active/app.json').read_bytes()
    state=call(root,'open',expect=2);assert not state['loader_inits'] and not state['opens'] and state['installed']
    assert (root/'video/active/app.json').read_bytes()==active
    for mode,result in failures:
        state=call(root,'update',mode,result)
        assert state['installed'] and not state['cached'] and (root/'video/active/app.json').read_bytes()==active
    shutil.copytree(saved,asset_root,dirs_exist_ok=True)
    current=next(asset_root.glob('*/clip.vid'))
    with current.open('r+b') as f:f.seek(100);b=f.read(1);f.seek(100);f.write(bytes([b[0]^1]))
    broken=snapshot(root)
    state=call(root,'open',expect=5);assert not state['loader_inits'] and snapshot(root)==broken
    state=call(root,'update',expect=5);assert not state['clip_opens'] and not state['cached'] and snapshot(root)==broken
    print('PASS joined genuine Video1.0.0 code+receipt+clip: bounded HTTPS stage before code commit, '
          'bad/missing/short/length/chunked/gzip/redirect/tamper/cancel/timeout preserve active/data, '
          'same-version asset reuse, independent-process offline Open through real C module/player/stb/host; '
          'SDK/TLS/radio/ELF execution doubled, no socket or target execution.')
