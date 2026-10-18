#!/usr/bin/python3
"""Public settings/status bridge. Existing root-owned helper validates all writes.

The native UI shares a private, versioned mmap with this launcher. The worker
runs outside the vendor sandbox, invokes only fixed helper actions, and never
opens account.json or any Parsec profile/credential file.
"""
import ctypes
import json
import mmap
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import shutil
from . import config as c


class State(ctypes.LittleEndianStructure):
    _fields_ = [(key, ctypes.c_uint32) for key in
        ('magic', 'version', 'seq', 'ack', 'action', 'configured', 'running', 'busy')]
    _fields_ += [('status', ctypes.c_char * 256), ('name', ctypes.c_char * 128),
                ('output', ctypes.c_char * 64)]
    _fields_ += [(key, ctypes.c_int32) for key in ('fps', 'bitrate', 'codec', 'quality', 'autostart')]
    _fields_ += [('gpu', ctypes.c_char * 32)]
    _fields_ += [(key, ctypes.c_int32) for key in ('encoder', 'conversion', 'full_fps', 'mute_speakers', 'clipboard')]
    _fields_ += [('gpu_count', ctypes.c_uint32), ('gpu_ids', (ctypes.c_char * 32) * 8),
                ('gpu_labels', (ctypes.c_char * 96) * 8)]


assert ctypes.sizeof(State) == 1580


