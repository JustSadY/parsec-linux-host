#!/usr/bin/python3
"""Create a deterministic source archive and bind the local PKGBUILD to its hash."""
import gzip
import hashlib
import io
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.3.0'


def main():
    files = []
    for name in ('runtime', 'native', 'packaging', 'patches', 'tests', 'tools', 'docs', '.github'):
        files += [p for p in (ROOT / name).rglob('*') if p.is_file()
                  and '__pycache__' not in p.parts and p.suffix not in ('.pyc', '.png')]
    files += [ROOT / name for name in ('LICENSE', 'README.md', 'upstream.json', 'Makefile', '.gitignore', '.gitattributes')]
    archive = ROOT / f'parsec-linux-host-{VERSION}.tar.gz'
    with archive.open('wb') as output, gzip.GzipFile(fileobj=output, mode='wb', mtime=0, filename='') as gz:
        with tarfile.open(fileobj=gz, mode='w') as tar:
            for path in sorted(files):
                info = tarfile.TarInfo(f'parsec-linux-host-{VERSION}/' + path.relative_to(ROOT).as_posix())
                content = path.read_bytes()
                info.size, info.mtime, info.mode = len(content), 0, 0o644
                tar.addfile(info, io.BytesIO(content))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    recipe = ROOT / 'PKGBUILD'
    recipe.write_text(re.sub(r"sha256sums=\('[0-9a-f]{64}'", f"sha256sums=('{digest}'", recipe.read_text(), count=1))
    print(archive.name, digest)


if __name__ == '__main__':
    main()
