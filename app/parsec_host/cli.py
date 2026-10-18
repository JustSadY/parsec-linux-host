"""Desktop entry and one-time account setup. Never place secrets in argv."""
import argparse
import getpass
import json
import subprocess
import sys
from . import config as c
from .native import launch


def main():
    parser = argparse.ArgumentParser(description='Parsec with an in-process Linux Host settings page')
    parser.add_argument('action', nargs='?', choices=('open', 'login', 'status', 'start', 'stop'), default='open')
    parser.add_argument('--temporary-profile', action='store_true')
    args, vendor_args = parser.parse_known_args()
    try:
        if args.action == 'open':
            return launch(vendor_args, temporary=args.temporary_profile)
        if vendor_args:
            parser.error('Unexpected arguments')
        data = {}
        if args.action == 'login':
            if not sys.stdin.isatty():
                raise ValueError('Run login in an interactive terminal.')
            data = dict(email=input('Parsec email: ').strip(), password=getpass.getpass('Parsec password: '))
            tfa = input('Two-factor code (Enter if unused): ').strip()
            if tfa:
                data['tfa'] = tfa
        command = ([] if args.action == 'status' else ['/usr/bin/pkexec']) + [c.HELPER, args.action]
        result = subprocess.run(command, input=json.dumps(data), text=True, capture_output=True, timeout=120)
        reply = json.loads(result.stdout)
        if not reply.get('ok'):
            raise ValueError(reply.get('error', 'Host controller failed'))
        print(json.dumps(reply['result'], indent=2) if args.action == 'status' else 'Done.')
        return result.returncode
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
