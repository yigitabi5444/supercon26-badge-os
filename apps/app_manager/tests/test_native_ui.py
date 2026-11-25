"""Compile the actual native UI renderer and contextual actions; optional PGM capture path."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
app=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='badge-native-ui-') as name:
    exe=Path(name)/'ui'
    mode=next((arg for arg in sys.argv[1:] if arg in ('--compact-core14','--fresh-core14')),None)
    output='--lease-only' if '--lease-only' in sys.argv else Path(sys.argv[-1]).resolve() if len(sys.argv)>1 and sys.argv[-1]!=mode else Path(name)/'ui.pgm'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                    '-Itests/stubs','-I../../components/os/include','tests/test_native_ui.c',
                    'manager_catalogue.c','manager_assets.c','manager_native_asset.c','-o',str(exe)],cwd=app,check=True)
    subprocess.run([str(exe)]+([mode] if mode else [])+[str(output)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
