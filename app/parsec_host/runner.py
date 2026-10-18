"""Root-owned service entry; redacts the private session from child output."""
import json
import signal
import subprocess
from . import config as c
from .control import owner
from .hardware import resolve


def main():
    uid = owner()['uid']
    if type(uid) is not int or uid < 1000:
        raise ValueError('Invalid desktop owner')
    token = json.loads((c.ETC / 'account.json').read_text())['session_id']
    if not isinstance(token, str) or not 1 <= len(token) <= 4096:
        raise ValueError('Invalid host session')
    settings = c.load()
    env = dict(PATH='/usr/bin:/bin', HOME='/root', LANG='C.UTF-8', KESSEL_SESSION=token,
               PULSE_SERVER=f'unix:/run/user/{uid}/pulse/native', LOWLAT_OWNER_UID=str(uid))
    child = subprocess.Popen(c.host_args(settings, resolve(settings)), env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    def stop(*_):
        if child.poll() is None:
            child.send_signal(signal.SIGINT)
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    for line in child.stdout:
        print(line.replace(token, '[session]'), end='', flush=True)
    return child.wait()
