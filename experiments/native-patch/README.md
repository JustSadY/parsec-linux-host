# Native binary reconstruction experiment — 150-105c

18 October 2026. **A real reconstructed C function was compiled and inserted
into a private copy of the vendor module, then executed and tested in the real
Parsec client. This did not produce a working Linux native host.**

## What was rebuilt

The earlier REA/Ghidra function dossier for `FUN_0012cc60` showed a capability
response builder calling private JSON constructors. Its reported support flag
was zero. The reconstruction in `capabilities.c` preserves the JSON envelope,
renderers and hostname, and changes `hosting_supported` to one.

This C code was compiled to a **266-byte** position-independent replacement.
The original function occupies **319 bytes**. The linker script binds calls to
the observed existing JSON/hostname helpers at their ELF RVAs. The resulting
payload has no unresolved relocations, fits in the original function's region,
and uses relative calls/addressing, so normal module ASLR still works.

`patch.py` admits only original SHA-256
`10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983`.
It validates executable ELF segment mapping, replaces that function in a lab
copy and leaves the host-start stub unchanged. It never writes the live module.
This is a single-function reconstruction, **not recovered original source or a
recompilation of the entire proprietary program**.

## Observed results

| Check | Original module | Rebuilt test module |
|---|---|---|
| Disposable runtime probe exit | 0 | 0 |
| `hosting_supported` | false | true |
| Renderer data | OpenGL, Vulkan | OpenGL, Vulkan |
| Direct native host-start function result | -5 | -5 |
| Real client startup | Existing control version | Opened successfully as 150-105c |
| Real settings tabs with the patch | — | Client, Network, Hotkeys, Gamepad, Experimental, Account; Host absent |

For the GUI test, the real `/usr/bin/parsecd` loader used the rebuilt module.
The mounted module hash was independently read back through the test process's
root and matched the patched file. The app was signed in using its existing
profile. Settings was opened and visually inspected on an isolated Xvfb screen.
The live profile was an overlay lower layer, with writes going to disposable
RAM; credentials were not copied, parsed, exported, or printed by these tools.
GUI startup required the online test in this run; the offline attempt had not
produced a window during the observed interval. The normal application files,
live profile and running lowlat service were not replaced.

The host-start test directly calls the observed unconditional stub, rather
than claiming a real guest connection was attempted. No native peer stream was
established. Returning success from this stub would not create the encoder,
capture context and callbacks its caller expects; this experiment does not
falsify that return value.

The changed capability response did not alter the classic native settings
tabs. This demonstrates that this one response is insufficient to enable that
screen. A bounded UTF-8/UTF-16LE literal search found `CLIENT SETTINGS` but no
`HOST SETTINGS`, `Hosting Enabled` or `Host Name` in this specific module. That
search alone does not prove the absence of all equivalent or encoded UI code.

## Reproduce

### Masaüstünde açmak

Önce açık Parsec istemcisini kapat. Çalışan lowlat host servisini kapatmak
gerekmez. Linux masaüstündeki terminalde, sudo kullanmadan:

```sh
cd ~/Desktop/parsec-linux-host
make -f experiments/native-patch/Makefile desktop
```

Bu hedef önce yamayı derleyip işlev testlerini çalıştırır, ardından yamalı gerçek
Parsec istemcisini masaüstünde açar. Mevcut giriş vendor istemci tarafından
kullanılır; profil yazıları geçici RAM katmanına gider. Pencereyi kapatınca
test sona erer. GUI, normal uygulamayla aynı görünebilir: bu deneyde destek
yanıtı true olsa da Host sekmesi henüz görünmez ve native host çalışmaz.
Yalnızca terminal doğrulaması için aşağıdaki `check` hedefini kullan.

From the repository root, with binutils, a C compiler, bubblewrap and the
matching module present:

```sh
make -f experiments/native-patch/Makefile check
```

This compiles the reconstructed function, creates the lab-only module, and
executes both function probes with networking disabled. Public results are in
`results.json`; transient binaries/logs/images are under `.cache/native-patch`
and are ignored by Git.

For the optional GUI test, Xvfb, xauth, Python Xlib and Pillow are also needed:

```sh
make -f experiments/native-patch/Makefile gui
```

This online test lets the vendor client use its existing login in a disposable
profile overlay. `app_host=0` keeps native hosting disabled during inspection.
Interactive commands affect only that Xvfb display: `shot LABEL`, `click X Y`,
and `quit`. Screenshots can contain account names; they are not committed.

## Packaging boundary

The default AUR/Arch recipe remains unchanged and does not apply this
experiment. No patched proprietary binary is committed or published. Original
unwind metadata is not regenerated for the replacement function, and this
experiment is not suitable for production installation. A functional native
integration would need the missing host implementation and a separate native
UI integration, with much more ABI and lifecycle validation.
