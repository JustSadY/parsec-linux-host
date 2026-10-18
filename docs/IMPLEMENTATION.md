# Native Host integration, 0.2.0

The PySide6 wrapper and capability-only experiment were removed from the current
tree. The Host page now executes in the vendor client.

## Evidence

Official module: 150-105c, SHA-256
`10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983`.
Ghidra image base: `0x100000`; scripts use ELF RVAs.

The constructor at `0x85010` registers Client, Network, Hotkeys, Gamepad,
Experimental and Account, with no Host call. The dispatcher at `0x3b2d0`
recognizes Host mask `0x200000040000002`, but that branch only renders the
configuration-file footer. It has no Host controls. Retained REA references:

- Constructor: `ev_a387b68d8520361af30cd9159d6759b0f47ffdb774eb166cc953aa8f760907c8`.
- Dispatcher: `ev_dc2239964371d5f50bc33e31218e4fabd478d46dbe00810a1c50832760c8e127`.

These are static decompiler observations, not recovered source. Our new C code
uses the existing native UI helpers. The build checks original bytes and patches
`0x86164` to add Host after Client, and `0x3cee8` to draw the new page. Code and
mutable state use separate RX/RW loads. The original capability builder at
`0x2cc60` is preserved, and host-start at `0x88440` still returns -5. No native
host success is forged; lowlat provides streaming. Vendor files are input only.
Original unwind metadata is not extended for the payload; this remains an
experimental private-ABI integration.

## Boundaries

Native controls and an unprivileged worker share a private 0600 runtime mmap in
a 0700 directory. It holds public settings/status and bounded action sequences,
never passwords or sessions. Allowed actions are save/start/stop/refresh/open-login.
Settings are validated in the worker and root-owned helper. Mutations use Polkit.
Terminal login sends credentials over stdin and stores only the returned session,
root-only. Runner output redacts it. Root does not execute user-writable project code.

lowlat patches retain owner-only admission, one guest, the namespaced
`/run/parsec-host/session` socket (0660), owner group and SO_PEERCRED UID check,
and the CUDA probe context-lifetime fix. DRM capture needs CAP_SYS_ADMIN.
No unprivileged-host guarantee is made.

## Validation

Tests cover settings/privileged boundaries, compiled C/Python mmap layout and
read-only rejection of mutations. A real vendor client was opened in a private
Xvfb/profile overlay. Host navigation, rendering, FPS/bitrate selection and save
acknowledgement were observed without changing live settings. Release checks
are recorded in `validation.json`. No sustained peer test at every setting or
GPU combination is claimed.
