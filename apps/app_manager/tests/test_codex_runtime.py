"""Actual Codex Board module through the production frame host and target I/O.

No sockets, USB devices, target compilation or firmware execution.
"""
from pathlib import Path
import os, subprocess, tempfile
app=Path(__file__).resolve().parents[1];repo=app.parents[1]
flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-pedantic',
       '-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
       '-Itests/codex_stubs','-Itests/native_host_stubs','-Itests/local_https_stubs','-Itests/stubs',
       '-I.','-Imodules/codex_board','-I'+str(repo/'components/os/include')]
# Function-like renames keep ABI struct member names unchanged. Module calls go
# through the real host callbacks to these OS doubles, without global collisions.
renames=[f'-D{n}(...)' + f'=nh_{n}(__VA_ARGS__)' for n in ['box','text','ui_keys','ui_frame','ui_close','ui_wipe']]
host=['native_host.c','native_codex.c','native_hackaday.c','native_messages.c','native_doom.c','native_codex_target.c','tests/native_host_double.c']
module=['tests/codex_controller_host.c','tests/codex_module_host.c','modules/codex_board/cb_native_view.c',
        'modules/codex_board/board_view.c','modules/codex_board/board_feed.c',
        'modules/codex_board/board_stream.c','modules/codex_board/board_alert.c','modules/codex_board/cb_format.c']
def compile_objects(tmp,extra=()):
    objects=[]
    for i,source in enumerate(host+module+['tests/codex_sdk_double.c','tests/test_codex_runtime.c']+list(extra)):
        obj=tmp/f'{i}.o';options=renames if source in host or source=='app_manager.c' else []
        if extra and source=='tests/test_codex_runtime.c':options+=['-Dmain=cb_runtime_checks']
        subprocess.run(flags+options+['-c',source,'-o',str(obj)],cwd=app,check=True);objects.append(str(obj))
    return objects
if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='badge-codex-runtime-') as name:
        tmp=Path(name);exe=tmp/'test'
        subprocess.run(flags+compile_objects(tmp)+['-lm','-o',str(exe)],cwd=app,check=True)
        subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
