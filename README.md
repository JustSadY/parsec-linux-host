# Parsec Linux Host

Adds **Settings → Host inside the actual Parsec Linux client**, using a compiled
C extension and the client's existing native drawing functions. Hosting uses
the source-built [lowlat backend](https://github.com/nomi-san/lowlat).

The previous Qt window wrapper has been removed. No Qt, Electron, WebView,
foreign-window embedding or second settings application is used in this version.
This is an independent, experimental integration, not an official Parsec release.

**Status (0.4.1, October 18, 2026):** This release targets Arch Linux and
CachyOS on x86_64. The graphical client requires an X11 or XWayland session.
The Linux host captures a connected physical display through KMS; it does not
create a virtual or headless display. A real second-account end-to-end
connection and sustained streaming reliability have not yet been verified.
The UI and local encoding tests below do not guarantee operation on other
systems or official Parsec compatibility.

## Usage

Open **Parsec Linux Host** from the application menu, or run
`parsec-linux-host`. In the left sidebar, select **Settings → Host**. On first
setup, **Connect Host account** opens a terminal for your Parsec email and
password. You can also sign in directly from a terminal:

```sh
parsec-linux-host login
```

The password is not echoed or passed as a command-line argument. The Host
session is stored as root-owned `/etc/parsec-host/account.json` with mode 0600.
The official client login and lowlat Host login are separate sessions. Your
existing `~/.parsec` client profile is preserved; this project's code does not
read or copy its credentials.

The Host page provides name, display, GPU, NVENC/VA-API, Vulkan/OpenGL
conversion, FPS, bandwidth, codec, quality, constant FPS, speaker muting,
clipboard sharing and boot startup settings. Changes **save automatically**;
there is no separate Save button. Use **Stop hosting → Start hosting** to apply
capture/encoding changes. Saving **Start at Boot** updates the system unit's
enabled state; **Share / Stop Sharing** takes effect immediately on a running
host.

While hosting runs, the local computer appears in the native Computers grid as
**This Computer / Share**. **Share** generates an invitation link and copies it
through the application's clipboard. You can also select and copy the link
displayed on the card. The red **X / Stop Sharing** disables new invitations.
These actions do **not restart** a running host or disconnect existing guests.
If the host is stopped, selecting **Share** first starts the service. The
**Connect** buttons for other computers remain unchanged.

An invitation request from another Parsec account opens the native **GUESTS →
WAITING TO CONNECT → Accept / Reject** menu. Admission is blocked until a
decision is made; requests expire after 30 seconds. The connected guest's menu
also provides **Kick** and live **Gamepad / Keyboard / Mouse** permissions.
Remote devices signed in to the Host owner's account are admitted automatically.
The backend refuses a same-machine guest loop to its own Linux host.

**Capture GPU** selects the card with the connected display. **Encoding GPU**
is independent: `Same as capture GPU` uses that card's encoder, while selecting
a supported AMD/Intel GPU enables VA-API encoding even without a monitor on
that GPU. Selections save automatically; apply them with **Stop hosting →
Start hosting**. No silent fallback to another GPU is performed.

Capturing on an RTX GPU and encoding on an RX 570 transfers frames through
system memory and may increase latency. This path uses eight-bit H.264/HEVC,
4:2:0 and Vulkan conversion; it does not guarantee 144 FPS. An RX 570 assigned
to a Windows VM through VFIO is unavailable to the Linux encoder. See the
[GPU measurements and method](docs/ENCODING-GPU.md).

The first management operation requires Linux administrator authentication
through Polkit. To allow passwordless settings and Share operations from the
registered owner's active local session, explicitly opt in once:

```sh
sudo /usr/lib/parsec-linux-host/hostctl authorize-owner </dev/null
```

This creates a local Polkit rule restricted to the registered Host owner and
this application's fixed management helper. It does not grant general sudo
access. To revoke it:

```sh
sudo /usr/lib/parsec-linux-host/hostctl revoke-owner-authorization </dev/null
```

Closing the client window does not stop the Host service.

Both GPU selections are stored by PCI address; `card0/card1` numbering is
resolved at runtime. VFIO-bound devices are excluded. Capture requires a
connected display: GPUs marked `(no display connected)` appear at the end of
the list but cannot be selected for capture. Encoding GPU does not have this
restriction. This project does not alter drivers, VFIO assignments, kernel
parameters or global device permissions.

## Arch / CachyOS build

Prerequisites: Arch Linux or CachyOS x86_64, network access, `base-devel`,
Rust/Cargo, Python and the `jansson`, `libcurl`, `libsystemd` and OpenSSL
development files needed by the C tests. On a clean system, for example:

```sh
sudo pacman -S --needed base-devel git rust python jansson curl openssl systemd
```

From the repository directory, as a regular user:

```sh
make test
make source
makepkg -s
sudo pacman -U ./parsec-linux-host-0.4.1-1-x86_64.pkg.tar.zst
```

Do not run `makepkg` as root. This package conflicts with `parsec-bin` and
`parsec`, and includes the official client. Building does not start services or
sign in to an account. `make source` regenerates the source archive from the
current files and updates its SHA-256 in the local `PKGBUILD`; `makepkg` then
verifies that hash. The native Host UI is compiled and applied only to the
exact hash-matched vendor module, while lowlat is built from source. Neither
vendor binaries nor decompiled vendor source are committed to this repository.

Fresh-install defaults: hosting and boot startup off; 60 FPS, 10 Mbps, H.264,
lowest latency, automatic GPU/encoder, current display mode, speaker muting and
text clipboard sharing on; friend invitations off. With sharing disabled,
only the account owner may connect. An invitation link permits a guest to
request access, subject to Host approval. Only one concurrent client is
supported. An empty Host name uses the system hostname. The FPS setting is a
ceiling, not measured performance or a display refresh-rate setting.

**Start at Boot** enables the system service for subsequent boots. The Host
account must be connected and a local owner registered. The service waits if
the configured GPU/display is not yet available. For desktop-session features
such as audio and clipboard, the per-user session helper is also recommended:

```sh
systemctl --user enable --now parsec-host-session.service
```

The system service can start before a user signs in. Locking the desktop does
not stop the service: KMS capture and uinput run in the system service, and
normal lock-screen authentication remains required. No virtual display is
created for headless systems. Session services such as clipboard and audio
may be unavailable before login. Greeter behavior after a cold boot depends
on the GPU/display stack; testing an already-running locked session is not a
substitute for a cold-boot test. Capturing a locally locked KDE Wayland desktop
was verified on one machine; remote lock-screen interaction was not verified.

To see guest approvals in the client, optionally start the client at login:

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

The generator creates `dist/aur/PKGBUILD`, `.SRCINFO` and `.install`, and
recalculates the local `PKGBUILD` source hash. Upload the source archive as
`parsec-linux-host-0.4.1.tar.gz` to a GitHub release tagged `v0.4.1`. Submit
only those three packaging files to the AUR, not the source archive or
proprietary binaries. The AUR download URL becomes usable only after that
release is published. The generator does not publish a GitHub release or an
AUR package. **As of October 25, 2026, this repository has no `v0.4.1` Git
tag**; do not treat an AUR release as ready yet.

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
and sustained video/audio/input/frame pacing still require peer testing.
Windows virtual displays, resolution/orientation changes, approved-app
restrictions and CPU host encoding are unavailable. Capture continuing through
a **locally locked KDE desktop** was verified on one test machine, but remote
lock-screen keyboard interaction and cold-boot greeter access were **not tested**.

For development: `make native`, then `make run` uses a disposable profile with
controller mutations disabled. The official loader must be installed. Ignored
`.cache/` contains test logs/screenshots. See [evidence](docs/IMPLEMENTATION.md).

## License

Own code and lowlat: MIT. Vendor client: proprietary Parsec/Unity terms,
not this repository's MIT license. See [third-party notices](docs/THIRD-PARTY.md).
