# Maintainer: lykia
# Local build recipe. tools/prepare-aur.py generates the release-URL variant.
pkgname=parsec-linux-host
pkgver=0.4.0
pkgrel=1
pkgdesc='Parsec client with an in-process Linux Host settings page and lowlat backend'
arch=('x86_64')
url='https://github.com/JustSadY/parsec-linux-host'
license=('MIT' 'LicenseRef-Parsec')
# GCC LTO objects from ring cannot be consumed by Rust's LLVM linker.
# The vendor ELF has added PT_LOADs; stripping its old section table can discard
# the native UI payload and invalidate appdata.json. Preserve exact ELF bytes.
options=('!lto' '!strip')
depends=('glibc' 'gcc-libs' 'systemd' 'polkit' 'bubblewrap' 'jansson'
         'libx11' 'libxcursor' 'libxi' 'libsm' 'libglvnd' 'alsa-lib' 'libjpeg-turbo'
         'libpng' 'curl' 'openssl' 'ffmpeg' 'vulkan-icd-loader' 'libva' 'libpulse')
makedepends=('cargo' 'patch' 'gcc' 'binutils' 'python' 'pkgconf' 'jansson')
optdepends=('nvidia-utils: NVIDIA NVENC encoding'
            'mesa: OpenGL conversion and AMD/Intel graphics'
            'vulkan-radeon: AMD Vulkan capture conversion'
            'vulkan-intel: Intel Vulkan capture conversion')
provides=('parsec-bin=150_105c')
conflicts=('parsec-bin' 'parsec')
backup=('etc/parsec-host/settings.json')
install=packaging/parsec-linux-host.install
_lowlat=1d2bb76d3cab31ab73dbd88ca07d2e071683a7bb
_parsecver=150_104a
_modulever=150-105c
source=("$pkgname-$pkgver.tar.gz"
        "lowlat-$_lowlat.tar.gz::https://codeload.github.com/nomi-san/lowlat/tar.gz/$_lowlat"
        "parsec-linux-$_parsecver.deb::https://builds.parsec.app/package/parsec-linux.deb"
        "parsecd-$_modulever.so::https://builds.parsec.app/channel/release/binary/linux/gz/parsecd-$_modulever.so")
noextract=("parsec-linux-$_parsecver.deb" "parsecd-$_modulever.so")
sha256sums=('0c967c22a87fca0ee8466d9bff1f4d16b52aad61573d3f4e326db2f34e119e2e'
            'e30b4af9f4030b698bd814588fa5e8c3687b12efe823fb47f1ab78c1eb1bb55d'
            'c8a4407713d72dcb1c59f082977e81e1b9e8ffd679d0666882cde4c8a0821dd2'
            '10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983')

prepare() {
    # Extraction overwrites files but keeps ones a previous run's patches
    # created, so start from the pinned archive every time.
    rm -rf "$srcdir/lowlat-$_lowlat"
    bsdtar -xf "$srcdir/lowlat-$_lowlat.tar.gz" -C "$srcdir"
    cd "$srcdir/lowlat-$_lowlat"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0001-local-owner-access.patch"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0002-probe-context-lifetime.patch"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0003-advertise-share-secret.patch"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0004-refuse-same-machine-guest.patch"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0005-live-sharing-and-approval.patch"
    patch -Np1 -i "$srcdir/$pkgname-$pkgver/patches/0006-separate-encoding-gpu.patch"
    mkdir -p "$srcdir/vendor-deb"
    bsdtar -xf "$srcdir/parsec-linux-$_parsecver.deb" -C "$srcdir/vendor-deb"
    bsdtar -xf "$srcdir/vendor-deb/data.tar.zst" -C "$srcdir/vendor-deb"
}

build() {
    python "$srcdir/$pkgname-$pkgver/tools/build-native.py" \
        --module "$srcdir/parsecd-$_modulever.so" --output "$srcdir/native-build"
    # The source archive's files carry a fixed 1970 timestamp, so objects
    # kept from an earlier run always look newer; build them from scratch.
    rm -rf "$srcdir/runtime-build"
    make -C "$srcdir/$pkgname-$pkgver/runtime" OUT="$srcdir/runtime-build" all
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
    make -C "$srcdir/$pkgname-$pkgver/runtime" OUT="$srcdir/runtime-build" test
    cc -std=c11 -O2 -Wall -Wextra "$srcdir/$pkgname-$pkgver/tests/native.c" -o "$srcdir/native-tests"
    "$srcdir/native-tests"
    cd "$srcdir/lowlat-$_lowlat"
    CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$srcdir/target}" \
        cargo test --locked -p lowlatd --bin lowlatd
    CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$srcdir/target}" \
        cargo test --locked -p lowlat-core --lib
    CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$srcdir/target}" \
        cargo test --locked -p lowlat-capture --lib
}

package() {
    local project="$srcdir/$pkgname-$pkgver"
    local lib="$pkgdir/usr/lib/$pkgname"
    install -d "$lib/vendor" "$pkgdir/usr/bin" "$pkgdir/etc/parsec-host"
    install -m755 "${CARGO_TARGET_DIR:-$srcdir/target}/release/lowlatd" "$lib/lowlatd"
    install -m755 "$srcdir/runtime-build/hostctl" "$srcdir/runtime-build/service-runner" "$lib/"
    install -m755 "$srcdir/runtime-build/parsec-linux-host" "$pkgdir/usr/bin/parsec-linux-host"
    install -m755 "$project/packaging/parsecd" "$pkgdir/usr/bin/parsecd"
    install -m755 "$srcdir/vendor-deb/usr/bin/parsecd" "$lib/vendor/parsecd"
    install -m644 "$srcdir/native-build/parsecd-$_modulever.so" \
        "$srcdir/native-build/appdata.json" "$lib/vendor/"
    cp -r "$srcdir/vendor-deb/usr/share" "$pkgdir/usr/"
    rm -rf "$pkgdir/usr/share/parsec/skel"
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
