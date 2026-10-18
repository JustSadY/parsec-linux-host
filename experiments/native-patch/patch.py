#!/usr/bin/python3
"""Apply the rebuilt capability function to a lab-only, hash-pinned copy."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser()
parser.add_argument('module', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
lab = root / '.cache/native-patch'
lab.mkdir(parents=True, exist_ok=True)
original = args.module.read_bytes()
expected = '10af623d573b566bfd3f9a5c36a509001e02be3da5a60e0f2e0f6920cb6eb983'
if hashlib.sha256(original).hexdigest() != expected:
    raise SystemExit('Unsupported module hash; nothing patched')
if original[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('Expected little-endian ELF64')
phoff = struct.unpack_from('<Q', original, 32)[0]
phentsize, phnum = struct.unpack_from('<HH', original, 54)
def offset(rva, length):
    for i in range(phnum):
        typ, flags, pos, va, pa, size, memsize, align = struct.unpack_from('<IIQQQQQQ', original, phoff + i * phentsize)
        if typ == 1 and flags & 1 and va <= rva and rva + length <= va + size:
            return pos + rva - va
    raise ValueError('RVA is not file-backed executable code')
start, capacity = offset(0x2cc60, 319), 319
payload = (lab / 'capabilities.bin').read_bytes()
if not payload.startswith(bytes.fromhex('f30f1efa')) or not 0 < len(payload) <= capacity:
    raise SystemExit('Invalid rebuilt function or insufficient space')
stub_start = offset(0x88440, 10)
stub = original[stub_start:stub_start + 10]
if stub.hex() != 'f30f1efab8fbffffffc3':
    raise SystemExit('Unexpected host-start stub')
patched = original[:start] + payload + b'\x90' * (capacity - len(payload)) + original[start + capacity:]
assert patched[:start] == original[:start] and patched[start + capacity:] == original[start + capacity:]
assert patched[stub_start:stub_start + 10] == stub
target = lab / 'parsecd-150-105c-rebuilt.so'
if target.resolve() == args.module.resolve():
    raise SystemExit('The original module cannot be the destination')
target.write_bytes(patched)
record = dict(version='150-105c', original_sha256=expected,
    patched_sha256=hashlib.sha256(patched).hexdigest(), elf_rva=hex(0x2cc60),
    ghidra_address='0x12cc60', file_offset=hex(start), original_size=capacity,
    rebuilt_size=len(payload), host_start_stub_unchanged=True,
    reconstruction='capability response only; not a reconstructed host engine')
(lab / 'patch.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
