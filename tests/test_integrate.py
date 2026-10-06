"""Offline integration preflight/path checks using a tiny real Git checkout."""
from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import build, integrate
from scripts.bootstrap import ToolError, resolve_paths


PATCH = """diff --git a/upstream.txt b/upstream.txt
--- a/upstream.txt
+++ b/upstream.txt
@@ -1 +1 @@
-upstream
+fork
"""


@unittest.skipUnless(shutil.which("git"), "integration tests require local Git (no network)")
class IntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name) / "browser-fork"
        self.repo.mkdir()
        environment = mock.patch.dict(os.environ, {
            "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull,
            "GIT_TERMINAL_PROMPT": "0", "CHROMIUM_SOURCE_ROOT": "", "CHROMIUM_BUILD_ROOT": "",
        })
        environment.start()
        self.addCleanup(environment.stop)
        self.paths = resolve_paths(self.repo, platform_name="linux")
        self.source = self.paths.source_root
        self.source.mkdir(parents=True)
        (self.source / "DEPS").write_text("", encoding="utf-8")
        (self.source / "upstream.txt").write_text("upstream\n", encoding="utf-8")
        (self.source / "second.txt").write_text("original\n", encoding="utf-8")
        self.git("init", "--quiet")
        self.git("add", "DEPS", "upstream.txt", "second.txt")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "--quiet", "-m", "fixture")
        self.revision = self.git("rev-parse", "HEAD").stdout.strip()
        pin = json.loads((Path(__file__).resolve().parents[1] / "config/upstream.json").read_text(encoding="utf-8"))
        pin["chromium_revision"] = self.revision
        (self.repo / "config").mkdir()
        (self.repo / "config/upstream.json").write_text(json.dumps(pin), encoding="utf-8")
        (self.repo / "patches").mkdir()
        self.patch = self.repo / "patches/0001-test.patch"
        self.patch.write_text(PATCH, encoding="utf-8")
        self.series = self.repo / "patches/series"
        self.series.write_text("0001-test.patch\n", encoding="utf-8")
        (self.repo / "browser").mkdir()
        self.origin = self.repo / "browser/BUILD.gn"
        self.origin.write_text('group("owned") {}\n', encoding="utf-8")
        self.target = self.source / "browser/BUILD.gn"
        self.manifest = self.source / ".browser-final-overlay.json"

    def git(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(["git", *args], cwd=self.source, capture_output=True,
                              text=True, check=True)

    def snapshot(self) -> dict:
        return {path.relative_to(self.repo).as_posix(): path.read_bytes()
                for path in self.repo.rglob("*") if path.is_file() and not path.is_symlink()}

    def install(self, check_only: bool = False) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            integrate.integrate(self.repo, self.source, check_only)

    def test_read_only_preflight_leaves_entire_fixture_unchanged(self) -> None:
        before = self.snapshot()
        self.install(check_only=True)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.manifest.exists())
        self.assertFalse(self.target.exists())

    def test_install_is_idempotent_and_writes_a_verifiable_manifest(self) -> None:
        self.install()
        self.assertEqual((self.source / "upstream.txt").read_text(encoding="utf-8"), "fork\n")
        self.assertEqual(self.target.read_bytes(), self.origin.read_bytes())
        record = json.loads(self.manifest.read_text(encoding="utf-8"))
        self.assertEqual(record, {
            "revision": self.revision,
            "files": {"browser/BUILD.gn": integrate.digest(self.origin.read_bytes())},
            "patches": {"0001-test.patch": integrate.digest(self.patch.read_bytes())},
        })
        self.assertEqual(self.git("rev-parse", "HEAD").stdout.strip(), self.revision)
        before = self.snapshot()
        self.install()
        self.assertEqual(self.snapshot(), before)
        self.install(check_only=True)

    def test_wrong_revision_fails_before_writes(self) -> None:
        path = self.repo / "config/upstream.json"
        pin = json.loads(path.read_text(encoding="utf-8"))
        pin["chromium_revision"] = "0" * 40
        path.write_text(json.dumps(pin), encoding="utf-8")
        before = self.snapshot()
        with self.assertRaisesRegex(ToolError, "pinned Chromium"):
            self.install()
        self.assertEqual(self.snapshot(), before)

    def test_malformed_receipt_shapes_fail_before_writes(self) -> None:
        for record in ([], "invalid", {"files": []}, {"files": {"path": 1}}):
            with self.subTest(record=record):
                self.manifest.write_text(json.dumps(record), encoding="utf-8")
                before = self.snapshot()
                with self.assertRaisesRegex(ToolError, "Invalid overlay"):
                    self.install()
                self.assertEqual(self.snapshot(), before)

    def test_nested_directory_is_not_accepted_as_its_parent_checkout(self) -> None:
        nested = self.source / "nested"
        nested.mkdir()
        with self.assertRaisesRegex(ToolError, "own Git checkout"):
            integrate.integrate(self.repo, nested)
        self.assertFalse(self.manifest.exists())

    def test_patch_series_traversal_and_absolute_paths_are_rejected(self) -> None:
        for entry in ("../escape.patch", str(self.patch), "not-a-patch.txt"):
            with self.subTest(entry=entry):
                self.series.write_text(entry + "\n", encoding="utf-8")
                before = self.snapshot()
                with self.assertRaisesRegex(ToolError, "Invalid patch series entry"):
                    self.install()
                self.assertEqual(self.snapshot(), before)

    def test_missing_symlinked_and_empty_patch_series_are_rejected(self) -> None:
        self.series.write_text("missing.patch\n", encoding="utf-8")
        with self.assertRaisesRegex(ToolError, "Missing or symlinked patch"):
            self.install()
        self.series.write_text("# no patches\n", encoding="utf-8")
        with self.assertRaisesRegex(ToolError, "Empty patch series"):
            self.install()
        elsewhere = self.repo / "external.patch"
        elsewhere.write_text(PATCH, encoding="utf-8")
        link = self.repo / "patches/link.patch"
        link.symlink_to(elsewhere)
        self.series.write_text("link.patch\n", encoding="utf-8")
        with self.assertRaisesRegex(ToolError, "Missing or symlinked patch"):
            self.install()
        self.assertEqual((self.source / "upstream.txt").read_text(encoding="utf-8"), "upstream\n")

    def test_patch_conflicts_and_mixed_application_refuse_overlay_writes(self) -> None:
        upstream = self.source / "upstream.txt"
        upstream.write_text("local edit\n", encoding="utf-8")
        with self.assertRaisesRegex(ToolError, "Patch preflight failed"):
            self.install()
        self.assertFalse(self.target.exists())
        self.assertEqual(upstream.read_text(encoding="utf-8"), "local edit\n")
        upstream.write_text("upstream\n", encoding="utf-8")
        second = self.repo / "patches/0002-test.patch"
        second.write_text(PATCH.replace("upstream.txt", "second.txt").replace("-upstream", "-original").replace("+fork", "+custom"), encoding="utf-8")
        self.series.write_text("0001-test.patch\n0002-test.patch\n", encoding="utf-8")
        self.git("apply", str(self.patch))
        before = self.snapshot()
        with self.assertRaisesRegex(ToolError, "Patch preflight failed"):
            self.install()
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.manifest.exists())

    def test_unowned_existing_overlay_and_owned_local_edits_are_preserved(self) -> None:
        self.target.parent.mkdir()
        self.target.write_text("local edit", encoding="utf-8")
        before = self.snapshot()
        with self.assertRaisesRegex(ToolError, "unowned/local edits"):
            self.install()
        self.assertEqual(self.snapshot(), before)
        self.target.write_bytes(self.origin.read_bytes())
        self.install()
        self.target.write_text("new local edit", encoding="utf-8")
        before = self.snapshot()
        with self.assertRaisesRegex(ToolError, "unowned/local edits"):
            self.install()
        self.assertEqual(self.snapshot(), before)

    def test_owned_overlay_can_be_updated_but_removed_files_need_explicit_review(self) -> None:
        self.install()
        self.origin.write_text('group("updated") {}\n', encoding="utf-8")
        self.install()
        self.assertEqual(self.target.read_bytes(), self.origin.read_bytes())
        self.origin.unlink()
        before = self.snapshot()
        with self.assertRaisesRegex(ToolError, "Overlay files were removed"):
            self.install()
        self.assertEqual(self.snapshot(), before)
        self.assertTrue(self.target.exists())

    def test_symlinked_input_destination_ancestor_and_manifest_are_rejected(self) -> None:
        external = self.repo / "outside.txt"
        external.write_text("untouched", encoding="utf-8")
        input_link = self.repo / "browser/link.txt"
        input_link.symlink_to(external)
        with self.assertRaisesRegex(ToolError, "symlinked overlay input"):
            self.install()
        input_link.unlink()
        self.target.parent.mkdir()
        self.target.symlink_to(external)
        with self.assertRaisesRegex(ToolError, "symlinked overlay destination"):
            self.install()
        self.target.unlink()
        self.target.parent.rmdir()
        (self.source / "browser").symlink_to(self.repo / "browser", target_is_directory=True)
        with self.assertRaisesRegex(ToolError, "symlinked overlay destination"):
            self.install()
        (self.source / "browser").unlink()
        self.manifest.symlink_to(external)
        with self.assertRaisesRegex(ToolError, "symlinked overlay manifest"):
            self.install()
        self.assertEqual(external.read_text(encoding="utf-8"), "untouched")
        self.assertEqual((self.source / "upstream.txt").read_text(encoding="utf-8"), "upstream\n")

    def test_destination_directory_is_not_overwritten(self) -> None:
        self.target.mkdir(parents=True)
        with self.assertRaisesRegex(ToolError, "destination is not a file"):
            self.install()
        self.assertTrue(self.target.is_dir())
        self.assertFalse(self.manifest.exists())

    def test_reserved_temporary_file_is_preserved_and_partial_install_cannot_build(self) -> None:
        self.target.parent.mkdir()
        temporary = self.target.with_name(self.target.name + ".browser-final-tmp")
        temporary.write_text("preexisting", encoding="utf-8")
        before = self.snapshot()
        with self.assertRaisesRegex(ToolError, "Unexpected temporary file"):
            self.install()
        self.assertEqual(temporary.read_text(encoding="utf-8"), "preexisting")
        self.assertFalse(self.target.exists())
        self.assertEqual(self.snapshot(), before)
        # Collision is checked before applying any patches or writing overlays.
        with self.assertRaisesRegex(ToolError, "fork integration manifest"):
            build._require_integration(self.paths)

    def test_configure_accepts_installed_fork_and_rejects_stale_or_unapplied_receipts(self) -> None:
        self.install()
        build._require_integration(self.paths)
        with mock.patch.object(build, "find_gn", return_value="gn"), mock.patch.object(build, "run_command", return_value=0) as runner, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(build.configure(self.paths, "linux", "release", dry_run=False), 0)
            self.assertEqual(runner.call_args.args[0], ["gn", "gen", str(self.paths.build_root)])
            self.assertTrue((self.paths.build_root / "args.gn").is_file())
        self.git("apply", "--reverse", str(self.patch))
        with self.assertRaisesRegex(ToolError, "Invalid fork integration"):
            build._require_integration(self.paths)
        self.git("apply", str(self.patch))
        self.origin.write_text("changed owned overlay", encoding="utf-8")
        with self.assertRaisesRegex(ToolError, "missing or edited overlay"):
            build._require_integration(self.paths)

    def test_build_gate_refuses_symlinked_receipt_and_overlay(self) -> None:
        self.install()
        saved = self.repo / "saved-manifest.json"
        saved.write_bytes(self.manifest.read_bytes())
        self.manifest.unlink()
        self.manifest.symlink_to(saved)
        with self.assertRaisesRegex(ToolError, "symlinked fork integration manifest"):
            build._require_integration(self.paths)
        self.manifest.unlink()
        self.manifest.write_bytes(saved.read_bytes())
        self.target.unlink()
        self.target.symlink_to(self.origin)
        with self.assertRaisesRegex(ToolError, "symlinked overlay destination"):
            build._require_integration(self.paths)


if __name__ == "__main__":
    unittest.main()
