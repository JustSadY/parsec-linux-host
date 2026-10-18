#!/usr/bin/python3
"""Generate an AUR upload directory after a real GitHub repository URL is known."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--repo', required=True, help='https://github.com/OWNER/REPOSITORY')
args = parser.parse_args()
repo = args.repo.removesuffix('/').removesuffix('.git')
if not re.fullmatch(r'https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
    raise SystemExit('A concrete GitHub repository URL is required')
root = Path(__file__).resolve().parents[1]
subprocess.run(['python3', str(root / 'tools/prepare-source.py')], check=True)
recipe = (root / 'PKGBUILD').read_text()
recipe = re.sub(r"^url=.*$", "url='" + repo + "'", recipe, flags=re.M)
recipe = recipe.replace('"$pkgname-$pkgver.tar.gz"', '"$pkgname-$pkgver.tar.gz::' + repo + '/releases/download/v$pkgver/$pkgname-$pkgver.tar.gz"', 1)
recipe = recipe.replace('install=packaging/parsec-linux-host.install', 'install=parsec-linux-host.install')
dest = root / 'dist/aur'
dest.mkdir(parents=True, exist_ok=True)
(dest / 'PKGBUILD').write_text(recipe)
shutil.copyfile(root / 'packaging/parsec-linux-host.install', dest / 'parsec-linux-host.install')
with (dest / '.SRCINFO').open('w') as output:
    subprocess.run(['makepkg', '--printsrcinfo'], cwd=dest, stdout=output, check=True)
print('Generated:', dest)
print('Upload the source archive as the v0.2.0 GitHub release asset BEFORE submitting the AUR recipe.')
