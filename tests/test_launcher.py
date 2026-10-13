"""Security-relevant checks for the Linux development launcher."""

import os
import stat
import unittest
from pathlib import Path
from unittest import mock

from scripts import launch


class LauncherTest(unittest.TestCase):
    def test_helper_requires_root_suid_and_no_group_or_world_write(self):
        helper = mock.Mock(spec=Path)
        for uid, mode, accepted in [
            (0, stat.S_IFREG | 0o4755, True),
            (1000, stat.S_IFREG | 0o4755, False),
            (0, stat.S_IFREG | 0o4775, False),
            (0, stat.S_IFREG | 0o4757, False),
            (0, stat.S_IFREG | 0o755, False),
            (0, stat.S_IFDIR | 0o4755, False),
        ]:
            with self.subTest(uid=uid, mode=mode):
                helper.stat.return_value = mock.Mock(st_uid=uid, st_mode=mode)
                with mock.patch.object(os, "access", return_value=True):
                    self.assertEqual(launch.installed_sandbox_helper(helper), accepted)

    def test_linux_launch_selects_existing_helper_and_preserves_arguments(self):
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(
            launch.sys, "platform", "linux"
        ), mock.patch.object(launch, "installed_sandbox_helper", return_value=True), mock.patch.object(
            launch, "build_main", return_value=0
        ) as run:
            self.assertEqual(launch.main(["--ozone-platform=wayland", "about:blank"]), 0)
            self.assertEqual(os.environ["CHROME_DEVEL_SANDBOX"], "/opt/google/chrome/chrome-sandbox")
            self.assertEqual(os.environ["CHROME_DESKTOP"], "browser-final-dev.desktop")
            run.assert_called_once_with(["run", "--", "--ozone-platform=wayland", "about:blank"])

    def test_explicit_helper_is_preserved_and_empty_helper_is_rejected(self):
        with mock.patch.object(launch.sys, "platform", "linux"), mock.patch.object(
            launch, "build_main", return_value=0
        ) as run:
            with mock.patch.dict(os.environ, {"CHROME_DEVEL_SANDBOX": "/explicit/helper"}, clear=True):
                self.assertEqual(launch.main([]), 0)
                self.assertEqual(os.environ["CHROME_DEVEL_SANDBOX"], "/explicit/helper")
            run.reset_mock()
            with mock.patch.dict(os.environ, {"CHROME_DEVEL_SANDBOX": ""}, clear=True):
                self.assertEqual(launch.main([]), 2)
                run.assert_not_called()

    def test_wayland_session_selects_wayland_and_preserves_explicit_platform(self):
        with mock.patch.dict(os.environ, {"WAYLAND_DISPLAY": "wayland-0"}, clear=True), mock.patch.object(
            launch.sys, "platform", "linux"
        ), mock.patch.object(launch, "installed_sandbox_helper", return_value=False), mock.patch.object(
            launch, "build_main", return_value=0
        ) as run:
            launch.main(["about:blank"])
            run.assert_called_with(["run", "--", "--ozone-platform=wayland", "about:blank"])
            launch.main(["--ozone-platform=x11"])
            run.assert_called_with(["run", "--", "--ozone-platform=x11"])
