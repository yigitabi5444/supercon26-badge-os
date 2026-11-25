"""Actual signed native v1 through C store/controller, with process-restart fault recovery.
Usage: python3 run_native_transactions.py PACKAGE_DIRECTORY PUBLIC_PEM
PACKAGE_DIRECTORY contains manifest.json, manifest.sig and module.elf; no keys are created.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

APP = Path(__file__).resolve().parents[1]
args=sys.argv[1:]
one_action='--one-action' in args
if one_action:args.remove('--one-action')
package, public = Path(args[0]).resolve(),Path(args[-1]).resolve()
next_package=Path(args[1]).resolve() if one_action else None
manifest = json.loads((package / 'manifest.json').read_text())
assert manifest['id'] == 'aliens' and manifest['abi'] == 2
assert manifest['bytes'] == (package / 'module.elf').stat().st_size
prefix = Path(shutil.which('openssl')).resolve().parents[1]
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

with tempfile.TemporaryDirectory(prefix='badge-native-transactions-') as name:
    temp = Path(name)
    inputs = temp / 'package'
    inputs.mkdir()
    for source, dest in [('manifest.json', 'manifest'), ('manifest.sig', 'signature'), ('module.elf', 'module.elf')]:
        shutil.copyfile(package / source, inputs / dest)
    common = ['txn_json.c', 'txn_package.c', 'native_elf.c', 'txn_store.c', 'txn_vfs.c', 'package_source.c',
              'host_c/io_posix.c', 'host_c/crypto_openssl.c']
    flags = ['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if (prefix / 'include/openssl/evp.h').exists():
        flags += ['-I', str(prefix / 'include')]
    libraries = ['-lcrypto']
    if (prefix / 'lib/libcrypto.dylib').exists():
        libraries = ['-L', str(prefix / 'lib')] + libraries
    store, controller = temp / 'store', temp / 'controller'
    targets=[(controller, ['controller.c', 'manager_catalogue.c', 'tests/native_controller_driver.c'])]
    if not one_action:targets.insert(0,(store,['tests/c_store_driver.c']))
    for output, extra in targets:
        subprocess.run(flags + common + extra + libraries + ['-o', str(output)], cwd=APP, check=True)
    root = temp / 'filesystem'

    def raw(operation, folder=inputs, point=-1, mode=0, full=0, abi=2, quota=131072):
        return subprocess.run([str(store), str(root), str(public), operation, str(folder), str(abi),
                               str(quota), str(point), str(mode), str(full)], capture_output=True, text=True, env=env)

    def call(operation, expected=0, **kw):
        r = raw(operation, **kw)
        assert r.returncode == 0, r.stdout + r.stderr
        result = json.loads(r.stdout)
        assert result['result'] == expected, (operation, result, expected)
        return result

    def control(operation, expected=0, folder=inputs):
        r = subprocess.run([str(controller), str(root), str(public), operation, str(folder)],
                           capture_output=True, text=True, env=env)
        assert r.returncode == 0, r.stdout + r.stderr
        result = json.loads(r.stdout)
        assert result['result'] == expected, (operation, result)
        return result

    if one_action:
        newer=temp/'newer';newer.mkdir()
        for src,dst in [('manifest.json','manifest'),('manifest.sig','signature'),('module.elf','module.elf')]:
            shutil.copyfile(next_package/src,newer/dst)
        result=control('one_install',expected=1,folder='-')
        assert not result['installed'] and result['fetches']==1
        result=control('one_install')
        assert result['installed'] and result['version']==manifest['version'] and result['fetches']==1 and result['installs']==1
        data=root/'data/protected.txt';data.write_bytes(b'host preservation sentinel')

        def retained():
            return {str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*')
                    if p.is_file() and 'stage' not in p.relative_to(root).parts}

        before=retained();result=control('one_update',expected=16)
        assert result['fetches']==1 and not result['updates'] and retained()==before
        bad_sig=temp/'bad-signature';shutil.copytree(newer,bad_sig)
        bytes_=bytearray((bad_sig/'signature').read_bytes());bytes_[-1]^=1;(bad_sig/'signature').write_bytes(bytes_)
        bad_hash=temp/'bad-hash';shutil.copytree(newer,bad_hash)
        bytes_=bytearray((bad_hash/'module.elf').read_bytes());bytes_[-1]^=1;(bad_hash/'module.elf').write_bytes(bytes_)
        for action,folder,wanted in [('one_update',bad_sig,4),('one_update',bad_hash,5),
                                     ('one_full',newer,8),('one_cancel',newer,10),('one_cancel_install',newer,10)]:
            before=retained();result=control(action,expected=wanted,folder=folder)
            assert result['fetches']==1 and result['version']==manifest['version'] and retained()==before
        result=control('one_update',folder=newer)
        next_meta=json.loads((next_package/'manifest.json').read_text())
        assert result['version']==next_meta['version'] and result['fetches']==1 and result['updates']==1
        before=retained();control('one_update',expected=11);assert retained()==before
        control('one_update',expected=16,folder=newer);assert retained()==before
        result=control('open',folder='-');assert result['launched']==1 and result['version']==next_meta['version']
        assert data.read_bytes()==b'host preservation sentinel'
        print('PASS one action: missing install; signed install; same-version up-to-date; real newer update; bad signature/hash, older, full and precommit cancellation preserve current/data; offline Open. Actual C controller/store with fetch/launcher doubles, no device.')
        sys.exit(0)

    call('check')
    call('install')
    call('lookup')  # Independent process, not a retained UI state.
    call('lease')
    call('install', expected=11)  # Same version cannot masquerade as an update.
    call('lookup', expected=1, abi=3)  # Disabled package is retained across incompatible runtime.
    call('lookup')
    assert control('open')['execution_size'] == manifest['bytes']
    control('busy', expected=9)  # Copy and lease retained until a later successful quiesce.
    control('source', expected=1)
    data = root / 'data' / 'protected.txt'
    data.write_text('host-only data preservation sentinel')
    control('uninstall')
    call('lookup', expected=2)
    assert data.read_text() == 'host-only data preservation sentinel'
    control('install')
    assert control('lookup')['version'] == manifest['version']
    control('uninstall')
    for point in range(8):
        r = raw('install', point=point, mode=2)
        assert r.returncode == 73, (point, r.stdout, r.stderr)
        call('recover')
        call('lookup', expected=0 if point >= 6 else 2)
        call('uninstall')
        assert data.exists()
    call('install', expected=8, quota=1)
    call('lookup', expected=2)
    call('install', expected=8, full=1)
    call('recover')
    call('lookup', expected=2)
    call('install', expected=8, full=1, quota=10000)
    call('install', quota=10000)  # Retry must reclaim failed stage before capacity preflight.
    call('lookup')
    call('uninstall')
    corrupted = temp / 'corrupted'
    shutil.copytree(inputs, corrupted)
    content = bytearray((corrupted / 'module.elf').read_bytes())
    content[64] ^= 1
    (corrupted / 'module.elf').write_bytes(content)
    call('install', expected=5, folder=corrupted)
    call('lookup', expected=2)
    print('PASS: signed real v1; actual C store/controller; independent-process persistence; 8 install crash checkpoints; no downgrade; private-size snapshot; busy lifecycle retention; full/corrupt rejection; protected data. No target execution, v2 build, firmware build or device access.')
