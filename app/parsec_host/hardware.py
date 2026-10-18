"""Read-only DRM inventory; PCI identity survives kernel card renumbering."""
import re
from pathlib import Path


def inventory(root=Path('/sys/class/drm')):
    devices = []
    for card in sorted(root.glob('card*')):
        if not re.fullmatch(r'card[0-9]+', card.name):
            continue
        try:
            dev = (card / 'device').resolve(strict=True)
            pci = dev.name
            if not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]', pci):
                continue
            driver = (dev / 'driver').resolve(strict=True).name
            if driver not in ('nvidia', 'amdgpu', 'i915', 'xe', 'nouveau'):
                continue
            vendor = (dev / 'vendor').read_text().strip().removeprefix('0x')
            ident = (dev / 'device').read_text().strip().removeprefix('0x')
            outputs = []
            for connector in sorted(root.glob(card.name + '-*')):
                if (connector / 'status').read_text().strip() == 'connected':
                    outputs.append(connector.name.removeprefix(card.name + '-'))
            brand = {'10de': 'NVIDIA', '1002': 'AMD', '8086': 'Intel'}.get(vendor, vendor)
            devices.append(dict(pci=pci, card=card.name, vendor=vendor, driver=driver,
                                label=f'{brand} [{ident}] · {pci}', outputs=outputs))
        except (OSError, ValueError):
            continue
    return devices


def resolve(s, devices=None):
    devices = inventory() if devices is None else devices
    eligible = [d for d in devices if s['gpu'] in ('auto', d['pci'])]
    if s['output'] != 'auto':
        eligible = [d for d in eligible if s['output'] in d['outputs']]
    eligible.sort(key=lambda d: (not bool(d['outputs']), d['pci']))
    if not eligible or not eligible[0]['outputs']:
        raise ValueError('No connected display on the selected GPU; VFIO devices are excluded')
    return eligible[0]
