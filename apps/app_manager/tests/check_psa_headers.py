"""Public PSA API syntax check only; not an IDF target compile/link or crypto run."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

idf = Path(sys.argv[1]).resolve()
root = idf / 'components/mbedtls/mbedtls/tf-psa-crypto'
includes = [root / 'include', root / 'drivers/builtin/include']
source = Path(__file__).resolve().parents[1] / 'txn_crypto_psa.c'
command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsyntax-only']
for include in includes:
    command.extend(['-I', str(include)])
subprocess.run(command + [str(source)], check=True)
headers = ['include/psa/crypto.h', 'include/psa/crypto_types.h', 'include/psa/crypto_struct.h']
print(json.dumps(dict(status='PASS public-PSA-header syntax only', idf=str(idf),
                      headers={p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in headers}), indent=2))
