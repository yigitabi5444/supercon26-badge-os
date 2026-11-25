#!/usr/bin/env python3
"""Compile the actual registry with optional help on/off and the legacy profile."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source='\n'.join(l for l in (root/'main/apps.c').read_text().splitlines() if not l.startswith('#include'))
labels={'badge':'Badge','clock':'Clock','update':'Update','pong':'Pong','snake':'Snake','life':'Life','aliens':'SpaceInv','blocks':'Tetris','rocks':'Asteroids','radar':'Radar','source':'Source','hackaday':'Hackaday','video':'Video','weather':'Weather','settings':'Settings','doom':'Doom','codex_board':'Codex Board','faq':'FAQ','messaging':'Messages','clock_faces':'Clock Faces','app_manager':'App Manager'}
core=['Badge','Clock','Update','Pong','Snake','Life','Tetris','Radar','Source','Weather','Settings','FAQ','Clock Faces','App Manager']
legacy=['Badge','Clock','Update','Pong','Snake','Life','SpaceInv','Tetris','Asteroids','Radar','Source','Hackaday','Video','Weather','Settings','Doom','Codex Board','FAQ','Messages']
with tempfile.TemporaryDirectory(prefix='badge-registry-') as d:
 for enabled,help in [(1,0),(1,1),(0,0)]:
  p=Path(d);code='#include <assert.h>\n#include <stddef.h>\n#include <string.h>\ntypedef struct {const char*name;} app_t;typedef struct {int unused;} app_info_t;\n'
  for id,name in labels.items():
   code+='const app_t app_'+id+'={"'+name+'"};\n'
   if id!='app_manager' or help:code+='const app_info_t app_'+id+'_info={0};\n'
  code+=source+'\nint main(void){const char *expected[]={'+','.join('"'+s+'"' for s in (core if enabled else legacy))+'};assert(napps==sizeof(expected)/sizeof(*expected));for(int i=0;i<napps;i++)assert(!strcmp(apps[i]->name,expected[i]));'
  if enabled:code+='assert((app_infos[13]!=NULL)=='+str(help)+');'
  code+='}\n';c=p/'test.c';exe=p/'test';c.write_text(code)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DBADGE_APP_MANAGER='+str(enabled),'-DCONFIG_BADGE_APP_MANAGER_HELP='+str(help),str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
print('PASS actual registry: Core14 optional-help off/on and legacy19; no optional apps in fresh Core14')
