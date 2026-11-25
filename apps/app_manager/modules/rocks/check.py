"""Focused original-Asteroids model/pixel comparison and ABI2 scheduling proof."""
import argparse,hashlib,subprocess
from pathlib import Path
p=Path(__file__).resolve().parent;repo=p.parents[3]
a=argparse.ArgumentParser();a.add_argument('--api-dir',required=True,type=Path);a.add_argument('--output',required=True,type=Path);a=a.parse_args();a.output.mkdir(parents=True,exist_ok=True)
s=(repo/'apps/rocks/rocks.c').read_text();assert hashlib.sha256(s.encode()).hexdigest()=='3dacf90238cd832376bfa662dfdf13aa3c61df085c6ce5524ff49bf00fb37ed0'
state=s[s.index('typedef struct'):s.index('static SemaphoreHandle_t')]
helpers=s[s.index('static float frand'):s.index('static void draw_task')]
model=s[s.index('static void add_rock'):s.index('static void run(void)')]
tick=s[s.index('            g.t++;'):s.index('            xSemaphoreGive(lock);',s.index('            g.t++;'))]
tick=tick.replace('bool over = g.over, clear = !any && !g.gone;','return !any && !g.gone;')
names=['rock_t','shot_t','RAD','PTS','g','d','frand','wrap','near','hit','dot','line','shape','ship','draw','add_rock','wave','spawn','ref','cool']
fixture='\n'.join('#define '+x+' baseline_'+x for x in names)+'\nstatic int ref,cool;\n'+state+helpers+model+'static bool baseline_tick(uint32_t held,bool fire,int a) {\n'+tick+'\n}\n'+'\n'.join('#undef '+x for x in names)
(a.output/'reference.h').write_text(fixture)
command=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(a.api_dir),'-I',str(repo/'components/os/include'),'-I',str(p),'-I',str(a.output),str(p/'test.c'),'-o',str(a.output/'check')]
subprocess.run(command,check=True);subprocess.run([str(a.output/'check'),str(a.output/'initial.pbm')],check=True)
