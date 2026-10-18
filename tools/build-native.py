#!/usr/bin/python3
"""Build our native UI and patch an exact, independently downloaded vendor ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = '10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983'


def build(module, output):
    original = module.read_bytes()
    if hashlib.sha256(original).hexdigest() != EXPECTED:
        raise ValueError('Unsupported vendor module; original file was not modified')
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cc', '-Os', '-fPIE', '-fvisibility=hidden', '-fcf-protection=branch',
        '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
        '-fno-builtin', '-Wall', '-Wextra', '-c', str(ROOT / 'native/host-ui.c'),
        '-o', str(output / 'host-ui.o')], check=True)
    subprocess.run(['ld', '-nostdlib', '-T', str(ROOT / 'native/host-ui.ld'),
        str(output / 'host-ui.o'), '-o', str(output / 'host-ui.elf')], check=True)
    payload = output / 'host-ui.elf'
    symbols = {}
    for line in subprocess.check_output(['nm', '-n', str(payload)], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    raw = payload.read_bytes()
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * shentsize) for i in range(shnum)]
    strings = sections[shstrndx]
    names = raw[strings[4]:strings[4] + strings[5]]
    by_name = {names[s[0]:].split(b'\0', 1)[0].decode(): s for s in sections}
    code, state = by_name['.extension'], by_name['.state']
    if any(s[1] in (4, 9) and s[5] for s in sections):
        raise ValueError('Unresolved relocation in native UI')
    assert code[3] == 0x401000 and state[3] >= code[3] + code[5]
    align = lambda v: (v + 4095) & ~4095
    phoff = struct.unpack_from('<Q', original, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', original, 54)
    assert phentsize == 56
    headers = [struct.unpack_from('<IIQQQQQQ', original, phoff + i * 56) for i in range(phnum)]
    assert max(h[3] + h[6] for h in headers if h[0] == 1) < 0x400000
    new_phoff = align(len(original))
    code_offset = new_phoff + 4096
    data_offset = align(code_offset + code[5])
    headers += [(1, 5, new_phoff, 0x400000, 0x400000, 4096 + code[5], 4096 + code[5], 4096),
                (1, 6, data_offset, state[3], state[3], state[5], state[5], 4096)]
    assert len(headers) * 56 <= 4096
    patched = bytearray(original)
    patched.extend(b'\0' * (data_offset + state[5] - len(patched)))
    for i, h in enumerate(headers):
        struct.pack_into('<IIQQQQQQ', patched, new_phoff + i * 56, *h)
    struct.pack_into('<Q', patched, 32, new_phoff)
    struct.pack_into('<H', patched, 56, len(headers))
    patched[code_offset:code_offset + code[5]] = raw[code[4]:code[4] + code[5]]
    assert state[1] in (1, 8)
    if state[1] == 1:
        patched[data_offset:data_offset + state[5]] = raw[state[4]:state[4] + state[5]]
    def rel(op, site, target):
        return bytes([op]) + struct.pack('<i', target - site - 5)
    assert patched[0x86164:0x86169] == bytes.fromhex('e8d7e8fbff')
    patched[0x86164:0x86169] = rel(0xe8, 0x86164, symbols['client_and_host'])
    assert patched[0x3cee8:0x3cf00] == bytes.fromhex('488b442410488b7828e8ca550400e9e5e5ffff0f1f440000')
    replacement = (bytes.fromhex('f30f10442408') + bytes.fromhex('488b7c2410')
        + rel(0xe8, 0x3cee8 + 11, symbols['host_page'])
        + rel(0xe9, 0x3cee8 + 16, 0x3b4e0))
    patched[0x3cee8:0x3cf00] = replacement.ljust(24, b'\x90')
    assert patched[0x88440:0x8844a].hex() == 'f30f1efab8fbffffffc3'
    # Preserve the vendor's original capability answer; our backend is lowlat.
    assert patched[0x2cc60:0x2cc60 + 319] == original[0x2cc60:0x2cc60 + 319]
    target = output / 'parsecd-150-105c.so'
    assert target.resolve() != module.resolve()
    target.write_bytes(patched)
    metadata = dict(so_name=target.name, hash=hashlib.sha256(patched).hexdigest(), entry_symbol='wx_main')
    (output / 'appdata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    report = dict(version='150-105c', original_sha256=EXPECTED, patched_sha256=metadata['hash'],
        ui_extension_size=code[5], ui_state_size=state[5], native_host_tab=True,
        host_engine='lowlat', host_start_stub_unchanged=True, capability_builder_unchanged=True,
        patch_sites=['0x86164', '0x3cee8'], extension_rva=hex(code[3]))
    (output / 'patch.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    build(args.module, args.output)
