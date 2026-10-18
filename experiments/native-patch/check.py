#!/usr/bin/python3
"""Compare the real loaded functions in network-isolated disposable probes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
lab = root / '.cache/native-patch'
original = Path(sys.argv[1]).resolve(strict=True)
digest = hashlib.sha256(original.read_bytes()).hexdigest()
record = json.loads((lab / 'patch.json').read_text())
assert digest == record['original_sha256']
results = {}
for label, module in [('original', original), ('rebuilt', lab / 'parsecd-150-105c-rebuilt.so')]:
    result = subprocess.run(['bwrap', '--ro-bind', '/', '/', '--unshare-net',
        '--proc', '/proc', '--dev', '/dev', '--', str(lab / 'probe'), str(module)],
        capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise SystemExit(f'{label} probe failed: exit {result.returncode}')
    results[label] = dict(exit=result.returncode, result=json.loads(result.stdout))
before, after = results['original']['result'], results['rebuilt']['result']
assert before['hosting_supported'] is False and after['hosting_supported'] is True
assert before['boolean_valid'] is True and after['boolean_valid'] is True
assert before['renderers'] == after['renderers'] == {'0': 'opengl', '5': 'vulkan'}
assert before['host_start_result'] == after['host_start_result'] == -5
assert hashlib.sha256(original.read_bytes()).hexdigest() == digest
(lab / 'runtime-probe.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
print('PASS: compiled capability replacement runs; native host start remains -5.')
