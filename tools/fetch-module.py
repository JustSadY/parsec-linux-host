#!/usr/bin/python3
"""Download the supported vendor module and pin it before any compilation."""
import hashlib
from pathlib import Path
import urllib.request

root = Path(__file__).resolve().parents[1]
target = root / '.cache/vendor/parsecd-150-105c.so'
expected = '10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983'
if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == expected:
    print('Verified cached official module 150-105c')
else:
    with urllib.request.urlopen('https://builds.parsec.app/channel/release/binary/linux/gz/parsecd-150-105c.so', timeout=45) as response:
        data = response.read(16 * 1024 * 1024 + 1)
    if len(data) > 16 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != expected:
        raise SystemExit('Vendor module checksum mismatch; no file installed')
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix('.download')
    temporary.write_bytes(data)
    temporary.replace(target)
    print('Downloaded and verified official module 150-105c')
