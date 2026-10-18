# Parsec Linux Host

Native Qt settings in the classic Parsec layout, an embedded official Linux
client, a lowlat hosting backend, and an Arch Linux `PKGBUILD`.

**This is an independent integration. Parsec is proprietary: this repository
does not contain or patch its source, and does not add a host engine to the
vendor binary.** The native settings belong to this project; hosting uses
patched, source-built lowlat. No Electron, WebView, or browser runtime is used.

## Türkçe kullanım

Proje Masaüstü'nde bağımsız bir Git deposu olarak hazırlanmıştır. Önizleme:

```sh
make preview
```

Bu komut çalışan Parsec/host oturumuna dokunmaz. Yeni paketi oluşturmak için:

```sh
make test
make source
makepkg
```

`makepkg` root olarak çalıştırılmaz; derleme sırasında servis etkinleştirilmez,
hesap girişi yapılmaz, GPU bağlanmaz/ayrılmaz. Resmi Parsec yükleyicisi ve
belirli lowlat commit'i indirilir, SHA-256 doğrulanır, yalnızca lowlat yamaları
uygulanıp derlenir. Oluşan `.pkg.tar.zst` paketi Parsec istemcisini de içerir ve
`parsec-bin`/`parsec` paketleriyle çakışır. Kurulumu ayrı bir adımdır:

```sh
sudo pacman -U ./parsec-linux-host-0.1.0-1-x86_64.pkg.tar.zst
```

Kurulumdan sonra **Parsec Linux Host → Settings → Account** bölümünde Host
hesabını bağla. Mevcut resmi istemci profili korunur; onun oturum dosyaları
okunmaz veya dışa aktarılmaz. **Host → Hosting Enabled** yayını açar/kapatır.
Yönetim işlemleri Polkit ile Linux yönetici doğrulaması ister.

**Client** bölümündeki Hardware/GPU ve Software/CPU seçimi video çözümlemesini
kontrol eder. Renderer OpenGL veya deneysel Vulkan olabilir. **Host** bölümünde
GPU/monitör ve otomatik/NVENC/VA-API kodlayıcı seçilir; CPU host kodlaması bu
sürümde yoktur. Encoder yakalanan ekranla aynı GPU'da çalışır; ayrı bir encode
GPU'suna çapraz kopyalama seçeneği sunulmaz.

GPU'lar PCI adresiyle saklanır; `card0/card1` numaraları çalışma anında çözülür.
VFIO'ya bağlı kartlar seçime alınmaz. Sürücü değiştirme, kernel parametresi,
GPU blacklist veya genel aygıt izni değişikliği yapılmaz. Bağlı monitörü olmayan
bir GPU'yu başlatma isteği hata verir; CPU'ya sessiz geri dönüş yapılmaz.

## Defaults and supported settings

Documented vendor defaults are used where there is a direct mapping:
60 FPS target, 10 Mbps, empty host name → system hostname, lowest latency,
OpenGL client renderer, hardware client decoding, H.264 compatibility default,
constant FPS off, local speaker mute on, clipboard text sharing on.

Hosting and boot startup are opt-in on a fresh Linux installation. Access is
limited to the account owner, with one simultaneous client. These are deliberate
project policies, rather than a claim that every Windows default is reproduced.
An FPS setting is a limit, not a measured frame rate or an automatic display
refresh change.

Resolution/orientation are shown in the familiar layout but remain on the
current Linux display mode. Their controls and automatic desktop locking are
disabled, with tooltips, because they are not implemented by this integration.
Windows-only virtual display and approved-app restrictions are not advertised
as working. See [support and evidence](docs/IMPLEMENTATION.md).

## GitHub and AUR

This directory is the source repository; caches, account files, vendor binaries,
build outputs and screenshots are excluded from Git. No credentials or local
machine identifiers are part of the project configuration.

To prepare the AUR directory after creating your real GitHub repository:

```sh
python3 tools/prepare-aur.py --repo https://github.com/YOUR-ACCOUNT/parsec-linux-host
```

Use your actual repository URL. Upload the generated deterministic
`parsec-linux-host-0.1.0.tar.gz` as a **v0.1.0 release asset**, then submit
`dist/aur/PKGBUILD`, `dist/aur/.SRCINFO` and the `.install` file to AUR.
The AUR recipe downloads the release archive; it does not bundle source or a
proprietary binary inside the AUR Git repository. Nothing is published by the
generator. A GitHub repository URL and release must exist before the generated
recipe is usable from AUR.

The official installer can lag behind its own auto-update module. On
2026-10-18 the official download was **150-104a**, while the existing signed-in
client had already downloaded module **150-105c**. Both are recorded in
`upstream.json`. To refresh the installer without executing it:

```sh
python3 tools/refresh-upstream.py
make source
git diff
```

A changed upstream hash fails the existing build. Review updates and regenerate
the pin explicitly. The code does not patch version-dependent binary offsets.

## Existing local prototype

An earlier manual `Parsec + Host` prototype uses `/usr/local/lib/parsec-linux-host`
and `parsec-linux-host.service`. This package uses `/usr/lib/parsec-linux-host`,
`/etc/parsec-host`, and `parsec-host.service`. **Stop the old host and its session
helper before starting the new host** to avoid two independent capture/input
services. The package uses a separate `/run/parsec-host/session` control socket.
Close the old launcher before embedding the vendor client in the new UI.
Old account/session files are never copied automatically. The project preview
does not replace or stop that installation.

For boot hosting, enable Start at Boot and enable the helper for your current
graphical user: `systemctl --user enable parsec-host-session.service`.
Account sessions may expire and require a new login. Closing the GUI leaves
hosting running; use Hosting Enabled → Disabled to stop it. Saving settings
does not interrupt a live stream; restart hosting to apply them.

## License and sources

Project integration and lowlat: MIT. Official Parsec client: proprietary vendor
terms, separately downloaded; see [third-party notices](docs/THIRD-PARTY.md).
Independent project, not affiliated with Parsec/Unity.

- [lowlat source](https://github.com/nomi-san/lowlat)
- [Official Linux client](https://support.parsec.app/hc/en-us/articles/32381494397332-Parsec-App-for-Linux)
- [Official advanced defaults and settings](https://support.parsec.app/hc/en-us/articles/32381443626516-All-Advanced-Configuration-Options)
- [Arch package guidelines](https://wiki.archlinux.org/title/Arch_package_guidelines)
