"""Fixed privileged operations. JSON stdin carries credentials, never argv."""
import json
import os
import pwd
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path
from . import config as c
from .hardware import inventory, resolve


def run(*args, timeout=25):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout, check=False)


def atomic(path, data, mode=0o644):
    fd, name = tempfile.mkstemp(dir=path.parent, prefix='.new-')
    try:
        os.fchmod(fd, mode)
        with os.fdopen(fd, 'w') as f:
            json.dump(data, f, ensure_ascii=False)
            f.write('\n')
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def request(method, endpoint, body=None, token=None):
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    req = urllib.request.Request('https://kessel-api.parsec.app' + endpoint, method=method,
        headers=headers, data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(req, timeout=25) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        try:
            answer = json.loads(error.read())
            codes = ', '.join(str(i.get('type', '')) for i in answer.get('codes', []) if isinstance(i, dict))
        except (TypeError, ValueError):
            codes = ''
        raise ValueError(f'Parsec login: HTTP {error.code} {codes}') from None


def owner():
    return json.loads((c.ETC / 'owner.json').read_text())


def claim_owner():
    uid = int(os.environ.get('PKEXEC_UID') or os.environ.get('SUDO_UID') or '0')
    if uid < 1000:
        raise ValueError('Run from the desktop account through pkexec or sudo')
    user = pwd.getpwuid(uid)
    try:
        current = owner()
    except FileNotFoundError:
        current = None
    if current and current['uid'] != uid:
        raise ValueError('This host belongs to another local account')
    c.ETC.mkdir(mode=0o755, parents=True, exist_ok=True)
    atomic(c.ETC / 'owner.json', dict(uid=uid, gid=user.pw_gid))
    dropin = Path('/etc/systemd/system') / (c.UNIT + '.d')
    dropin.mkdir(mode=0o755, parents=True, exist_ok=True)
    target = dropin / 'owner.conf'
    wanted = f'[Service]\nGroup={user.pw_gid}\n'
    if not target.exists() or target.read_text() != wanted:
        target.write_text(wanted)
        target.chmod(0o644)
        if run('/usr/bin/systemctl', 'daemon-reload').returncode:
            raise ValueError('systemd reload failed')
    return uid


def status():
    p = run('/usr/bin/systemctl', 'show', c.UNIT, '--property=ActiveState,SubState,Result')
    state = dict(line.split('=', 1) for line in p.stdout.splitlines() if '=' in line)
    name = ''
    try:
        name = json.loads((c.ETC / 'account-info.json').read_text()).get('name', '')
    except (OSError, ValueError):
        pass
    return dict(installed=(c.LIB / 'lowlatd').exists(), configured=(c.ETC / 'account.json').exists(),
                service=state, account=name, settings=c.load(), devices=inventory())


def dispatch(action, data):
    if action == 'status':
        return status()
    if action == 'hardware':
        return {'devices': inventory()}
    if os.geteuid() != 0:
        raise PermissionError('Administrator authentication required')
    if action not in ('save', 'login', 'start', 'stop', 'logs'):
        raise ValueError('Unknown action')
    if action == 'logs':
        return {'logs': run('/usr/bin/journalctl', '-u', c.UNIT, '-n', '40', '--no-pager', '-o', 'cat').stdout[-16000:]}
    if action == 'save':
        settings = c.validate(data)
        c.host_args(settings, resolve(settings))
        claim_owner()
        atomic(c.ETC / 'settings.json', settings)
        if run('/usr/bin/systemctl', 'enable' if settings['autostart'] else 'disable', c.UNIT).returncode:
            raise ValueError('Could not save boot preference')
        return {'saved': True, 'restart_required': status()['service'].get('ActiveState') == 'active'}
    if action == 'login':
        if not isinstance(data, dict) or set(data) - {'email', 'password', 'tfa'}:
            raise ValueError('Invalid login fields')
        if any(not isinstance(data.get(k, ''), str) for k in ('email', 'password', 'tfa')):
            raise ValueError('Invalid login fields')
        if not data.get('email') or not data.get('password'):
            raise ValueError('Email and password required')
        if status()['service'].get('ActiveState') == 'active':
            raise ValueError('Stop hosting before changing account')
        claim_owner()
        body = {k: data[k].strip() if k != 'password' else data[k] for k in ('email', 'password')}
        body['session_scopes'] = ['ws.host', 'ws.client', 'api.writer']
        if data.get('tfa'):
            body['tfa'] = data['tfa'].strip()
        auth = request('POST', '/v2/auth', body)
        token = auth.get('session_id')
        if not isinstance(token, str) or not token or len(token) > 4096:
            raise ValueError('Invalid host session response')
        atomic(c.ETC / 'account.json', {'session_id': token}, 0o600)
        name = 'Parsec account'
        try:
            name = request('GET', '/me', token=token).get('data', {}).get('name') or name
        except Exception:
            pass
        atomic(c.ETC / 'account-info.json', {'name': str(name)[:80]})
        return {'connected': True}
    if action == 'start':
        claim_owner()
        if not (c.ETC / 'account.json').is_file():
            raise ValueError('Connect your Host account first')
        c.host_args(c.load(), resolve(c.load()))
    result = run('/usr/bin/systemctl', action, c.UNIT)
    if result.returncode:
        raise ValueError('Service operation failed; open Host logs')
    return {'completed': True}


def main():
    try:
        if len(sys.argv) != 2:
            raise ValueError('Exactly one action required')
        raw = sys.stdin.buffer.read(16385)
        if len(raw) > 16384:
            raise ValueError('Request too large')
        result = dispatch(sys.argv[1], json.loads(raw) if raw.strip() else {})
        print(json.dumps({'ok': True, 'result': result}))
        return 0
    except Exception as error:
        safe = str(error) if isinstance(error, (ValueError, PermissionError)) else type(error).__name__
        print(json.dumps({'ok': False, 'error': safe}))
        return 1
