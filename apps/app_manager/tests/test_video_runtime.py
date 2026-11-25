"""Actual Video prefix/host/asset/player/stb integration; host-only platform doubles.

Usage: test_video_runtime.py PLAYER_DIRECTORY SIGNED_RECEIPT_DIRECTORY PUBLIC_PEM CLIP
"""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile

app=Path(__file__).resolve().parents[1];repo=app.parents[1]
player,receipt,public,clip=[Path(p).resolve() for p in sys.argv[1:5]]
prefix=Path(shutil.which('openssl')).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='badge-video-runtime-') as temp:
    temp=Path(temp);binary=temp/'test';root=temp/'volume'
    flags=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-pedantic',
           '-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
           '-Itests/native_host_stubs','-Itests/stubs','-I.',
           '-I'+str(repo/'components/os/include'),'-I'+str(repo/'components/stb'),'-I'+str(player)]
    if (prefix/'include/openssl/evp.h').exists():flags+=['-I',str(prefix/'include')]
    libraries=['-lcrypto']
    if (prefix/'lib/libcrypto.dylib').exists():libraries=['-L',str(prefix/'lib')]+libraries
    sources=['native_host.c','native_codex.c','native_hackaday.c','native_messages.c','native_doom.c','native_video_target.c','video_asset.c','txn_json.c','txn_package.c',
             'txn_store.c','txn_vfs.c','native_elf.c','host_c/io_posix.c','host_c/crypto_openssl.c',
             'tests/native_host_double.c','tests/test_video_runtime.c',
             str(repo/'components/stb/stb_image.c'),str(player/'vid2.c'),str(player/'video_playback.c')]
    subprocess.run(flags+sources+libraries+['-lm','-o',str(binary)],cwd=app,check=True)
    subprocess.run([str(binary),str(root),str(public),str(receipt),str(clip)],check=True,
                   env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
