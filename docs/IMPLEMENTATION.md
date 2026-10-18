# Native Host integration, 0.3.0

The runtime is compiled C (`runtime/`). The extension runs inside the actual
vendor client, calls its native ImGui helpers and shares ABI 6 through
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
is stored root-only and redacted from runner output. Root never executes project
files. Settings autosave after a stable edit, without a Save/Refresh button.

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
