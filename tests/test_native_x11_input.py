"""Native keyboard tests must refuse unrelated or ambiguous desktop clients."""
import subprocess
import unittest
from unittest import mock

from scripts.native_x11_input import NativeX11Input


class NativeX11InputScopeTest(unittest.TestCase):
    def test_other_process_cannot_receive_fixture_keys(self):
        with mock.patch("scripts.native_x11_input.subprocess.run", side_effect=[
            subprocess.CompletedProcess([], 0, "_NET_CLIENT_LIST = 0xa\n"),
            subprocess.CompletedProcess([], 0, '_NET_WM_PID = 1234\n_NET_WM_NAME = "Blynx command center"\n'),
        ]), mock.patch("scripts.native_x11_input.ctypes.CDLL") as library:
            with self.assertRaisesRegex(RuntimeError, "identify one X11 client"):
                NativeX11Input(123, "Blynx command center")
            library.assert_not_called()

    def test_two_owned_windows_require_an_unambiguous_target(self):
        with mock.patch("scripts.native_x11_input.subprocess.run", side_effect=[
            subprocess.CompletedProcess([], 0, "_NET_CLIENT_LIST = 0xa, 0xb\n"),
            subprocess.CompletedProcess([], 0, '_NET_WM_PID = 123\n_NET_WM_NAME = "First fixture"\n'),
            subprocess.CompletedProcess([], 0, '_NET_WM_PID = 123\n_NET_WM_NAME = "Second fixture"\n'),
        ]), mock.patch("scripts.native_x11_input.ctypes.CDLL") as library:
            with self.assertRaisesRegex(RuntimeError, "identify one X11 client"):
                NativeX11Input(123, "Blynx command center")
            library.assert_not_called()

    def test_unsupported_text_is_refused_before_opening_the_display(self):
        driver = NativeX11Input.__new__(NativeX11Input)
        driver.x11 = mock.Mock()
        with self.assertRaisesRegex(ValueError, "ASCII"):
            driver.replace("non-ASCII \N{SNOWMAN}")
        driver.x11.XOpenDisplay.assert_not_called()

    def test_negative_pointer_coordinate_is_refused_before_display_access(self):
        driver = NativeX11Input.__new__(NativeX11Input)
        driver.x11 = mock.Mock()
        for action in (driver.move, driver.click):
            with self.assertRaisesRegex(ValueError, "inside its client"):
                action(-1, 10)
        driver.x11.XOpenDisplay.assert_not_called()
