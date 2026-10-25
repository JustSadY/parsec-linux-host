# Native Host integration, 0.4.1

This document describes the hash-pinned implementation at commit `de23106`
(2026-10-18), together with the local verification recorded that day. It is a
technical snapshot, **not** evidence of successful external peer playback or a
claim that newer official Parsec modules are compatible. The README gives the
user-facing setup and support boundaries.

The runtime is compiled C (`runtime/`). The extension runs inside the actual
vendor client, calls its native ImGui helpers and shares ABI 8 through
`native/bridge.h`. Python is build/test tooling only.

## Artifact and static evidence

Official module 150-105c, SHA-256
`10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983`.
Ghidra image base is `0x100000`; build scripts and the table below use ELF RVAs.
These are decompiler/assembly observations, not original source.

| Observed code | RVA | Adaptation |
|---|---|---|
| Settings constructor, omits Host | `0x85010` | Add native Host tab at call `0x86164` |
| Host dispatcher, controls absent | `0x3b2d0` | Render Linux capture controls at `0x3cee8` |
| Desktop host-list parser | `0x4f120` | Wrap `0x5d1c3`; merge exact service registration into native list, `self` byte `0x2f5` |
| Computer card, local crown and This Computer/Share | `0x53e20` | Wrap call `0x5edbc`; mark only exact service peer local for this draw |
| Card action / secret / revoke | `0x382d0`, `0x83ed0`, `0x340b0` | Three card-only hooks dispatch service actions, bypass unsupported SDK paths |
| Native clipboard | `0x1c9ac0` | Copy generated link through the app's own clipboard |
| Guest strip + Accept/Reject/permissions/Kick | `0x527c0` | Wrap call `0x5e3f1`, supply native guest records via call `0x52999` |
| Host command RPC | `0x69830` | Three guest-strip-only hooks translate commands into owner-gated IPC |

Retained REA evidence:

- Settings constructor: `ev_a387b68d8520361af30cd9159d6759b0f47ffdb774eb166cc953aa8f760907c8`.
- Settings dispatcher: `ev_dc2239964371d5f50bc33e31218e4fabd478d46dbe00810a1c50832760c8e127`.
- Card renderer: `ev_cea226634c8f064f4f8df42124e49e3eb1efc05c83787bc29034313dd51b5772`.
- Native button: `ev_c133db020c50ee47f018bd1a82df7c2ceff620b9aca419b0535dcf7fc253f195`.
- Guest menu: `ev_6bd34b3c061596b7418a7ae3bd41cef0dfb3053ab664ef221a40ab9f37fcaa88`.
- Guest assembly: `ev_9c5230bcc6f8381e3ccc7c059d5550368d2f84a9183281d64cd99b06e66cff1c`.
- Host command assembly: `ev_b753826992ecd5c80c29211d7e663b1d6dbaec814169cb427115a6a7d408c3c4`.

The original module hash and original call target are checked before every
patch. Code/state use separate RX/RW loads. The original capability builder
`0x2cc60` and unconditional -5 host-start stub `0x88440` are preserved. Streaming
is lowlat, with actual native Parsec UI paths reused where they exist. Original
unwind metadata is not extended; private ABI compatibility remains a limit.

## Identity and invitation

The client login and lowlat Host login are separate sessions. The service fetches
its own registration with its root-only Host session. Only an explicit self flag
or exact machine-id match can select the peer; a one-host account is not enough.
The public peer cache at `/run/parsec-host/host-peer.json` allows the native client to identify the exact local card
without exposing the Host session. The verified local registration is merged into
the native list if the separate client session has not received it yet, and
removed when hosting stops. Display names/account ownership are not used
as local identity. A private copy of the host record is modified for drawing.

The invitation uses the native `https://parsec.gg/g/<peer>/<secret>/` path.
Stock Join parses exactly eight secret characters, so RAND_bytes produces four
random bytes encoded as eight hex characters. Legacy longer secrets fail closed.
Holding a link permits requesting access, not automatic non-owner admission.
The secret/link file is root-owned, readable only to the desktop owner's group;
the UI bridge containing the link is 0600 in a 0700 runtime directory. It never
contains account passwords or sessions. No external clipboard command is needed.

Patch 0005 watches the fixed root-owned share file every 250 ms. Secret/policy
updates change the advertisement without restarting capture or dropping guests.
A missing, malformed, untrusted or invalid share file closes invited admission.
The owner is automatic; every non-owner waits for an explicit decision, even if
the relay's skip-approval flag is set. At most eight requests wait for 30 seconds;
monotonic u64 decision IDs prevent stale/replayed decisions. Cancellation removes
the request. P2P/admission begins only after acceptance. Same-machine established
paths are refused by patch 0004.

## Local control boundary

