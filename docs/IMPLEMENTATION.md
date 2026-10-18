# Implementation boundaries

The settings page is a native PySide6/Qt widget tree, styled to match the
classic Parsec settings layout supplied by the user. The official client is
embedded through an X11 foreign window under XWayland. It is detached on GUI
close. This is visual/window integration, **not an in-process host engine or
a modification to the official Settings → Host implementation**.

Reverse engineering of the existing Linux module 150-105c observed a capability
builder reporting hosting_supported=0 and a host-start callee returning -5
unconditionally. Those observations do not supply the missing source code.
This project does not distribute the vendor's disassembly, decompilation,
authentication files or patched binaries.

The package fetches the official Linux installer from the fixed HTTPS vendor
endpoint, with an explicit checksum, and lowlat at a pinned source commit.
`prepare()` patches lowlat. `build()` compiles its real host implementation.
`check()` runs Python UI/security tests plus daemon and protocol core tests.
`package()` stages files only; installation is a separate user action.

Local patches:

- Owner-only remote admission: rejects non-owner offers before guest admission.
- Namespaced `/run/parsec-host/session` control socket, mode 0660, and root-owned
  directory 0750; the authenticated setup
  helper assigns the desktop owner's primary group in a numeric systemd drop-in.
- SO_PEERCRED UID check against LOWLAT_OWNER_UID, provided by the root-owned
  owner record, also protects installations with a shared primary group.
- Hardware probe example retains the CUDA import context until imported memory
  is dropped, fixing the observed example teardown crash.

The privileged helper executes fixed argv arrays and validates all input. Root
code and vendor binaries are installed under `/usr/lib`; no root service runs
code from a user-writable checkout. Credentials enter on stdin, are sent only to
the official HTTPS authentication endpoint, and only the resulting session is
stored root-only (0600). Secrets are neither command-line parameters nor Git
assets. Runner output redacts the session. No upstream credential cache is read.

Root DRM capture needs CAP_SYS_ADMIN and is therefore highly privileged. The
service follows lowlat's device/capability restrictions and does not claim an
unprivileged sandbox. No TCP management web UI or unauthenticated root API is
opened by this project.

NVENC, VA-API and Vulkan/OpenGL conversion have hardware compatibility limits.
Live audio, input, high frame rates, relative mouse capture, and sustained
streaming still require real peer testing. lowlat is pre-release software;
this repository cannot claim official Parsec compatibility or support.
