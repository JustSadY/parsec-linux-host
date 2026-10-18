#!/usr/bin/python3
"""Development launch uses a temporary profile, never an installed root helper."""
from pathlib import Path
from parsec_host.native import launch

root = Path(__file__).resolve().parents[1]
loader = Path('/usr/lib/parsec-linux-host/vendor/parsecd')
if not loader.exists():
    loader = Path('/usr/bin/parsecd')
raise SystemExit(launch(temporary=True, vendor=loader, module_dir=root / '.cache/native', read_only=True))
