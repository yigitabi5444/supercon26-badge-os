"""Actual C multi-app transactions/source admission, disposable key and namespaced store.

Usage: test_multi_app.py /absolute/SpaceInv/module.elf
   or: test_multi_app.py --signed SPACEINV NEXT_SPACEINV ROCKS PUBLIC_PEM
The rocks payload below is a metadata-only host fixture, never a playable package.
No production key, published package, Xtensa build, network or device is used.
"""
from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess,sys,tempfile

app=Path(__file__).resolve().parents[1]
signed=sys.argv[1:2]==['--signed']
assets=[Path(p).resolve() for p in sys.argv[2:5]] if signed else []
base=(assets[0]/'module.elf').read_bytes() if signed else Path(sys.argv[1]).read_bytes()
table=struct.unpack_from('<I',base,32)[0]
count,names_index=struct.unpack_from('<HH',base,48)
sections=[struct.unpack_from('<10I',base,table+i*40) for i in range(count)]
names=base[sections[names_index][4]:sections[names_index][4]+sections[names_index][5]]
notes=[s for s in sections if names[s[0]:].split(b'\0',1)[0]==b'.badge_app']
assert len(notes)==1 and notes[0][5]==48
note=notes[0][4]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='badge-multi-app-') as tmp:
    tmp=Path(tmp);root=tmp/'volume';root.mkdir()
    private,public=tmp/'host-only-private.pem',tmp/'host-only-public.pem'
    if signed:public=Path(sys.argv[5]).resolve()
    else:
        subprocess.run(['openssl','genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256',
                        '-out',str(private)],check=True,capture_output=True)
        subprocess.run(['openssl','pkey','-in',str(private),'-pubout','-out',str(public)],
                       check=True,capture_output=True)

    def fixture(name,identity,version='1.0.1',note_id=None):
        folder=tmp/name;folder.mkdir()
        payload=bytearray(base)
        struct.pack_into('<16s24s',payload,note+8,(note_id or identity).encode(),version.encode())
        (folder/'module.elf').write_bytes(payload)
        manifest=dict(schema=1,id=identity,version=version,target='esp32s3',abi=2,
                      sdk='6.0.2',loader='2.1.0',bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())
        (folder/'manifest').write_text(json.dumps(manifest,separators=(',',':')))
        subprocess.run(['openssl','dgst','-sha256','-sign',str(private),'-out',str(folder/'signature'),
                        str(folder/'manifest')],check=True,capture_output=True)
        return folder

    if signed:
        def copy_assets(source,name):
            folder=tmp/name;folder.mkdir()
            for src,dst in [('manifest.json','manifest'),('manifest.sig','signature'),('module.elf','module.elf')]:
                shutil.copyfile(source/src,folder/dst)
            return folder
        aliens,aliens_next,rocks=[copy_assets(src,name) for src,name in zip(assets,['aliens','aliens-next','rocks'])]
    else:
        aliens=fixture('aliens','aliens');rocks=fixture('rocks','rocks')
        aliens_next=fixture('aliens-next','aliens','1.0.2')
        rocks_next=fixture('rocks-next','rocks','1.0.2')
        wrong_note=fixture('wrong-note','rocks',note_id='aliens')
    rock_version=json.loads((rocks/'manifest').read_text())['version']
    exe=tmp/'driver'
    flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-g',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
    libraries=['-lcrypto']
    if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
    sources=['controller.c','manager_catalogue.c','package_source.c','txn_json.c','txn_package.c',
             'native_elf.c','txn_store.c','txn_vfs.c','host_c/io_posix.c','host_c/crypto_openssl.c',
             'tests/native_controller_driver.c']
    subprocess.run(flags+sources+libraries+['-o',str(exe)],cwd=app,check=True)

    def action(identity,operation,folder='-',expected=0):
        result=subprocess.run([str(exe),str(root),str(public),operation,str(folder),identity],
                              capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        assert result.returncode==0,result.stdout+result.stderr
        data=json.loads(result.stdout);assert data['result']==expected,(identity,operation,data,expected)
        return data

    def files(identity=None):
        base=root/identity if identity else root
        return {str(p.relative_to(base)):p.read_bytes() for p in base.rglob('*') if p.is_file()}

    assert action('aliens','one_install',aliens)['installs']==1
    assert not (root/'rocks').exists() # Listing a configured app never creates its namespace.
    sentinel_a=root/'aliens/data/protected.txt';sentinel_a.write_bytes(b'aliens data retained')
    a=files('aliens')
    state=action('rocks','one_install',rocks)
    assert state['aliens_installed'] and state['rocks_installed'] and state['installs']==1
    assert files('aliens')==a
    sentinel_r=root/'rocks/data/protected.txt';sentinel_r.write_bytes(b'rocks data retained')
    before=files();action('aliens','one_update',aliens,16);assert files()==before
    action('rocks','one_update',rocks,16);assert files()==before
    # Both directions must reject the authenticated other app before requesting its payload.
    for identity,folder in [('aliens',rocks),('rocks',aliens)]:
        state=action(identity,'one_update',folder,1)
        assert state['payload_reads']==0 and files()==before
    if not signed:
        assert action('rocks','one_update',wrong_note,3)['payload_reads']==1 and files()==before
        for operation,expected in [('one_full',8),('one_cancel',10),('one_cancel_install',10)]:
            action('rocks',operation,rocks_next,expected);assert files()==before
    r=files('rocks');state=action('aliens','one_update',aliens_next)
    assert state['aliens_version']=='1.0.2' and state['rocks_version']==rock_version and files('rocks')==r
    a=files('aliens');state=action('rocks','open')
    assert state['launched']==1 and state['aliens_version']=='1.0.2' and state['rocks_version']==rock_version
    # A correctly signed foreign receipt cannot disable or alter the other namespace.
    saved=files('rocks')
    shutil.copyfile(root/'aliens/active/app.json',root/'rocks/active/app.json')
    for version in (root/'aliens/versions').iterdir():
        shutil.copytree(version,root/'rocks/versions'/version.name,dirs_exist_ok=True)
    poisoned=files();assert action('aliens','open')['launched']==1
    action('rocks','open',expected=6);assert files()==poisoned
    shutil.rmtree(root/'rocks');(root/'rocks').mkdir()
    for name,data in saved.items():
        path=root/'rocks'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    action('rocks','uninstall');assert files('aliens')==a and sentinel_r.read_bytes()==b'rocks data retained'
    state=action('rocks','one_install',rocks)
    assert state['aliens_version']=='1.0.2' and state['rocks_version']==rock_version
    assert sentinel_a.read_bytes()==b'aliens data retained' and sentinel_r.read_bytes()==b'rocks data retained'
    assert files('aliens')==a
    print('PASS actual C source/controller/store/OpenSSL: independent aliens/rocks receipts across processes; '
          'same-version checks; cross-ID rejection before payload; independent update/uninstall/data and offline Open. '+
          ('Genuine signed SpaceInv1.0.1/1.0.2 + Asteroids1.0.0; no target execution or physical-capacity claim.' if signed else
           'Also ELF note/full/cancel checks with synthetic metadata fixture; no target execution.'))