The unprivileged C worker validates editable settings and sends fixed root-helper
actions through Polkit. Terminal login sends credentials over stdin; the session
is stored root-only and redacted from runner output. Privileged execution uses
installed, fixed-path `hostctl` / `service-runner` binaries and the installed
`lowlatd` backend; it does not execute scripts from a checkout or an arbitrary
path supplied by the UI. An administrator may explicitly opt in via
`hostctl authorize-owner`.
It writes a root-owned local Polkit rule scoped to the configured owner, an
active local session, the exact helper path and the root target. Other pkexec
commands and users retain their normal policy. No rule is shipped or enabled
for all users by the package. `revoke-owner-authorization` removes that opt-in.
Settings autosave after a stable edit, without a Save/Refresh button. Saving
capture/encoding changes does not restart an active stream: the operator stops
and starts hosting to apply them. Saving Start at Boot enables/disables the
system unit; live Share/Stop Sharing updates do not restart an active host.

The worker checks SO_PEERCRED root on `/run/parsec-host/session`. The service
checks the configured owner's UID. Length-prefixed JSON frames carry room,
pending requests and live permissions. Decisions, Kick and permission changes
use this existing channel and require no extra Polkit prompt. Read-only UI runs
refuse every mutation. Pending IDs stay u64 in IPC; their native attempt strings
are decimal, separate from the UI popup IDs. Native record arrays use the vendor's
allocator and are freed by its existing renderer.

## Validation scope

C tests cover native adapters, exact identity, ABI, settings/GPU mapping,
secret format, live sharing without restart, tray framing, decisions and
permissions. Rust tests cover pending lifecycle, admission, file trust and live
advertisement. An actual vendor client is exercised in an isolated Xvfb and
profile overlay: the native pending popup and click-to-decision path are observed.
Fixture UI tests do not establish a real external guest connection. A second
account and sustained streaming remain explicitly unverified.

## Separate encoding GPU

The validated `encoding_gpu` setting defaults to `auto` for old and new
installations. `gpu` still selects capture and needs a connected output.
Explicit encoding selects an available AMD/Intel PCI device independently;
headless devices are allowed, VFIO/missing devices are rejected. NVENC on a
separate named device is not advertised. The root runner derives a fixed
`/dev/dri/by-path/pci-<validated PCI>-render` value for `LOWLAT_VAAPI_DEVICE`.
It accepts no free-form executable or render path from the UI.

Patch 0006 leaves the default device-handle path intact. An explicit different
VA-API node uses eight-bit NV12 readback and hardware upload, preserving original
capture timestamps. CPU-visible cached staging memory is preferred, avoiding
slow reads from uncached GPU BAR memory. This is explicit RAM staging, not a
zero-copy or zero-overhead claim. Ten-bit, 4:4:4 and OpenGL staging are refused.
The native Encoding GPU selector and C autosave worker share the bridge ABI.

## Host options mapped to lowlatd (ABI 8)

ABI 8 (6144 bytes) appends four Windows Parsec host options that lowlatd
already implements, so no lowlat patch is involved: `max_guests` (1-8, the
bridge's guest capacity) becomes `--max-guests`; `exclusive_input` becomes
`--exclusive-pointer` (pointer arbitration only); `audio_device` becomes
`--audio-device` with a PulseAudio monitor-source name; `port` (0 or
1024-65535) becomes `--port`. Zero and `auto` omit the flag, leaving
lowlatd's own behaviour, and keep old settings files valid.

The unprivileged worker lists outputs with `/usr/bin/pactl -f json list sinks`
in the desktop session and publishes their monitor sources. Names are checked
against `^[A-Za-z0-9_][A-Za-z0-9_.:@+-]{0,126}$` in the worker and again by the
root validator, and are passed as one argv element, so none can become an
option. A configured device that is gone stays listed as unavailable. The port
is edited as text and parsed by the worker; a form that cannot be saved now
reports why on the page instead of silently not saving.

An actual local stream loop, without a signaling peer, captured RTX 5070 DP-1
and encoded on RX 570; both H.264 and HEVC outputs decoded successfully in
FFmpeg. See ENCODING-GPU.md for measured limits. Real second-account playback
and game FPS impact remain unmeasured.

## Boot and lock screens

The system service is independent of the graphical session target. It can
start before user login after Host account/owner setup and the Start at Boot
setting enables the unit. Its runner waits for the configured connected display,
with SIGTERM cancelling the wait;
restarts remain enabled through display-manager or network startup delays.
The per-user helper belongs to graphical-session.target, which ends at logout,
not at a screen lock. The helper is recommended for desktop-session facilities,
not a prerequisite for the system unit to start. Capture and input run in the
system service; session-only facilities such as audio/clipboard may become
available after login. This does not create a headless display or bypass the
desktop's normal lock-screen authentication.

The C regression suite runs as root and as an ordinary user in CI. Its share
fixture chooses a real non-root account even inside the Arch root container;
production owner rejection and 0640 group restrictions are preserved.

Local verification on CachyOS / KDE Wayland (2026-10-18): while logind reported
LockedHint=yes, the system host remained active with the same PID. A local
stream probe captured NVIDIA DP-1 and encoded 193 H.264 frames in five seconds
on RX 570 VA-API; FFmpeg decoded the output and the KDE lock screen was visually
confirmed. The original session was restored. This tests lock-screen capture;
remote keyboard entry and a cold boot through the login greeter were not tested.
Three consecutive installed-helper saves succeeded without authentication;
an unrelated pkexec action still required authorization.