class NativeBridge:
    def __init__(self, *, read_only=False, helper=None):
        self.read_only = read_only
        self.stopped = threading.Event()
        self.helper = helper or c.HELPER
        self.settings = {}
        self.actions = []

    def __enter__(self):
        runtime = Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))
        self.directory = tempfile.TemporaryDirectory(prefix='parsec-native-host-',
            dir=runtime if runtime.is_dir() else None)
        self.path = Path(self.directory.name) / 'bridge'
        fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
        os.ftruncate(fd, ctypes.sizeof(State))
        self.memory = mmap.mmap(fd, ctypes.sizeof(State))
        os.close(fd)
        self.state = State.from_buffer(self.memory)
        self.state.magic, self.state.version = 0x50484f53, 2
        self.state.fps, self.state.bitrate = 60, 10
        self.state.status = b'Loading Linux host status...'
        try:
            self.refresh(settings=True)
        except Exception as error:
            self.message(str(error))
        self.worker = threading.Thread(target=self.loop, daemon=True)
        self.worker.start()
        return self

    def call(self, action, data=None, privileged=False):
        if not Path(self.helper).is_file():
            raise ValueError('Linux host controller is not installed.')
        command = (['/usr/bin/pkexec'] if privileged else []) + [self.helper, action]
        with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True) as process:
            payload = json.dumps(data or {})
            for attempt in range(900):
                try:
                    stdout, stderr = process.communicate(payload if not attempt else None, timeout=.1)
                    break
                except subprocess.TimeoutExpired:
                    if self.stopped.is_set():
                        process.terminate()
                        try:
                            process.communicate(timeout=1)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.communicate()
                        raise ValueError('Host request cancelled.')
            else:
                process.kill()
                process.communicate()
                raise ValueError('Host controller timed out.')
        try:
            reply = json.loads(stdout)
        except ValueError:
            raise ValueError('Administrator authentication was cancelled or the controller failed.') from None
        if process.returncode or not reply.get('ok'):
            raise ValueError(reply.get('error', 'Host controller failed.'))
        return reply['result']

    def message(self, value):
        # UTF-8 boundary-safe; no arbitrary subprocess output reaches this UI.
        self.state.status = value.encode()[:255].decode(errors='ignore').encode()

    def refresh(self, *, settings=False, preserve_message=False):
        result = self.call('status')
        self.state.configured = bool(result.get('configured'))
        service = result.get('service', {})
        self.state.running = service.get('ActiveState') == 'active'
        if not preserve_message:
            self.message('Linux host: ' + service.get('ActiveState', 'unknown') +
                (' | Host account connected' if self.state.configured else ' | Connect the Host account first'))
        if settings:
            self.settings = {**c.DEFAULT, **result['settings']}
            for key, length in [('name', 127), ('output', 63)]:
                setattr(self.state, key, self.settings[key].encode()[:length])
            for key in ('fps', 'bitrate', 'autostart'):
                setattr(self.state, key, int(self.settings[key]))
            self.state.codec = ('h264', 'h265').index(self.settings['codec'])
            self.state.quality = ('lowest-latency', 'balanced', 'highest').index(self.settings['quality'])
            self.state.gpu = self.settings['gpu'].encode()
            for key in ('encoder', 'conversion'):
                setattr(self.state, key, c.CHOICES[key].index(self.settings[key]))
            for key in ('full_fps', 'mute_speakers', 'clipboard'):
                setattr(self.state, key, int(self.settings[key]))
            devices = [d for d in result.get('devices', []) if d['outputs']][:8]
            self.state.gpu_count = len(devices)
            for i, device in enumerate(devices):
                self.state.gpu_ids[i].value = device['pci'].encode()
                self.state.gpu_labels[i].value = device['label'].encode()[:95].decode(errors='ignore').encode()

    def form(self):
        state = self.state
        if not 0 <= state.codec < 2 or not 0 <= state.quality < 3:
            raise ValueError('Invalid native form selection.')
        if not 0 <= state.encoder < 3 or not 0 <= state.conversion < 3:
            raise ValueError('Invalid native encoder selection.')
        result = {**self.settings, 'name': state.name.decode(), 'output': state.output.decode(),
            'fps': state.fps, 'bitrate': state.bitrate, 'codec': ('h264', 'h265')[state.codec],
            'quality': ('lowest-latency', 'balanced', 'highest')[state.quality],
            'autostart': bool(state.autostart), 'gpu': state.gpu.decode(),
            'encoder': c.CHOICES['encoder'][state.encoder],
            'conversion': c.CHOICES['conversion'][state.conversion],
            **{key: bool(getattr(state, key)) for key in ('full_fps', 'mute_speakers', 'clipboard')}}
        return c.validate(result)

    def login(self):
        for executable, arguments in [('xdg-terminal-exec', []), ('konsole', ['--separate', '-e']),
                                      ('gnome-terminal', ['--']), ('xterm', ['-e'])]:
            terminal = shutil.which(executable)
            if terminal:
                subprocess.Popen([terminal, *arguments, '/usr/bin/parsec-linux-host', 'login'],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    start_new_session=True)
                self.message('Complete Host login in the terminal, then choose Refresh status.')
                return
        raise ValueError('Run parsec-linux-host login in a terminal, then Refresh status.')

    def loop(self):
        while not self.stopped.wait(.1):
            if self.state.seq == self.state.ack:
                continue
            seq, action = self.state.seq, self.state.action
            try:
                if action not in (1, 2, 3, 4, 5):
                    raise ValueError('Unknown native Host action.')
                self.actions.append(action)
                if action == 4:
                    self.refresh()
                elif self.read_only:
                    self.message('Read-only UI test: no system settings or service state changed.')
                elif action == 5:
                    self.login()
                elif action == 1:
                    self.call('save', self.form(), privileged=True)
                    self.refresh(settings=True)
                    self.message('Settings saved. Stop and start hosting to apply them.')
                else:
                    self.call('start' if action == 2 else 'stop', privileged=True)
                    self.refresh()
            except Exception as error:
                self.message(str(error) if isinstance(error, (ValueError, UnicodeError)) else type(error).__name__)
            finally:
                self.state.ack, self.state.busy = seq, 0

    def __exit__(self, *_):
        self.stopped.set()
        self.worker.join(timeout=3)
        if self.worker.is_alive():
            raise RuntimeError('Native Host worker did not stop')
        self.state = None
        self.memory.close()
        self.directory.cleanup()
