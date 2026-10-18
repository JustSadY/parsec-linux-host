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

Computers sayfasında bu host'un kartı uygulamanın kendi **This Computer / Share**
kartıdır. **Share** davet bağlantısını oluşturup uygulamanın panosuna kopyalar;
aynı kartta görünen bağlantı seçilip tekrar kopyalanabilir. Kırmızı **X / Stop
Sharing** yeni davetleri kapatır. Bu işlemler host'u yeniden başlatmaz ve mevcut
misafirleri düşürmez. Başka bilgisayarların **Connect** düğmeleri korunur.

Başka bir Parsec hesabından gelen davet isteği, uygulamanın mevcut **GUESTS →
WAITING TO CONNECT → Accept / Reject** menüsünü açar. Karar verilmeden bağlantı
kurulmaz; istek 30 saniyede sona erer. Bağlanan misafirin aynı menüsünden **Kick**
ve canlı **Gamepad / Keyboard / Mouse** izinleri yönetilir. Kendi hesabının
uzaktaki cihazı otomatik kabul edilir. Aynı Linux bilgisayarından kendi host'una
bağlanma döngüsü backend tarafından reddedilir.

**Capture GPU** ekranın bağlı olduğu karttır. **Encoding GPU** ayrı seçilir:
`Same as capture GPU` mevcut kartın kodlayıcısını kullanır; AMD/Intel seçildiğinde
monitör bağlı olmasa da VA-API kodlar. Seçimler otomatik kaydedilir; **Stop
hosting → Start hosting** ile uygulanır. Başka karta sessiz geçiş yapılmaz.

RTX masaüstü + RX 570 kodlama, kareleri sistem belleği üzerinden aktarır ve
gecikmeyi artırabilir. Bu yol sekiz bit H.264/HEVC, 4:2:0 ve Vulkan dönüşümü
kullanır; 144 FPS garantisi vermez. RX 570 VFIO ile Windows'a verilmişken Linux
kodlayıcısı olarak kullanılamaz. Ölçümler ve yöntem [GPU notlarında](docs/ENCODING-GPU.md).

İlk yönetim işlemi Polkit ile Linux yönetici doğrulaması ister. Kendi aktif,
yerel oturumunda ayar/Share işlemlerinde tekrar parola sorulmaması için bir kez:

```sh
sudo /usr/lib/parsec-linux-host/hostctl authorize-owner </dev/null
```

Bu, yalnızca kayıtlı host sahibini ve bu uygulamanın sabit yönetim yardımcısını
yetkilendiren bir yerel Polkit kuralı oluşturur. Genel sudo izni vermez. Geri almak:

```sh
sudo /usr/lib/parsec-linux-host/hostctl revoke-owner-authorization </dev/null
```

Pencereyi kapatmak host servisini durdurmaz.

Her iki GPU seçimi PCI adresiyle saklanır. `card0/card1` sırası çalışma anında çözülür; VFIO
kartları listelenmez. Seçilen GPU'ya bağlı ekran gerekir: ekranı olmayan GPU'lar
listenin sonunda "(no display connected)" notuyla görünür ama seçilemez, çünkü
yakalama ekranın bağlı olduğu GPU'da yapılır; Encoding GPU seçimi bu kısıtı taşımaz. Sürücüler, VFIO,
çekirdek parametreleri veya genel aygıt izinleri değiştirilmez.

## Arch / CachyOS build

```sh
make test
make source
makepkg -s
sudo pacman -U ./parsec-linux-host-0.4.1-1-x86_64.pkg.tar.zst
```

`makepkg` root olarak çalıştırılmaz. Paket `parsec-bin` / `parsec` ile çakışır ve
resmi istemciyi de sağlar. Derleme servis başlatmaz veya hesap girişi yapmaz.
Kaynak SHA-256 doğrulanır; native Host UI derlenip tam hash'i eşleşen vendor
modülüne uygulanır; lowlat kaynak koddan derlenir. Vendor binary veya decompile
edilmiş vendor kaynak depoya eklenmez.

