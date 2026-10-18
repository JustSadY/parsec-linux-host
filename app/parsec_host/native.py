"""Launch the patched vendor client; all Host widgets run inside that process."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
from . import config as c
from .bridge import NativeBridge


def launch(extra=(), *, temporary=False, vendor=None, module_dir=None, read_only=False):
    if os.geteuid() == 0:
        raise ValueError('Open Parsec as your desktop user, without sudo.')
    if not os.environ.get('DISPLAY'):
        raise ValueError('An X11 or XWayland desktop session is required.')
    vendor = Path(vendor or c.VENDOR)
    module_dir = Path(module_dir or c.LIB / 'vendor')
    metadata = json.loads((module_dir / 'appdata.json').read_text())
    module = module_dir / 'parsecd-150-105c.so'
    if (metadata.get('so_name') != module.name or metadata.get('entry_symbol') != 'wx_main'
        or hashlib.sha256(module.read_bytes()).hexdigest() != metadata.get('hash')):
        raise ValueError('Installed native module is inconsistent; reinstall the matching package.')
    profile = Path.home() / '.parsec'
    profile.mkdir(mode=0o700, exist_ok=True)
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))
    lock_path = runtime / 'parsec-linux-host.lock'
    import fcntl
    with lock_path.open('a') as lock, NativeBridge(read_only=read_only) as bridge:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('Parsec Linux Host is already open.') from None
        command = ['bwrap', '--ro-bind', '/', '/', '--unshare-pid', '--unshare-ipc',
            '--proc', '/proc', '--dev-bind', '/dev', '/dev', '--tmpfs', '/tmp',
            '--ro-bind', '/tmp/.X11-unix', '/tmp/.X11-unix', '--dir', '/tmp/runtime']
        if temporary:
            command += ['--overlay-src', str(profile), '--tmp-overlay', str(profile)]
        else:
            command += ['--bind', str(profile), str(profile)]
        command += ['--ro-bind', str(module), str(profile / module.name),
            '--ro-bind', str(module_dir / 'appdata.json'), str(profile / 'appdata.json'),
            '--bind', str(bridge.path), str(bridge.path), '--clearenv',
            '--setenv', 'HOME', str(Path.home()), '--setenv', 'PATH', '/usr/bin:/bin',
            '--setenv', 'LANG', os.environ.get('LANG', 'C.UTF-8'),
            '--setenv', 'DISPLAY', os.environ['DISPLAY'], '--setenv', 'XDG_SESSION_TYPE', 'x11',
            '--setenv', 'XDG_RUNTIME_DIR', '/tmp/runtime',
            '--setenv', 'PARSEC_NATIVE_HOST_BRIDGE', str(bridge.path)]
        for key in ('XAUTHORITY', '__EGL_VENDOR_LIBRARY_FILENAMES', '__GLX_VENDOR_LIBRARY_NAME',
                    '__NV_PRIME_RENDER_OFFLOAD', 'LIBGL_ALWAYS_SOFTWARE'):
            if key in os.environ:
                command += ['--setenv', key, os.environ[key]]
        # The upstream hosting implementation is absent; start/stop use lowlat.
        command += ['--die-with-parent', '--', str(vendor),
            ':'.join(['app_host=0', *c.client_args(c.load())]), *extra]
        child = subprocess.Popen(command)
        try:
            return child.wait()
        except KeyboardInterrupt:
            child.terminate()
            return child.wait(timeout=5)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
