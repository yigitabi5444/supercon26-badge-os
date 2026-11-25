"""Generate public-only package configuration for the seven fixed app IDs."""
import argparse
import ipaddress
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import urlsplit

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--base-url', required=True)
parser.add_argument('--public-key', required=True, type=Path)
parser.add_argument('--ca', type=Path, help='PEM CA for the existing local IPv4:8443 verifier')
parser.add_argument('--redirect-host', help='Optional public HTTPS redirect host')
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
base = args.base_url.rstrip('/')
url = urlsplit(base)
host = re.compile(r'[a-z0-9](?:[a-z0-9.-]{0,251}[a-z0-9])?\Z')
if (url.scheme != 'https' or not url.netloc or url.username or url.password or
        url.query or url.fragment or any(ord(c) <= 32 or ord(c) > 126 or c in '@#\\' for c in base)):
    parser.error('Use an absolute HTTPS base URL without credentials, query or fragment')
local = args.ca is not None
if local:
    if url.port != 8443 or str(ipaddress.IPv4Address(url.hostname)) + ':8443' != url.netloc or args.redirect_host:
        parser.error('Custom CA requires canonical IPv4:8443 without redirects')
elif not host.fullmatch(url.netloc) or any(x in url.netloc for x in ('..', '.-', '-.')):
    parser.error('Public HTTPS sources require a lowercase host without a port')
if args.redirect_host and (not host.fullmatch(args.redirect_host) or any(x in args.redirect_host for x in ('..', '.-', '-.'))):
    parser.error('Invalid public redirect host')
if not args.public_key.read_bytes().startswith(b'-----BEGIN PUBLIC KEY-----'):
    parser.error('Supply a public SubjectPublicKeyInfo PEM, never a private key')
der = subprocess.check_output(['openssl', 'pkey', '-pubin', '-in', str(args.public_key), '-outform', 'DER'])
prefix = bytes.fromhex('3059301306072a8648ce3d020106082a8648ce3d030107034200')
if len(der) != 91 or not der.startswith(prefix) or der[26] != 4:
    parser.error('APP signing public key must be P-256')
quote = lambda value: json.dumps(value, ensure_ascii=True)
lines = ['#pragma once', '#include "video_source.h"',
         'static const uint8_t channel_key[65]={' + ','.join(f'0x{x:02x}' for x in der[26:]) + '};']
if local:
    ca = args.ca.read_text()
    if not 0 < len(ca.encode()) <= 8192 or 'PRIVATE KEY' in ca:
        parser.error('CA must contain only public certificates, at most 8192 bytes')
    subprocess.run(['openssl', 'x509', '-in', str(args.ca), '-noout'], check=True)
    lines.append('static const char channel_ca[]=' + quote(ca) + ';')
symbols = {'aliens': 'am_app_source_config', 'rocks': 'am_rocks_source_config',
           'video': 'video_code', 'codex_board': 'am_codex_source_config',
           'hackaday': 'am_hackaday_source_config', 'messaging': 'am_messages_source_config',
           'doom': 'am_doom_source_config'}
for app, symbol in symbols.items():
    fields = [f'.{field}={quote(base + "/" + app + "/" + name)}' for field, name in
              [('manifest_url', 'manifest.json'), ('signature_url', 'manifest.sig'), ('payload_url', 'module.elf')]]
    fields += ['.public_key=channel_key', '.key_size=65']
    if local:
        fields += ['.local_authority=' + quote(url.netloc), '.tls_ca_pem=channel_ca']
    elif args.redirect_host:
        fields += ['.redirect_host=' + quote(args.redirect_host)]
    lines.append(('static ' if app == 'video' else '') + 'const am_source_config_t ' + symbol + '={' + ','.join(fields) + '};')
lines.append('const am_video_source_config_t am_video_source_config={.code=&video_code,' +
             ','.join('.' + field + '=' + quote(base + '/video/assets/' + name) for field, name in
                      [('receipt_url', 'receipt.bin'), ('signature_url', 'receipt.sig'), ('clip_url', 'clip.vid')]) + '};')
if len((base + '/codex_board/manifest.json').encode()) > 2048:
    parser.error('Generated package URLs exceed the existing 2048-byte limit')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('\n'.join(lines) + '\n')
print('Generated public channel configuration for seven apps:', args.output)
