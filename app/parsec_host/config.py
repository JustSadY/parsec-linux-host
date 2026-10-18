"""Validated, machine-independent settings. Never execute configuration as code."""
import json
import re
import socket
from pathlib import Path

LIB = Path('/usr/lib/parsec-linux-host')
ETC = Path('/etc/parsec-host')
UNIT = 'parsec-host.service'
USER_UNIT = 'parsec-host-session.service'
HELPER = '/usr/lib/parsec-linux-host/hostctl'
VENDOR = LIB / 'vendor/parsecd'
DEFAULT = dict(name='', gpu='auto', output='auto', fps=60, bitrate=10, codec='h264',
               encoder='auto', conversion='auto', quality='lowest-latency',
               full_fps=False, mute_speakers=True, clipboard=True, autostart=False,
               client_decoder='hardware', client_renderer='opengl')
CHOICES = dict(codec=('h264', 'h265'), encoder=('auto', 'nvenc', 'vaapi'),
               conversion=('auto', 'vulkan', 'opengl'),
               quality=('lowest-latency', 'balanced', 'highest'),
               client_decoder=('hardware', 'software'), client_renderer=('opengl', 'vulkan'))


def validate(data):
    if not isinstance(data, dict) or set(data) - set(DEFAULT):
        raise ValueError('Unknown setting')
    s = {**DEFAULT, **data}
    if not isinstance(s['name'], str) or len(s['name']) > 64 or any(ord(c) < 32 for c in s['name']):
        raise ValueError('Invalid host name')
    s['name'] = s['name'].strip()
    if s['name'].startswith('-'):
        raise ValueError('Host name cannot start with a dash')
    if not isinstance(s['gpu'], str) or not re.fullmatch(r'auto|[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]', s['gpu']):
        raise ValueError('Invalid GPU PCI address')
    if not isinstance(s['output'], str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,40}', s['output']):
        raise ValueError('Invalid display connector')
    for key, low, high in [('fps', 30, 240), ('bitrate', 5, 100)]:
        if type(s[key]) is not int or not low <= s[key] <= high:
            raise ValueError(f'{key} must be between {low} and {high}')
    for key, choices in CHOICES.items():
        if s[key] not in choices:
            raise ValueError(f'Unsupported {key}')
    for key in ('full_fps', 'mute_speakers', 'clipboard', 'autostart'):
        if type(s[key]) is not bool:
            raise ValueError(f'{key} must be a boolean')
    if s['encoder'] == 'nvenc' and s['conversion'] == 'opengl':
        raise ValueError('NVENC requires Vulkan capture conversion; use Automatic or Vulkan')
    return s


def load():
    try:
        return validate(json.loads((ETC / 'settings.json').read_text()))
    except FileNotFoundError:
        return DEFAULT.copy()


def host_args(s, device):
    s = validate(s)
    if not re.fullmatch(r'card[0-9]+', device['card']):
        raise ValueError('Invalid DRM card')
    if s['encoder'] == 'nvenc' and device['vendor'] != '10de':
        raise ValueError('NVENC needs an NVIDIA GPU')
    if s['encoder'] == 'vaapi' and device['vendor'] == '10de':
        raise ValueError('Select NVENC or Automatic for NVIDIA')
    args = [str(LIB / 'lowlatd'), '--name', s['name'] or socket.gethostname(),
            '--output', device['card'] + ':' + (s['output'] if s['output'] != 'auto' else device['outputs'][0]),
            '--fps', str(s['fps']), '--bitrate', str(s['bitrate']), '--codec', s['codec'],
            '--quality', s['quality'], '--max-guests', '1', '--owner-only',
            '--clipboard', 'both' if s['clipboard'] else 'off']
    if s['encoder'] != 'auto':
        args += ['--encoder', s['encoder']]
    if s['conversion'] != 'auto':
        args += ['--convert', s['conversion']]
    if not s['mute_speakers']:
        args += ['--no-host-mute']
    if s['full_fps']:
        args += ['--full-fps']
    return args


def client_args(s):
    s = validate(s)
    return [f"decoder_software={int(s['client_decoder'] == 'software')}:client_renderer={0 if s['client_renderer'] == 'opengl' else 5}"]
