"""Focused private VFS platform branches; host-only filesystem, no IDF build/mount."""
from pathlib import Path
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
flags = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='badge-vfs-stat-') as directory:
    root = Path(directory)
    for target in [False, True]:
        name = 'esp-branch' if target else 'posix-host'
        options = ['-DESP_PLATFORM'] if target else []
        obj, binary = root / (name + '.o'), root / name
        subprocess.run(flags + options + ['-c', 'txn_vfs.c', '-o', str(obj)], cwd=app, check=True)
        symbols = subprocess.check_output(['nm', '-u', str(obj)], text=True)
        assert ('lstat' in symbols) == (not target), symbols
        subprocess.run(flags + options + [str(obj), 'txn_json.c', 'tests/test_vfs_stat.c',
                                         '-o', str(binary)], cwd=app, check=True)
        subprocess.run([str(binary), str(root / (name + '-store'))], check=True)
        print(name + ': undefined-symbol inspection PASS', flush=True)
