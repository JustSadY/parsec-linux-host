# Parsec Linux Host

Adds **Settings → Host inside the actual Parsec Linux client**, using a compiled
C extension and the client's existing native drawing functions. Hosting uses
the source-built [lowlat backend](https://github.com/nomi-san/lowlat).

The previous Qt window wrapper has been removed. No Qt, Electron, WebView,
foreign-window embedding or second settings application is used in this version.
This is an independent, experimental integration, not an official Parsec release.

## Türkçe kullanım

Uygulama menüsünden **Parsec Linux Host** aç veya `parsec-linux-host` çalıştır.
Sol taraftaki dişli → **Host**. İlk kurulumda **Connect Host account** bir terminal
açar; Parsec e-posta/parolanı orada gir. Doğrudan terminalden de yapılabilir:

```sh
parsec-linux-host login
```

Parola ekrana veya komut argümanına konmaz. Host oturumu root'a ait
`/etc/parsec-host/account.json` içinde 0600 izinle saklanır. Resmi istemcinin
girişi ve lowlat Host girişi ayrı oturumlardır. Var olan `~/.parsec` istemci
profilin korunur; proje kodu buradaki kimlik bilgilerini okumaz veya kopyalamaz.

Host sayfasında isim, ekran, GPU, NVENC/VA-API, Vulkan/OpenGL dönüşümü, FPS,
bant genişliği, codec, kalite, sabit FPS, hoparlör susturma, pano paylaşımı ve
boot başlangıcı bulunur. Değişiklikler **otomatik kaydedilir** (ayrı bir Kaydet
düğmesi yoktur); uygulamak için **Stop hosting → Start hosting** yap.

Hosting açıkken **Hosting Enabled** satırının altında canlı yayın bilgisi
(çözünürlük, FPS, bitrate, codec) ve bağlı misafirler listelenir. Her misafirin
yanında hesap türü (kendi hesabın / davetli) ve bağlantı durumu görünür;
**Kick** o misafiri çıkarır. Liste lowlatd'nin yerel oturum kanalından okunur,
yönetici doğrulaması istemez.

Arkadaşını davet etmek için **Friend Access → Create share link**: bir
`https://parsec.gg/g/...` davet linki üretilip panoya kopyalanır. Link, host'u
yalnız o linke sahip misafirin bağlanabileceği davetli moduna alır; **Copy
link** yeniden kopyalar, **Stop sharing** kapatır. Kendi bilgisayarına "Connect"
ile bağlanmaya gerek yoktur — paylaşım bu link iledir. Aynı bilgisayardan
kendi host'una bağlanma denemesi (ekranı kendine yansıtan döngü) host tarafında
reddedilir ve günlüğe yazılır; Computers listesindeki kart yine **Connect**
gösterir, ama bağlantı kurulmaz.

Yönetim işlemleri Polkit ile Linux yönetici doğrulaması ister. Pencereyi
kapatmak host servisini durdurmaz.

GPU PCI adresiyle saklanır. `card0/card1` sırası çalışma anında çözülür; VFIO
kartları listelenmez. Seçilen GPU'ya bağlı ekran gerekir. Sürücüler, VFIO,
çekirdek parametreleri veya genel aygıt izinleri değiştirilmez.

## Arch / CachyOS build

```sh
make test
make source
makepkg -s
sudo pacman -U ./parsec-linux-host-0.2.0-1-x86_64.pkg.tar.zst
```

`makepkg` root olarak çalıştırılmaz. Paket `parsec-bin` / `parsec` ile çakışır ve
resmi istemciyi de sağlar. Derleme servis başlatmaz veya hesap girişi yapmaz.
Kaynak SHA-256 doğrulanır; native Host UI derlenip tam hash'i eşleşen vendor
modülüne uygulanır; lowlat kaynak koddan derlenir. Vendor binary veya decompile
edilmiş vendor kaynak depoya eklenmez.

Yeni kurulum varsayılanları: hosting/boot kapalı; 60 FPS, 10 Mbps, H.264, en düşük
gecikme, otomatik GPU/encoder, mevcut ekran modu, hoparlör susturma ve pano metin
paylaşımı açık; arkadaş paylaşımı kapalıdır. Paylaşım kapalıyken yalnız hesap
sahibi bağlanabilir; davet linki açıkken linke sahip bir misafir de bağlanabilir.
Eşzamanlı istemci sayısı birdir. İsim boşsa sistem adı kullanılır. FPS bir
sınırdır, ölçülmüş sonuç veya ekran Hz ayarı değildir.

Boot hosting için **Start at Boot** seçeneğine ek olarak:

```sh
systemctl --user enable --now parsec-host-session.service
```

## GitHub / AUR

Repository: [JustSadY/parsec-linux-host](https://github.com/JustSadY/parsec-linux-host).

```sh
python3 tools/prepare-aur.py --repo https://github.com/JustSadY/parsec-linux-host
```

`dist/aur/PKGBUILD`, `.SRCINFO` ve `.install` üretilir. Kaynak arşivi `v0.2.0`
GitHub release'ine `parsec-linux-host-0.2.0.tar.gz` adıyla eklenmelidir. AUR'a
bu üç paketleme dosyasını gönder; kaynak arşivi veya proprietary binary gönderme.
AUR indirme adresi release yayımlandığında çalışır. Üretici kendiliğinden
GitHub release'i veya AUR paketi yayımlamaz.

## Implementation and limits

Only module **150-105c**, original SHA-256
`10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983`,
is admitted. Another version is rejected before patching. The official loader
comes from the separately pinned 150-104a installer; see `upstream.json`.

The C payload uses observed private function RVAs. Two exact sites add the Host
tab and its real controls. Code is RX; mutable state is in a separate RW segment.
The vendor host-start stub and capability response remain unchanged. A compiled
C worker manages public settings, friend invite sharing and fixed helper
actions; it does not render the page or implement streaming. The launcher pins
the module using bubblewrap.
Profile settings persist; module auto-updates remain pinned until a compatible
package is built. This launch is not advertised as a security sandbox.

Private ABI compatibility and sustained video/audio/input/frame pacing require
real peer testing. Windows virtual displays, resolution/orientation changes,
approved-app restrictions, desktop locking and CPU host encoding are unavailable.

For development: `make native`, then `make run` uses a disposable profile with
controller mutations disabled. The official loader must be installed. Ignored
`.cache/` contains test logs/screenshots. See [evidence](docs/IMPLEMENTATION.md).

## License

Own code and lowlat: MIT. Vendor client: proprietary Parsec/Unity terms,
not this repository's MIT license. See [third-party notices](docs/THIRD-PARTY.md).
