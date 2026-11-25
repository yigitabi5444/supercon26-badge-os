#!/usr/bin/env python3
"""Standalone unsigned Messages ELF only. No host firmware, signing, server or device."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess
ROOT=Path(__file__).resolve().parent
APP_ID='messaging';VERSION='1.0.0'
HEADERS={'include/badge_native_api.h':'3c26055cc13984c6bd1aca086df442c74151509e7ae9334fbc41852522833e86',
         'include/badge_messages_api.h':'d985d603bd9785494189943a62e059a0b86c6c6d2332b9ced8bf785d81a615f0',
         'include/badge/messaging.h':'3c5e57b6a1ba3ec99aab7381ce932f3081582390adfc030fb61b29ad94a4a7ef'}
MAPPED=('.text','.bss','.data','.rodata','.data.rel.ro','.got.loc')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def sections(data):
    assert data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(3,94)
    at=struct.unpack_from('<I',data,32)[0];stride,count,names=struct.unpack_from('<HHH',data,46)
    assert stride==40 and at+count*40<=len(data)
    fields='name_index type flags address offset size link info align entsize'.split()
    raw=[dict(zip(fields,struct.unpack_from('<10I',data,at+i*40))) for i in range(count)]
    table=raw[names];strings=data[table['offset']:table['offset']+table['size']]
    for s in raw:s['name']=strings[s['name_index']:].split(b'\0',1)[0].decode()
    return raw
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--cc',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--finalize-existing',action='store_true',help='Finalize the already compiled/linked ELF; never recompile target sources.')
    args=parser.parse_args()
    cc=args.cc.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    tool=lambda name:cc.with_name(cc.name.replace('gcc',name))
    for name,expected in HEADERS.items():assert digest(ROOT/name)==expected,name
    if args.finalize_existing:
        previous=json.loads((out/'source-sha256.json').read_text())
        for name,expected in previous.items():
            if name!='build.py':assert digest(ROOT/name)==expected,('compiled source changed',name)
        if not (out/'initial-commands.json').exists():(out/'initial-commands.json').write_bytes((out/'commands.json').read_bytes())
    commands=[]
    def run(*cmd):
        command=[str(x) for x in cmd];commands.append(command);p=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if p.returncode:print(p.stdout);raise SystemExit(p.returncode)
        return p.stdout
    objects=[]
    for source,name,extra in [
        ('src/messages_module.c','messages_module',[]),('src/controller.c','controller',[]),('src/render.c','render',[]),('src/private_memory.c','memory',[]),
        ('vendor/qrcodegen.c','qr',['-DNDEBUG','-Wno-unused-parameter','-Dstrlen=msg_strlen','-Dstrchr=msg_strchr','-Dmemcpy=msg_memcpy','-Dmemset=msg_memset','-Dmemmove=msg_memmove','-Dabs=msg_abs','-Dlabs=msg_labs'])]:
        obj=out/(name+'.o')
        if not args.finalize_existing:run(cc,'-std=c11','-Wall','-Wextra','-Werror','-Os','-ffreestanding','-fno-builtin','-fno-tree-loop-distribute-patterns','-fno-stack-protector',
            '-fPIC','-mlongcalls','-mno-target-align','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-fstack-usage',
            '-I',ROOT/'include','-I',ROOT/'vendor',*extra,'-c',ROOT/source,'-o',obj)
        objects.append(obj)
    elf=out/f'{APP_ID}-{VERSION}.app.elf';metadata=out/'module-info.bin'
    if not args.finalize_existing:run(tool('objcopy'),'--dump-section',f'.badge_app={metadata}',objects[0])
    note=struct.pack('<II16s24s',0x32504142,2,APP_ID.encode(),VERSION.encode())
    assert metadata.read_bytes()==note
    if not args.finalize_existing:
        run(cc,'-nostartfiles','-nostdlib','-fPIC','-shared','-e','app_main','-Wl,--gc-sections,--strip-all,--strip-debug,--strip-discarded',
            '-Wl,--exclude-libs,ALL','-Wl,-T,'+str(ROOT/'native-layout.ld'),*objects,'-lgcc','-o',elf)
        if '.badge_app' not in {s['name'] for s in sections(elf.read_bytes())}:run(tool('objcopy'),'--add-section',f'.badge_app={metadata}',elf)
        run(tool('objcopy'),'--set-section-flags','.badge_app=readonly,contents','--set-section-alignment','.badge_app=4',elf)
        run(tool('strip'),'--strip-unneeded','--remove-section=.comment','--remove-section=.dynamic','--remove-section=.xt.lit','--remove-section=.xt.prop','--remove-section=.xtensa.info',elf)
    # objcopy/strip can leave NOBITS sh_offset at the preceding four-byte .got
    # endpoint even though .bss sh_addralign is eight. NOBITS has no file payload.
    # Canonicalize only its conceptual file offset for the existing strict parser;
    # preserve all virtual addresses, section contents, sizes, flags and alignment.
    corrected=bytearray(elf.read_bytes());normalization=[];shoff=struct.unpack_from('<I',corrected,32)[0]
    for index,s in enumerate(sections(corrected)):
        if s['type']==8 and s['align']>1 and s['offset']%s['align']:
            aligned=(s['offset']+s['align']-1)&~(s['align']-1);assert aligned<=len(corrected)
            struct.pack_into('<I',corrected,shoff+index*40+16,aligned)
            normalization.append({'section':s['name'],'old_offset':s['offset'],'new_offset':aligned})
    elf.write_bytes(corrected)
    data=elf.read_bytes();sec=sections(data);byname={s['name']:s for s in sec};info=byname['.badge_app'];entry=struct.unpack_from('<I',data,24)[0]
    assert (info['type'],info['flags'],info['size'],info['align'])==(1,0,48,4)
    assert info['offset']%4==0 and data[info['offset']:info['offset']+48]==note
    imports=run(tool('nm'),'-D','--undefined-only',elf).split();assert imports==['U','badge_app_get_api'],imports
    text=byname['.text'];assert entry%4==0 and text['address']<=entry<text['address']+text['size']
    mapped={name:byname[name] for name in MAPPED if name in byname}
    rounded=(text['size']+3)&~3
    for name,s in mapped.items():
        if name!='.text' and s['size']:assert s['address']%4==0 and not(text['address']<s['address']+s['size'] and s['address']<text['address']+rounded)
    got=mapped.get('.got.loc');span=((got['address']+got['size']-text['address']+3)&~3) if got and got['size'] else rounded
    allocation=span+sum(s['size']+s['align']-1 for name,s in mapped.items() if name not in ('.text','.got.loc') and s['size'])
    relocs=[];types={}
    for s in sec:
        if s['type']!=4:continue
        assert s['entsize']==12
        for i in range(0,s['size'],12):
            off,word,addend=struct.unpack_from('<IIi',data,s['offset']+i);kind=word&255;types[str(kind)]=types.get(str(kind),0)+1
            relocs.append({'section':s['name'],'offset':off,'type':kind,'symbol':word>>8,'addend':addend})
    assert len(data)<=65536 and allocation<=131072
    manifest=dict(schema=1,id=APP_ID,version=VERSION,target='esp32s3',abi=2,sdk='6.0.2',loader='2.1.0',bytes=len(data),sha256=digest(elf))
    manifest_text=json.dumps(manifest,sort_keys=True,indent=2)+'\n';assert len(manifest_text.encode())<=512
    (out/'unsigned-manifest.json').write_text(manifest_text)
    hashes={str(p.relative_to(ROOT)):digest(p) for p in sorted(ROOT.rglob('*')) if p.is_file() and 'artifacts' not in p.relative_to(ROOT).parts and '__pycache__' not in p.parts and p.name!='FREEZE.json'}
    (out/'source-sha256.json').write_text(json.dumps(hashes,sort_keys=True,indent=2)+'\n')
    report={'display_name':'Messages','id':APP_ID,'version':VERSION,'manifest':manifest,'elf':str(elf),'entry':entry,'imports':imports[1:],
        'metadata':{k:info[k] for k in ['size','flags','align','offset']},'messages_api_bytes':116,'native_prefix_bytes':72,
        'mapped_sections':{name:{k:s[k] for k in ['address','size','align']} for name,s in mapped.items()},'mapped_section_sum':sum(s['size'] for s in mapped.values()),
        'loader_allocation_bound':allocation,'loader_caps':{'payload':65536,'mapped':131072},'relocations':len(relocs),'relocation_types':types,
        'nobits_file_offset_normalization':normalization,'compiled_target_sources_this_run':not args.finalize_existing,
        'source_checksum_file_sha256':digest(out/'source-sha256.json'),'compiler':run(cc,'--version').splitlines()[0],
        'scope':'Unsigned standalone module only; not host enrollment/signing/install, target execution, SDK stop/drain or runtime resource qualification.'}
    (out/'relocations.json').write_text(json.dumps(relocs,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
