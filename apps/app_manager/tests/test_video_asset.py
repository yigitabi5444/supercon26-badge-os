"""One measured Video asset via real C streaming transactions/leases; no firmware or network.

Usage: test_video_asset.py /absolute/rickroll.vid [--signed RECEIPT_DIR PUBLIC_PEM]
Default mode uses a disposable host-only key to exercise authenticated invalid fields.
"""
from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess,sys,tempfile

app=Path(__file__).resolve().parents[1];clip=Path(sys.argv[1]).resolve()
expected='c804c2cbb5df2830f17a814d65be2fab229b0825d6e89d089a64551d5d6d6df7'
with clip.open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==expected
assert clip.stat().st_size==819646
signed='--signed' in sys.argv
prefix=Path(shutil.which('openssl')).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='badge-video-asset-') as tmp:
    tmp=Path(tmp);private,public=tmp/'private.pem',tmp/'public.pem'
    if not signed:
        subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256',
                        '-out',str(private)],check=True,capture_output=True)
        subprocess.run(['openssl','pkey','-in',str(private),'-pubout','-out',str(public)],
                       check=True,capture_output=True)
    def receipt(name,owner=b'video',size=819646,digest=bytes.fromhex(expected)):
        folder=tmp/name;folder.mkdir()
        (folder/'clip.receipt').write_bytes(b'BVA1'+owner.ljust(16,b'\0')+struct.pack('<I',size)+digest)
        subprocess.run(['openssl','dgst','-sha256','-sign',str(private),'-out',str(folder/'clip.sig'),
                        str(folder/'clip.receipt')],check=True,capture_output=True)
        return folder
    if signed:
        index=sys.argv.index('--signed');good=Path(sys.argv[index+1]).resolve();public=Path(sys.argv[index+2]).resolve()
    else:good=receipt('good')
    exe=tmp/'driver';flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror',
        '-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
    libraries=['-lcrypto']
    if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
    sources=['video_asset.c','txn_json.c','txn_package.c','txn_store.c','txn_vfs.c','native_elf.c',
             'host_c/io_posix.c','host_c/crypto_openssl.c','tests/video_asset_driver.c']
    subprocess.run(flags+sources+libraries+['-o',str(exe)],cwd=app,check=True)
    root=tmp/'volume';root.mkdir()
    for name in ['versions','stage','active','data']:(root/name).mkdir()
    sentinel=root/'data/protected';sentinel.write_bytes(b'protected Video data')
    current=root/'assets'/expected/'clip.vid'
    def call(mode,expected_result=0,source=clip,record=good,owner='video'):
        r=subprocess.run([str(exe),str(root),str(public),str(record),str(source),mode,owner],
                         capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        assert r.returncode==0,r.stdout+r.stderr
        result=json.loads(r.stdout);assert result['result']==expected_result,(mode,result,expected_result)
        assert result['largest_chunk']<=4096 and sentinel.read_bytes()==b'protected Video data'
        return result
    if not signed:
        for record in [receipt('wrong-length',size=819645),receipt('wrong-owner',owner=b'aliens'),
                       receipt('wrong-hash',digest=b'\0'*32)]:
            assert not call('install',3,record=record)['source_calls'] and not current.exists()
    bad=tmp/'bad-signature';shutil.copytree(good,bad)
    signature=bytearray((bad/'clip.sig').read_bytes());signature[-1]^=1;(bad/'clip.sig').write_bytes(signature)
    assert not call('install',4,record=bad)['source_calls']
    short=tmp/'short.vid';shutil.copyfile(clip,short)
    with short.open('r+b') as f:f.truncate(819645)
    extra=tmp/'extra.vid';shutil.copyfile(clip,extra)
    with extra.open('ab') as f:f.write(b'X')
    for mode,source,result in [('tamper',clip,5),('install',short,5),('install',extra,5),('cancel',clip,10)]:
        call(mode,result,source);assert not current.exists() and not (root/'stage/asset').exists()
    assert not call('install',1,owner='aliens')['source_calls']
    # Retry must reclaim only abandoned staging before its capacity preflight.
    stale=root/'stage/asset';stale.mkdir()
    with (stale/'clip.vid').open('wb') as f:f.truncate(1600000)
    result=call('install');assert result['source_calls']>200
    with current.open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==expected
    def snapshot():
        result={}
        for path in root.rglob('*'):
            if path.is_file():
                with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
                result[str(path.relative_to(root))]=(path.stat().st_size,digest)
        return result
    before=snapshot()
    for mode,source in [('tamper',clip),('install',short),('install',extra),('cancel',clip)]:
        assert not call(mode,16,source)['source_calls'] # Same immutable hash is never replaced or duplicated.
    call('open') # Repeated new read-lease generations reuse the same asset; stale/cross-app reads fail.
    after=snapshot()
    assert after==before and len(list(root.rglob('clip.vid')))==1
    with current.open('r+b') as f:f.seek(100);old=f.read(1);f.seek(100);f.write(bytes([old[0]^1]))
    call('open',5);call('install',5) # Corrupt existing identity is refused, never silently overwritten.
    print('PASS measured819646B Video: authenticated exact owner/length/hash; <=4096B source streaming; '
          'tampered/short/extra/cancel staging refused; atomic content-addressed reuse; orphan retry; '
          'Video-only generation-bound borrowed lease/read bounds; protected data. '+
          ('Existing APP-signed receipt verified.' if signed else 'Disposable host-only signing key.'))
