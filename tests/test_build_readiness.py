"""Reject interrupted builds that mix a previous executable with new resources."""

import tempfile
import unittest
from pathlib import Path

from scripts import build
from scripts.bootstrap import ToolError, resolve_paths


class BuildReadinessTest(unittest.TestCase):
    def fixture(self, root):
        paths = resolve_paths(Path(root), platform_name="linux")
        paths.build_root.mkdir(parents=True)
        (paths.source_root / ".browser-final-overlay.json").write_text('{"revision":"test"}')
        (paths.build_root / "args.gn").write_text('is_debug=false\n')
        for relative in build.BROWSER_RUNTIME_FILES:
            path = paths.build_root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"test artifact")
        return paths

    def test_only_a_completed_consistent_build_can_launch(self):
        with tempfile.TemporaryDirectory() as root:
            paths = self.fixture(root)
            with self.assertRaisesRegex(ToolError, "incomplete"):
                build._require_completed_browser_build(paths)
            build._write_browser_build_receipt(paths)
            build._require_completed_browser_build(paths)
            # This is the observed failure: PAK generation succeeds but chrome
            # compilation/linking fails, leaving the old executable in place.
            (paths.build_root / "resources.pak").write_bytes(b"new resources")
            with self.assertRaisesRegex(ToolError, "stale"):
                build._require_completed_browser_build(paths)

    def test_changes_to_integration_or_args_require_a_new_build(self):
        with tempfile.TemporaryDirectory() as root:
            paths = self.fixture(root)
            build._write_browser_build_receipt(paths)
            (paths.source_root / ".browser-final-overlay.json").write_text('{"revision":"changed"}')
            with self.assertRaises(ToolError):
                build._require_completed_browser_build(paths)
            build._write_browser_build_receipt(paths)
            (paths.build_root / "args.gn").write_text('is_debug=true\n')
            with self.assertRaises(ToolError):
                build._require_completed_browser_build(paths)

    def test_symlinked_receipts_and_runtime_files_are_refused(self):
        with tempfile.TemporaryDirectory() as root:
            paths = self.fixture(root)
            receipt = paths.build_root / build.BROWSER_BUILD_RECEIPT
            receipt.symlink_to(paths.build_root / "resources.pak")
            with self.assertRaises(ToolError):
                build._require_completed_browser_build(paths)
            receipt.unlink()
            artifact = paths.build_root / "chrome"
            artifact.unlink()
            artifact.symlink_to(paths.build_root / "resources.pak")
            with self.assertRaises(ToolError):
                build._write_browser_build_receipt(paths)
