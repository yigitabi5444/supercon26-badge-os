"""Copy the tracked stock Video clip and create its unsigned BVA1 receipt."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
clip = Path(__file__).resolve().parents[2] / 'video/rickroll.vid'
digest = hashlib.sha256(clip.read_bytes()).digest()
if clip.stat().st_size != 819646 or digest.hex() != 'c804c2cbb5df2830f17a814d65be2fab229b0825d6e89d089a64551d5d6d6df7':
    raise SystemExit('Tracked clip differs from the native Video asset contract')
args.output.mkdir(parents=True, exist_ok=True)
shutil.copyfile(clip, args.output / 'clip.vid')
(args.output / 'receipt.bin').write_bytes(struct.pack('<4s16sI32s', b'BVA1', b'video', 819646, digest))
print('Prepared stock clip and 56-byte unsigned receipt:', args.output)
