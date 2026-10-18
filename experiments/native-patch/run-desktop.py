#!/usr/bin/python3
"""Open the lab module on the real desktop, with a disposable profile overlay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--check-launch', action='store_true', help='Verify a window opens, then close the disposable test')
args = parser.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
root = Path(__file__).resolve().parents[2]
lab = root / '.cache/native-patch'
module = lab / 'parsecd-150-105c-rebuilt.so'
record = json.loads((lab / 'patch.json').read_text())
if hashlib.sha256(module.read_bytes()).hexdigest() != record['patched_sha256']:
    raise SystemExit('Test module hash mismatch. Run the make check target again.')
if not os.environ.get('DISPLAY'):
    raise SystemExit('Run from a terminal inside the Linux graphical session.')
if subprocess.run(['pgrep', '-u', str(os.getuid()), '-x', 'parsecd'],
                  stdout=subprocess.DEVNULL).returncode == 0:
    raise SystemExit('Önce açık olan Parsec istemcisini kapat; host servisini durdurman gerekmez.')
profile = Path.home() / '.parsec'
metadata = lab / 'desktop-appdata.json'
metadata.write_text(json.dumps(dict(so_name='parsecd-150-105c.so',
    hash=record['patched_sha256'], entry_symbol='wx_main')))
command = ['bwrap', '--ro-bind', '/', '/', '--unshare-pid', '--unshare-ipc',
    '--proc', '/proc', '--dev-bind', '/dev', '/dev', '--tmpfs', '/tmp',
    '--ro-bind', '/tmp/.X11-unix', '/tmp/.X11-unix', '--dir', '/tmp/runtime',
    '--overlay-src', str(profile), '--tmp-overlay', str(profile),
    '--ro-bind', str(module), str(profile / 'parsecd-150-105c.so'),
    '--ro-bind', str(metadata), str(profile / 'appdata.json'), '--clearenv',
    '--setenv', 'PATH', '/usr/bin:/bin', '--setenv', 'HOME', str(Path.home()),
    '--setenv', 'USER', os.environ.get('USER', ''), '--setenv', 'LANG', 'C.UTF-8',
    '--setenv', 'DISPLAY', os.environ['DISPLAY'], '--setenv', 'XDG_SESSION_TYPE', 'x11',
    '--setenv', 'XDG_RUNTIME_DIR', '/tmp/runtime']
authority = os.environ.get('XAUTHORITY')
if authority:
    command += ['--setenv', 'XAUTHORITY', authority]
command += ['--die-with-parent', '--', '/usr/bin/parsecd', 'app_host=0:app_log_level=3:client_renderer=0']
print('Yamalı gerçek Parsec test kopyası açılıyor. Profil değişiklikleri kapanınca silinir.', flush=True)
with (lab / 'desktop.log').open('w') as log:
    child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        if args.check_launch:
            from Xlib import X, display
            screen = display.Display()
            seen = False
            for _ in range(60):
                if child.poll() is not None:
                    raise RuntimeError('Test client exited before creating a window')
                prop = screen.screen().root.get_full_property(screen.intern_atom('_NET_CLIENT_LIST'), X.AnyPropertyType)
                for xid in prop.value if prop is not None else []:
                    win = screen.create_resource_object('window', int(xid))
                    if 'parsecd' in (win.get_wm_class() or ()) and win.get_attributes().map_state == X.IsViewable:
                        seen = True
                        break
                if seen:
                    break
                time.sleep(.2)
            screen.close()
            if not seen:
                raise RuntimeError('No mapped Parsec test window appeared within 12 seconds')
            print('PASS: real desktop test window is mapped.', flush=True)
        else:
            raise SystemExit(child.wait())
    except KeyboardInterrupt:
        pass
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
