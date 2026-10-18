import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch
from parsec_host import bridge, config as c


class NativeBoundary(unittest.TestCase):
    def test_compiled_c_and_python_share_the_same_abi(self):
        root = Path(__file__).resolve().parents[1]
        source = '#include <stddef.h>\n#include <stdio.h>\n#include "bridge.h"\nint main(void){printf("%zu %zu %zu %zu",sizeof(struct Bridge),offsetof(struct Bridge,gpu),offsetof(struct Bridge,gpu_count),offsetof(struct Bridge,gpu_labels));}'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'abi'
            subprocess.run(['cc', '-I', str(root / 'native'), '-x', 'c', '-', '-o', str(binary)], input=source, text=True, check=True)
            actual = list(map(int, subprocess.check_output([str(binary)], text=True).split()))
        self.assertEqual(actual, [ctypes.sizeof(bridge.State), bridge.State.gpu.offset,
            bridge.State.gpu_count.offset, bridge.State.gpu_labels.offset])

    def test_read_only_ui_requests_cannot_mutate_or_open_login(self):
        def call(action, *_, **__):
            self.assertEqual(action, 'status')
            return dict(configured=True, service={'ActiveState': 'inactive'}, settings=c.DEFAULT.copy())
        with patch.object(bridge.NativeBridge, 'call', side_effect=call), \
             patch.object(bridge.NativeBridge, 'login') as login, bridge.NativeBridge(read_only=True) as worker:
            for seq, action in enumerate((1, 2, 3, 5, 99), 1):
                worker.state.action, worker.state.seq, worker.state.busy = action, seq, 1
                deadline = time.monotonic() + 2
                while worker.state.ack != seq and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertEqual(worker.state.ack, seq)
                self.assertEqual(worker.state.busy, 0)
            login.assert_not_called()

    def test_native_form_is_revalidated_before_privileged_save(self):
        worker = bridge.NativeBridge()
        worker.state = bridge.State()
        worker.settings = c.DEFAULT.copy()
        worker.state.gpu = b'auto'
        worker.state.output = b'auto'
        worker.state.fps, worker.state.bitrate = 60, 10
        self.assertEqual(worker.form()['gpu'], 'auto')
        worker.state.encoder = 99
        with self.assertRaises(ValueError):
            worker.form()
        worker.state.encoder = 0
        worker.state.name = b'--unexpected-option'
        with self.assertRaises(ValueError):
            worker.form()
