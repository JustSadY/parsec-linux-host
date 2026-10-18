#!/usr/bin/python3
"""Download the current official installer and update pinned metadata, never execute it."""
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import urllib.request

root = Path(__file__).resolve().parents[1]
metadata = json.loads((root / 'upstream.json').read_text())
previous_digest = metadata['parsec']['sha256']
url = 'https://builds.parsec.app/package/parsec-linux.deb'
cache = root / '.cache'
cache.mkdir(exist_ok=True)
with urllib.request.urlopen(url, timeout=60) as response:
    raw = response.read(64 * 1024 * 1024 + 1)
if len(raw) > 64 * 1024 * 1024 or not raw.startswith(b'!<arch>\n'):
    raise SystemExit('Unexpected installer; metadata unchanged')
temp = cache / 'download.deb'
temp.write_bytes(raw)
control = subprocess.check_output(['bsdtar', '-xOf', str(temp), 'control.tar.zst'])
text = subprocess.run(['bsdtar', '-xOf', '-', './control'], input=control, capture_output=True, check=True).stdout.decode()
version = re.search(r'^Version: ([A-Za-z0-9._+-]+)\s*$', text, re.M).group(1)
digest = hashlib.sha256(raw).hexdigest()
metadata['parsec'].update(version=version, sha256=digest, checked=datetime.date.today().isoformat())
(root / 'upstream.json').write_text(json.dumps(metadata, indent=2) + '\n')
recipe = (root / 'PKGBUILD').read_text()
recipe = re.sub(r'^_parsecver=.*$', '_parsecver=' + version.replace('-', '_'), recipe, flags=re.M)
# Loader updates do not change the hash-pinned native module/ABI.
recipe = recipe.replace("'" + previous_digest + "'", "'" + digest + "'")
(root / 'PKGBUILD').write_text(recipe)
temp.replace(cache / 'parsec-linux.deb')
print('Official installer:', version, digest)
print('Run: make source; review the diff before building.')
