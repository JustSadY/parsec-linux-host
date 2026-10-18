#!/usr/bin/python3
"""Disposable Xvfb + bubblewrap GUI test. The vendor alone reads its profile.

The live profile is a read-only overlay lower layer; all profile writes vanish.
No credential file is copied, parsed or printed. Networking is disabled.
Interactive stdin commands operate ONLY the private Xvfb display.
"""
import argparse
import json
import os
from pathlib import Path
import resource
import secrets
import signal
import subprocess
import sys
import time

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
parser = argparse.ArgumentParser()
parser.add_argument('module', type=Path)
parser.add_argument('--online', action='store_true', help='Allow the vendor client to use its existing login online')
parser.add_argument('--native-host', action='store_true', help='Attach the read-only native Host controller test')
parser.add_argument('--helper', help='Read-only status helper override for migration tests')
parser.add_argument('--debug', action='store_true', help='Collect a bounded crash backtrace for this disposable test')
parser.add_argument('--control-file', type=Path, help='Read private test commands from an append-only file')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
lab = root / '.cache/gui-test'
lab.mkdir(parents=True, exist_ok=True)
profile = Path.home() / '.parsec'
module = args.module.resolve(strict=True)
number = next(i for i in range(91, 120) if not Path(f'/tmp/.X11-unix/X{i}').exists())
name = f':{number}'
auth = lab / 'test-Xauthority'
auth.touch(mode=0o600)
auth.chmod(0o600)
subprocess.run(['xauth', '-f', str(auth), '-'],
    input=f'add {name} . {secrets.token_hex(16)}\n'.encode(), check=True, capture_output=True)
meta = lab / 'gui-appdata.json'
import hashlib
meta.write_text(json.dumps(dict(so_name='parsecd-150-105c.so',
    hash=hashlib.sha256(module.read_bytes()).hexdigest(), entry_symbol='wx_main')))
log = (lab / 'gui.log').open('w')
xlog = (lab / 'xvfb.log').open('w')
server = subprocess.Popen(['Xvfb', name, '-screen', '0', '1440x1000x24',
    '-nolisten', 'tcp', '-auth', str(auth)], stdout=xlog, stderr=subprocess.STDOUT)
client = None
connection = None
bridge = None
try:
    for _ in range(30):
        if Path(f'/tmp/.X11-unix/X{number}').exists():
            break
        time.sleep(.1)
    command = ['bwrap', '--ro-bind', '/', '/', '--unshare-pid', '--unshare-ipc',
        *([] if args.online else ['--unshare-net']), '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp',
        '--ro-bind', '/tmp/.X11-unix', '/tmp/.X11-unix', '--dir', '/tmp/runtime',
        '--overlay-src', str(profile), '--tmp-overlay', str(profile),
        '--ro-bind', str(module), str(profile / 'parsecd-150-105c.so'),
        '--ro-bind', str(meta), str(profile / 'appdata.json'), '--clearenv',
        '--setenv', 'PATH', '/usr/bin:/bin', '--setenv', 'HOME', str(Path.home()),
        '--setenv', 'USER', os.environ.get('USER', ''), '--setenv', 'LANG', 'C.UTF-8',
        '--setenv', 'DISPLAY', name, '--setenv', 'XAUTHORITY', str(auth),
        '--setenv', 'XDG_RUNTIME_DIR', '/tmp/runtime', '--setenv', 'XDG_SESSION_TYPE', 'x11',
        '--setenv', 'LIBGL_ALWAYS_SOFTWARE', '1']
    if args.native_host:
        # The native Host bridge now lives in the compiled launcher; this bare
        # vendor harness no longer drives it. Use the read-only launcher path.
        print('For the native Host bridge use: parsec-linux-host open --read-only',
              file=sys.stderr)
    command += ['--die-with-parent', '--']
    if args.debug:
        command += ['gdb', '--batch', '-ex', 'set pagination off', '-ex', 'set print frame-arguments none',
            '-ex', 'run', '-ex', 'bt 12', '-ex', 'info registers rip rsp rdi rsi rdx',
            '-ex', 'x/6i $pc', '-ex', 'info sharedlibrary parsecd', '--args']
    vendor = Path('/usr/lib/parsec-linux-host/vendor/parsecd')
    command += [str(vendor if vendor.exists() else '/usr/bin/parsecd'), 'app_host=0:app_log_level=3:client_renderer=0']
    client = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log,
                              stderr=subprocess.STDOUT, start_new_session=True)
    os.environ['DISPLAY'], os.environ['XAUTHORITY'] = name, str(auth)
    from Xlib import X, display
    from Xlib.ext import xtest
    from PIL import Image
    connection = display.Display(name)
    root_window = connection.screen().root
    def snapshot(suffix):
        image = root_window.get_image(0, 0, 1440, 1000, X.ZPixmap, 0xffffffff)
        destination = lab / ('gui-' + suffix + '.png')
        Image.frombytes('RGB', (1440, 1000), image.data, 'raw', 'BGRX').save(destination)
        windows = []
        for win in root_window.query_tree().children:
            try:
                if win.get_attributes().map_state == X.IsViewable:
                    g = win.get_geometry()
                    windows.append(dict(id=win.id, title=win.get_wm_name(), x=g.x, y=g.y, width=g.width, height=g.height))
            except Exception:
                pass
        print(json.dumps(dict(snapshot=str(destination), windows=windows, client_exit=client.poll())), flush=True)
    time.sleep(6)
    snapshot('initial')
    print('READY: click X Y, shot LABEL, or quit (private Xvfb display only)', flush=True)
    def commands():
        if not args.control_file:
            yield from sys.stdin
            return
        args.control_file.touch(mode=0o600, exist_ok=True)
        with args.control_file.open() as control:
            while True:
                line = control.readline()
                if line:
                    yield line
                else:
                    time.sleep(.1)
    for line in commands():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'quit':
            break
        if parts[0] == 'click' and len(parts) == 3:
            x, y = map(int, parts[1:])
            if not 0 <= x < 1440 or not 0 <= y < 1000:
                raise ValueError('Coordinates outside test screen')
            xtest.fake_input(connection, X.MotionNotify, x=x, y=y)
            xtest.fake_input(connection, X.ButtonPress, 1)
            xtest.fake_input(connection, X.ButtonRelease, 1)
            connection.sync()
            time.sleep(1)
            snapshot('clicked')
        elif parts[0] == 'shot' and len(parts) == 2 and parts[1].isalnum():
            snapshot(parts[1])
        elif parts[0] == 'scroll' and len(parts) == 2:
            amount = max(-20, min(20, int(parts[1])))
            for _ in range(abs(amount)):
                xtest.fake_input(connection, X.ButtonPress, 5 if amount > 0 else 4)
                xtest.fake_input(connection, X.ButtonRelease, 5 if amount > 0 else 4)
            connection.sync()
            time.sleep(.5)
            snapshot('scrolled')
        elif parts[0] == 'bridge' and bridge:
            print(json.dumps(dict(status=bridge.state.status.decode(),
                actions=bridge.actions, seq=bridge.state.seq, ack=bridge.state.ack,
                fps=bridge.state.fps, bitrate=bridge.state.bitrate)), flush=True)
finally:
    if connection:
        connection.close()
    if client and client.poll() is None:
        client.terminate()
        try:
            client.wait(timeout=3)
        except subprocess.TimeoutExpired:
            client.kill()
            client.wait()
    server.terminate()
    try:
        server.wait(timeout=3)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait()
    log.close()
    xlog.close()
    auth.unlink(missing_ok=True)
    if bridge:
        bridge.__exit__(None, None, None)
