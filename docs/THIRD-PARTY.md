# Third-party components

New native UI code, control integration and packaging: MIT. lowlat: MIT;
its full notice is preserved in LICENSE.lowlat and packages. The build pins the
lowlat revision in `upstream.json` and `PKGBUILD` rather than assuming that
future upstream revisions will remain compatible.

The official Parsec loader and module are proprietary Parsec/Unity software,
not covered by this project's MIT license. They are downloaded from the vendor
HTTPS server with explicit hashes during package construction. The build output
is patched; vendor binary and decompiled source are not committed. Modification
does not imply endorsement, permission beyond the applicable vendor terms or
official Linux hosting support. The loader and module versions are separately
pinned (`150-104a` and `150-105c`); newer modules need a compatibility review,
not just a URL or hash update. Vendor terms:
https://unity.com/legal/parsec-additional-terms.

Upstream: https://github.com/nomi-san/lowlat.
Loader: https://builds.parsec.app/package/parsec-linux.deb.
Module: https://builds.parsec.app/channel/release/binary/linux/gz/parsecd-150-105c.so.
