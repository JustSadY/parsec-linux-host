import argparse
import unittest
from PySide6.QtWidgets import QApplication
from parsec_host.app import Main
from parsec_host.config import DEFAULT


class NativeUI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def test_preview_is_native_read_only_and_defaults_round_trip(self):
        window = Main(argparse.Namespace(preview=True, snapshot=None, exit_after=0))
        self.assertEqual(window.values(), DEFAULT)
        self.assertFalse(window.save.isEnabled())
        self.assertFalse(window.hosting.isEnabled())
        self.assertEqual(window.tabs.currentIndex(), 1)
        self.assertEqual(window.foreign, None)
        window.poll.stop()
        window.close()
