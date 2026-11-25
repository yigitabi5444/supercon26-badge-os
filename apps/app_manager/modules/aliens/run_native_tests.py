"""Native module checks against the original renderer, not a firmware/device build."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
p=Path(__file__).resolve().parent;repo=p.parents[3]
a=argparse.ArgumentParser();a.add_argument('--api-dir',required=True,type=Path);a.add_argument('--output',type=Path);args=a.parse_args()
out=args.output or Path(tempfile.mkdtemp(prefix='aliens-native-'));out.mkdir(parents=True,exist_ok=True)
raw=(repo/'apps/aliens/aliens.c').read_bytes()
# Exact upstream b43e450 and its identical game with appended launcher help.
assert hashlib.sha256(raw).hexdigest() in {
    '6279f7d1dcccbccbf98159a327b7af0347fab72f5607db8327040ec7e3115540',
    '80315151b9e23d9931e16df34560493a92692d46a7ec038057816fc3e98264d8',
},'Original built-in changed: rebind and review deliberately'
s=raw.decode()
sprites=s[s.index('// Sprites:'):s.index('static struct {')]
state=s[s.index('static struct {'):s.index('static SemaphoreHandle_t')];state=state[:state.index('} g;')+len('} g;')]
render=s[s.index('static void sprite('):s.index('static void draw_task')]
# Rename only static identifiers; the original source and its formatting/drawing code remain unchanged.
renames=['kinds','gun','burst','g','sprite','kind','draw']
text='\n'.join('#define '+x+' baseline_'+x for x in renames)+'\n'+sprites+state+render+'\n'+'\n'.join('#undef '+x for x in renames)
(out/'reference_render.h').write_text(text)
exe=out/'test_native';subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I',str(args.api_dir),'-I',str(repo/'components/os/include'),'-I',str(p),'-I',str(out),str(p/'test_native.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True);print('Native evidence:',out)