Yeni kurulum varsayılanları: hosting/boot kapalı; 60 FPS, 10 Mbps, H.264, en düşük
gecikme, otomatik GPU/encoder, mevcut ekran modu, hoparlör susturma ve pano metin
paylaşımı açık; arkadaş paylaşımı kapalıdır. Paylaşım kapalıyken yalnız hesap
sahibi bağlanabilir; davet linki açıkken linke sahip bir misafir host onayıyla bağlanabilir.
Eşzamanlı istemci sayısı birdir. İsim boşsa sistem adı kullanılır. FPS bir
sınırdır, ölçülmüş sonuç veya ekran Hz ayarı değildir.

Boot hosting için **Start at Boot** seçeneğine ek olarak:

```sh
systemctl --user enable --now parsec-host-session.service
```

Sistem servisi giriş yapılmadan da başlar; seçili ekran/GPU henüz hazır değilse
bekler. Masaüstü kilitlenince servis durmaz: KMS yakalama ve uinput sistem
servisindedir; normal kilit ekranı parolası yine gerekir. Ekranı olmayan/headless
sistemler için sanal ekran eklenmez. Oturum açılmadan pano/ses gibi kullanıcı
hizmetleri hazır olmayabilir. Yeniden başlatma sonrası greeter davranışı ekran
ve sürücüye bağlıdır; canlı kilit testi soğuk açılış testinin yerini tutmaz.

Misafir onaylarını görmek için istemciyi de oturum açılışında çalıştırmak istersen:

```sh
mkdir -p ~/.config/autostart
ln -s /usr/share/applications/parsec-linux-host-integrated.desktop \
  ~/.config/autostart/parsec-linux-host.desktop
```

## GitHub / AUR

Repository: [JustSadY/parsec-linux-host](https://github.com/JustSadY/parsec-linux-host).

```sh
python3 tools/prepare-aur.py --repo https://github.com/JustSadY/parsec-linux-host
```

`dist/aur/PKGBUILD`, `.SRCINFO` ve `.install` üretilir. Kaynak arşivi `v0.4.1`
GitHub release'ine `parsec-linux-host-0.4.1.tar.gz` adıyla eklenmelidir. AUR'a
bu üç paketleme dosyasını gönder; kaynak arşivi veya proprietary binary gönderme.
AUR indirme adresi release yayımlandığında çalışır. Üretici kendiliğinden
GitHub release'i veya AUR paketi yayımlamaz.

## Implementation and limits

Only module **150-105c**, original SHA-256
`10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983`,
is admitted. Another version is rejected before patching. The official loader
comes from the separately pinned 150-104a installer; see `upstream.json`.

The C payload uses observed private function RVAs. Two sites add the Host
tab and its controls; ten confined call-site hooks adapt the existing local
computer card, sharing actions and guest menus to the Linux service. Code is RX; mutable state is in a separate RW segment.
The vendor host-start stub and capability response remain unchanged. A compiled
C worker manages public settings, friend invite sharing, approvals, per-guest permissions and fixed helper
actions; it does not render the page or implement streaming. The launcher pins
the module using bubblewrap.
Profile settings persist; module auto-updates remain pinned until a compatible
package is built. This launch is not advertised as a security sandbox.

The actual native card and guest popup have been exercised in an isolated
Xvfb session with fixture records. A real second-account end-to-end connection
and sustained video/audio/input/frame pacing still require peer testing. Windows virtual displays, resolution/orientation changes,
approved-app restrictions, desktop locking and CPU host encoding are unavailable.

For development: `make native`, then `make run` uses a disposable profile with
controller mutations disabled. The official loader must be installed. Ignored
`.cache/` contains test logs/screenshots. See [evidence](docs/IMPLEMENTATION.md).

## License

Own code and lowlat: MIT. Vendor client: proprietary Parsec/Unity terms,
not this repository's MIT license. See [third-party notices](docs/THIRD-PARTY.md).
