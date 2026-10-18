# Maintainer: lykia
# Local build recipe. tools/prepare-aur.py generates the release-URL variant.
pkgname=parsec-linux-host
pkgver=0.1.0
pkgrel=1
pkgdesc='Native Linux host settings, lowlat backend, and official Parsec client'
arch=('x86_64')
url='https://github.com/nomi-san/lowlat'
license=('MIT' 'LicenseRef-Parsec')
# GCC LTO objects from ring cannot be consumed by Rust's LLVM linker.
options=('!lto')
depends=('glibc' 'gcc-libs' 'systemd' 'polkit' 'python' 'pyside6' 'python-xlib'
         'libx11' 'libxcursor' 'libxi' 'libsm' 'libglvnd' 'alsa-lib' 'libjpeg-turbo'
         'libpng' 'curl' 'openssl' 'ffmpeg' 'vulkan-icd-loader' 'libva' 'libpulse')
makedepends=('cargo' 'patch')
optdepends=('nvidia-utils: NVIDIA NVENC encoding'
            'mesa: OpenGL conversion and AMD/Intel graphics'
            'vulkan-radeon: AMD Vulkan capture conversion'
            'vulkan-intel: Intel Vulkan capture conversion')
provides=('parsec-bin=150_104a')
conflicts=('parsec-bin' 'parsec')
backup=('etc/parsec-host/settings.json')
install=packaging/parsec-linux-host.install
_lowlat=1d2bb76d3cab31ab73dbd88ca07d2e071683a7bb
_parsecver=150_104a
source=("$pkgname-$pkgver.tar.gz"
        "lowlat-$_lowlat.tar.gz::https://codeload.github.com/nomi-san/lowlat/tar.gz/$_lowlat"
        "parsec-linux-$_parsecver.deb::https://builds.parsec.app/package/parsec-linux.deb")
noextract=("parsec-linux-$_parsecver.deb")
sha256sums=('9dd5560f821acf3a0e43aab1c6d502a5d5960d5c73e2c57a304a9f7f54b45b5d'
            'e30b4af9f4030b698bd814588fa5e8c3687b12efe823fb47f1ab78c1eb1bb55d'
            'c8a4407713d72dcb1c59f082977e81e1b9e8ffd679d0666882cde4c8a0821dd2')

prepare() {
    cd "$srcdir/lowlat-$_lowlat"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0001-local-owner-access.patch"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0002-probe-context-lifetime.patch"
    mkdir -p "$srcdir/vendor-deb"
    bsdtar -xf "$srcdir/parsec-linux-$_parsecver.deb" -C "$srcdir/vendor-deb"
    bsdtar -xf "$srcdir/vendor-deb/data.tar.zst" -C "$srcdir/vendor-deb"
}

build() {
    cd "$srcdir/lowlat-$_lowlat"
    # A distributable x86_64 build must not inherit CachyOS target-cpu=native.
    export RUSTFLAGS="${RUSTFLAGS:-} -C target-cpu=x86-64"
    export CFLAGS="${CFLAGS:-} -march=x86-64 -mtune=generic"
    CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$srcdir/target}" \
        cargo build --release --locked -p lowlatd --bin lowlatd
}

check() {
    export RUSTFLAGS="${RUSTFLAGS:-} -C target-cpu=x86-64"
    export CFLAGS="${CFLAGS:-} -march=x86-64 -mtune=generic"
    cd "$srcdir/$pkgname-$pkgver"
    PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=app QT_QPA_PLATFORM=offscreen \
        python -m unittest discover -s tests -v
    cd "$srcdir/lowlat-$_lowlat"
    CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$srcdir/target}" \
        cargo test --locked -p lowlatd --bin lowlatd
    CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$srcdir/target}" \
        cargo test --locked -p lowlat-core --lib
}

package() {
    local project="$srcdir/$pkgname-$pkgver"
    local lib="$pkgdir/usr/lib/$pkgname"
    install -d "$lib/vendor" "$pkgdir/usr/bin" "$pkgdir/etc/parsec-host"
    cp -r "$project/app/parsec_host" "$lib/"
    find "$lib" -type d -name __pycache__ -prune -exec rm -rf {} +
    install -m755 "${CARGO_TARGET_DIR:-$srcdir/target}/release/lowlatd" "$lib/lowlatd"
    install -m755 "$project/packaging/hostctl" "$project/packaging/service" "$lib/"
    install -m755 "$project/packaging/launch" "$pkgdir/usr/bin/parsec-linux-host"
    install -m755 "$project/packaging/parsecd" "$pkgdir/usr/bin/parsecd"
    install -m755 "$srcdir/vendor-deb/usr/bin/parsecd" "$lib/vendor/parsecd"
    cp -r "$srcdir/vendor-deb/usr/share" "$pkgdir/usr/"
    rm -f "$pkgdir/usr/share/applications/parsecd.desktop"
    install -Dm644 "$project/packaging/parsec-linux-host-integrated.desktop" \
        "$pkgdir/usr/share/applications/parsec-linux-host-integrated.desktop"
    install -Dm644 "$project/packaging/systemd/parsec-host.service" \
        "$pkgdir/usr/lib/systemd/system/parsec-host.service"
    install -Dm644 "$project/packaging/systemd/parsec-host-session.service" \
        "$pkgdir/usr/lib/systemd/user/parsec-host-session.service"
    install -Dm644 "$project/packaging/org.parsec-linux-host.manage.policy" \
        "$pkgdir/usr/share/polkit-1/actions/org.parsec-linux-host.manage.policy"
    install -m644 "$project/packaging/defaults.json" "$pkgdir/etc/parsec-host/settings.json"
    install -Dm644 "$project/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    install -Dm644 "$project/docs/LICENSE.lowlat" "$pkgdir/usr/share/licenses/$pkgname/lowlat"
    install -Dm644 "$project/docs/THIRD-PARTY.md" "$pkgdir/usr/share/licenses/$pkgname/Parsec"
    install -Dm644 "$project/README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
}
