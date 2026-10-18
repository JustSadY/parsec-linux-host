import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from parsec_host import config as c, control


class Boundary(unittest.TestCase):
    def test_unprivileged_mutations_are_rejected(self):
        with patch.object(os, 'geteuid', return_value=1000):
            for action in ('save', 'login', 'start', 'stop', 'logs'):
                with self.subTest(action=action), self.assertRaises(PermissionError):
                    control.dispatch(action, {})

    def test_unknown_actions_cannot_reach_systemctl(self):
        with patch.object(os, 'geteuid', return_value=0), patch.object(control, 'run') as run:
            with self.assertRaises(ValueError):
                control.dispatch('enable-other-service', {})
            run.assert_not_called()

    def test_session_not_returned_and_mode_is_private(self):
        def api(method, path, body=None, token=None):
            if method == 'POST':
                self.assertEqual(path, '/v2/auth')
                return {'session_id': 'TEST-TOKEN-NOT-A-REAL-SESSION'}
            return {'data': {'name': 'Fixture'}}
        with tempfile.TemporaryDirectory() as directory, patch.object(c, 'ETC', Path(directory)), \
             patch.object(os, 'geteuid', return_value=0), patch.object(control, 'claim_owner'), \
             patch.object(control, 'status', return_value={'service': {'ActiveState': 'inactive'}}), \
             patch.object(control, 'request', side_effect=api):
            result = control.dispatch('login', {'email': 'fixture@example.invalid', 'password': 'never-store-me'})
            path = Path(directory) / 'account.json'
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertNotIn('never-store-me', path.read_text())
            self.assertNotIn('TEST-TOKEN', json.dumps(result))
            self.assertEqual(json.loads(path.read_text())['session_id'], 'TEST-TOKEN-NOT-A-REAL-SESSION')
