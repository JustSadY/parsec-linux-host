#!/usr/bin/python3 -I
"""One-time removal of the older manual installation, preserving its account.

Run only after installing the new package. Copies private session bytes without
printing or parsing them. No third-party application/profile is removed.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import pwd
import shutil
import stat
import subprocess
import sys


def main():
    if os.geteuid() != 0:
        raise SystemExit('Run this migration as root, after package installation.')
    parser = argparse.ArgumentParser()
    parser.add_argument('--uid', required=True, type=int)
    args = parser.parse_args()
    user = pwd.getpwuid(args.uid)
    if args.uid < 1000:
        raise SystemExit('A desktop user UID is required.')
    lib = Path('/usr/lib/parsec-linux-host')
    if not (lib / 'vendor/appdata.json').is_file() or not (lib / 'hostctl').is_file():
        raise SystemExit('Install the native Host package first.')
    if lib.stat().st_uid or lib.stat().st_mode & 0o022:
        raise SystemExit('Installed package directory must be root-owned and not writable by others.')
    sys.path.insert(0, str(lib))
    from parsec_host import config as c, control
    old = Path('/etc/parsec-linux-host')
    if old.exists():
        if old.is_symlink() or old.stat().st_uid:
            raise SystemExit('Unexpected legacy configuration ownership.')
        try:
            existing_owner = control.owner()
        except FileNotFoundError:
            existing_owner = None
        if existing_owner and existing_owner['uid'] != args.uid:
            raise SystemExit('The destination host belongs to another account.')
        backup = Path('/var/lib/parsec-host') / ('legacy-config-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
        backup.mkdir(parents=True, mode=0o700)
        os.chmod(backup.parent, 0o700)
        for name in ('account.json', 'account-info.json', 'settings.json'):
            source = old / name
            if not source.exists():
                continue
            info = source.lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_uid:
                raise SystemExit('Unexpected legacy file ownership/type.')
            shutil.copyfile(source, backup / name)
            (backup / name).chmod(0o600)
        c.ETC.mkdir(parents=True, exist_ok=True)
        account = old / 'account.json'
        if account.exists() and not (c.ETC / 'account.json').exists():
            shutil.copyfile(account, c.ETC / 'account.json')
            (c.ETC / 'account.json').chmod(0o600)
            if (old / 'account-info.json').exists():
                shutil.copyfile(old / 'account-info.json', c.ETC / 'account-info.json')
                (c.ETC / 'account-info.json').chmod(0o644)
        previous = json.loads((old / 'settings.json').read_text()) if (old / 'settings.json').exists() else {}
        migrated = c.validate({**c.DEFAULT, **previous, 'gpu': '0000:01:00.0',
            'encoder': 'nvenc', 'mute_speakers': False, 'clipboard': False})
        control.atomic(c.ETC / 'settings.json', migrated)
        control.atomic(c.ETC / 'owner.json', dict(uid=args.uid, gid=user.pw_gid))
        dropin = Path('/etc/systemd/system/parsec-host.service.d')
        dropin.mkdir(parents=True, exist_ok=True)
        (dropin / 'owner.conf').write_text(f'[Service]\nGroup={user.pw_gid}\n')
        (dropin / 'owner.conf').chmod(0o644)
        print('Host account and public settings migrated. Private configuration backup:', backup)
    subprocess.run(['systemctl', 'disable', '--now', 'parsec-linux-host.service'], check=False)
    for path in (Path('/usr/local/lib/parsec-linux-host'), old):
        if path.exists():
            if path.is_symlink() or path.stat().st_uid:
                raise SystemExit('Refusing unexpected legacy path: ' + str(path))
            shutil.rmtree(path)
    for path in map(Path, ('/usr/local/libexec/parsec-hostctl',
        '/etc/systemd/system/parsec-linux-host.service',
        '/usr/lib/systemd/user/parsec-linux-host-session.service',
        '/usr/share/polkit-1/actions/local.parsec-host.manage.policy')):
        if path.exists():
            if path.is_symlink() or path.stat().st_uid:
                raise SystemExit('Refusing unexpected legacy file: ' + str(path))
            path.unlink()
    subprocess.run(['systemctl', 'daemon-reload'], check=True)
    print('Old manual host application, helper, units and policy removed.')


if __name__ == '__main__':
    main()
