"""Build only the standalone Asteroids module; no firmware, signing key, store or device access."""
import argparse,hashlib,json,re,struct,subprocess
from pathlib import Path
p=Path(__file__).resolve().parent
arg=argparse.ArgumentParser();arg.add_argument('--cc',required=True);arg.add_argument('--api-dir',required=True,type=Path);arg.add_argument('--output',required=True,type=Path)
a=arg.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
cc=Path(a.cc).absolute();objcopy=cc.with_name(cc.name.replace('gcc','objcopy'));strip=cc.with_name(cc.name.replace('gcc','strip'))
header=a.api_dir/'badge_native_api.h'
assert hashlib.sha256(header.read_bytes()).hexdigest()=='3c26055cc13984c6bd1aca086df442c74151509e7ae9334fbc41852522833e86','Rebind changed host ABI deliberately'
version=re.search(r'#define ROCKS_APP_VERSION "([^"]+)"',(p/'rocks.c').read_text())[1]
obj=out/'rocks.o';elf=out/('rocks-'+version+'.app.elf');meta=out/'module-info.bin'
commands=[]
def run(cmd):commands.append([str(x) for x in cmd]);subprocess.run(commands[-1],check=True)
def sections(payload):
    assert payload[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<H',payload,18)[0]==94
    at=struct.unpack_from('<I',payload,32)[0];stride,count,names=struct.unpack_from('<HHH',payload,46)
    assert stride==40 and at+count*40<=len(payload)
    raw=[struct.unpack_from('<10I',payload,at+i*40) for i in range(count)]
    strings=payload[raw[names][4]:raw[names][4]+raw[names][5]]
    return [{**dict(zip(('name_off','type','flags','addr','offset','size','link','info','align','entsize'),s)),
             'name':strings[s[0]:].split(b'\0',1)[0].decode()} for s in raw]
run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Os','-fno-builtin-memset','-fno-builtin-memcpy','-fPIC','-mlongcalls','-mno-target-align',
     '-ffunction-sections','-fdata-sections','-fvisibility=hidden','-Dmain=app_main','-I',a.api_dir,'-c',p/'rocks.c','-o',obj])
run([objcopy,'--dump-section','.badge_app='+str(meta),obj])
metadata=meta.read_bytes();assert len(metadata)==48
magic,abi,ident,ver=struct.unpack('<II16s24s',metadata)
assert (magic,abi)==(0x32504142,2) and ident==b'rocks'+b'\0'*11 and ver==version.encode()+b'\0'*(24-len(version))
run([cc,'-nostartfiles','-nostdlib','-fPIC','-shared','-e','app_main','-fdata-sections','-ffunction-sections',
     '-Wl,--gc-sections','-fvisibility=hidden','-Wl,--strip-all','-Wl,--strip-debug','-Wl,--strip-discarded',obj,'-Wl,--exclude-libs,ALL','-lm','-lgcc','-o',elf])
# GNU section GC may drop the compiler-retained, unreferenced metadata. Reattach exactly
# the object-emitted bytes; no runtime relocation references this non-ALLOC admission record.
has_info=any(s['name']=='.badge_app' for s in sections(elf.read_bytes()))
if not has_info:run([objcopy,'--add-section','.badge_app='+str(meta),elf])
run([objcopy,'--set-section-flags','.badge_app=readonly,contents','--set-section-alignment','.badge_app=4',elf])
run([strip,'--strip-unneeded','--remove-section=.comment','--remove-section=.dynamic','--remove-section=.xt.lit',
     '--remove-section=.xt.prop','--remove-section=.xtensa.info',elf])
payload=elf.read_bytes();sec=sections(payload);byname={s['name']:s for s in sec};info=byname['.badge_app']
assert info['type']==1 and not info['flags']&2 and info['size']==48 and info['align']==4 and info['offset']%4==0
assert payload[info['offset']:info['offset']+48]==metadata
imports=[]
for s in sec:
    if s['type']!=11:continue
    strings=sec[s['link']];table=payload[strings['offset']:strings['offset']+strings['size']]
    for i in range(0,s['size'],16):
        name,value,size,bind,other,index=struct.unpack_from('<IIIBBH',payload,s['offset']+i)
        if index==0 and name:imports.append(table[name:].split(b'\0',1)[0].decode())
assert sorted(set(imports))==['badge_app_get_api'],imports
text=byname['.text'];rounded_end=text['addr']+((text['size']+3)&~3)
for name in ('.rodata','.data','.bss'):
    s=byname.get(name)
    if not s or not s['size']:continue
    assert s['addr']%4==0 and not (text['addr']<s['addr']+s['size'] and s['addr']<rounded_end),(name,'rounded TEXT overlap')
assert len(payload)<=24576, "Asteroids pilot payload budget"
manifest=dict(schema=1,id='rocks',version=version,target='esp32s3',abi=2,sdk='6.0.2',loader='2.1.0',bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())
manifest_text=json.dumps(manifest,sort_keys=True,indent=2)+'\n';assert len(manifest_text.encode())<=512
(out/'unsigned-manifest.json').write_text(manifest_text)
report=dict(display_name='Asteroids',manifest=manifest,elf=str(elf),imports=sorted(set(imports)),metadata_nonalloc=True,
    metadata_alignment=info['align'],metadata_bytes=info['size'],pilot_caps=dict(elf=24576,manifest=512,signature=128),
    sections=[{k:s[k] for k in ('name','addr','size','flags','align')} for s in sec if s['flags']&2],
    model_bytes=2256,load_section_bytes=sum(s['size'] for s in sec if s['flags']&2 and s['name'] in ('.text','.rodata','.data','.bss')),
    source_sha256={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in [p/'rocks.c',p/'model.h',p/'render.h',header]},
    math_archives_sha256={name:hashlib.sha256(Path(subprocess.check_output([str(cc),'-print-file-name='+name],text=True).strip()).read_bytes()).hexdigest() for name in ('libm.a','libgcc.a')},
    compiler=subprocess.check_output([str(cc),'--version'],text=True).splitlines()[0],commands=commands,
    scope='Standalone module only. Section sums are not target peak heap or total App Manager cost. No firmware, signing, storage or device action.')
(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
