"""Host model comparison only; no SDK firmware, signing key or device access."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
p=Path(__file__).resolve().parent;repo=p.parents[3]
a=argparse.ArgumentParser();a.add_argument('--output',type=Path);args=a.parse_args()
out=args.output or Path(tempfile.mkdtemp(prefix='aliens-model-'));out.mkdir(parents=True,exist_ok=True)
raw=(repo/'apps/aliens/aliens.c').read_bytes()
# Exact upstream b43e450 and its identical game with appended launcher help.
assert hashlib.sha256(raw).hexdigest() in {
    '6279f7d1dcccbccbf98159a327b7af0347fab72f5607db8327040ec7e3115540',
    '80315151b9e23d9931e16df34560493a92692d46a7ec038057816fc3e98264d8',
},'Original built-in changed: rebind and review deliberately'
source=raw.decode()
state=source[source.index('static struct {'):source.index('static SemaphoreHandle_t')]
state=state[:state.index('} g;')+len('} g;')]
kind=source[source.index('static int kind('):source.index('static int draw(')]
logic=source[source.index('static void new_wave('):source.index('static void run(')]
(out/'reference.h').write_text('// Verbatim unchanged built-in model; read-only test fixture.\n'+state+'\n'+kind+logic)
exe=out/'test_model'
subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(p),'-I',str(out),str(p/'test_model.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print('Model evidence:',out)
