"""Native preflight checks with strict warnings and ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


app = Path(__file__).resolve().parents[1]
arguments = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='badge-native-elf-') as directory:
    directory = Path(directory)
    binary = directory / 'native-elf-tests'
    command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-pedantic', '-g', '-O1', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', str(app / 'native_elf.c'),
               str(app / 'tests/test_native_elf.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary), *arguments], check=True,
                   env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
                        'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
