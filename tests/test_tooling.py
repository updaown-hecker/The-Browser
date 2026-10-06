from __future__ import annotations

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import bootstrap, build
from scripts.bootstrap import ToolError, generate_gn_args, resolve_paths
from scripts.build import _validate_browser_args, _validate_target


PIN = json.loads((Path(__file__).resolve().parents[1] / "config" / "upstream.json").read_text(encoding="utf-8"))


def write_pin(repo: Path) -> None:
    (repo / "config").mkdir(exist_ok=True)
    (repo / "config" / "upstream.json").write_text(json.dumps(PIN), encoding="utf-8")


class BootstrapPathTests(unittest.TestCase):
    def setUp(self) -> None:
        environment = mock.patch.dict(os.environ, {}, clear=True)
        environment.start()
        self.addCleanup(environment.stop)

    def test_default_source_and_build_are_workspace_local(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            paths = resolve_paths(repo, platform_name="linux", config="release")
            self.assertEqual(paths.source_root, repo / ".local" / "chromium" / "src")
            self.assertTrue(paths.source_root.is_absolute())
            self.assertEqual(paths.build_root, paths.source_root / "out" / "linux-release")

    def test_explicit_relative_paths_are_resolved_from_project_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            paths = resolve_paths(
                repo,
                source_root="../chromium/src",
                build_root="../chromium/src/out/windows-debug",
                platform_name="windows",
                config="debug",
            )
            self.assertEqual(paths.source_root, Path(temporary) / "chromium" / "src")
            self.assertEqual(paths.build_root, paths.source_root / "out" / "windows-debug")

    def test_project_root_cannot_also_be_source_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            with self.assertRaises(ToolError):
                resolve_paths(repo, source_root=".", platform_name="linux")

    def test_source_cannot_contain_project_and_build_cannot_equal_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            with self.assertRaises(ToolError):
                resolve_paths(repo, source_root="..", platform_name="linux")
            with self.assertRaises(ToolError):
                resolve_paths(repo, source_root="../chromium/src", build_root="../chromium/src", platform_name="linux")

    def test_build_must_be_inside_chromium_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            with self.assertRaisesRegex(ToolError, "subdirectory"):
                resolve_paths(repo, build_root="out/Release", platform_name="linux")

    def test_empty_path_and_file_roots_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            file = Path(temporary) / "not-a-directory"
            file.write_text("data", encoding="utf-8")
            for source in ("", str(file), "bad\x00path"):
                with self.subTest(source=source), self.assertRaises(ToolError):
                    resolve_paths(repo, source_root=source, platform_name="linux")

    def test_checkout_metadata_cannot_be_build_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            with self.assertRaisesRegex(ToolError, "metadata"):
                resolve_paths(repo, source_root="../chromium/src", build_root="../chromium/src/.git/output", platform_name="linux")

    def test_environment_overrides_and_cli_precedence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            with mock.patch.dict(os.environ, {"CHROMIUM_SOURCE_ROOT": "../env/src"}):
                paths = resolve_paths(repo, platform_name="linux")
                self.assertEqual(paths.source_root, Path(temporary) / "env" / "src")
                paths = resolve_paths(repo, source_root="../explicit/src", platform_name="linux")
                self.assertEqual(paths.source_root, Path(temporary) / "explicit" / "src")

    def test_gn_args_are_deterministic_and_platform_specific(self) -> None:
        linux = generate_gn_args("linux", "release")
        self.assertEqual(linux, generate_gn_args("linux", "release"))
        self.assertIn('target_os = "linux"', linux)
        self.assertIn('target_cpu = "x64"', linux)
        self.assertIn("is_component_build = false", linux)
        self.assertIn("is_chrome_branded = false", linux)
        self.assertIn("proprietary_codecs = false", linux)
        self.assertNotIn("no_sandbox", linux)
        self.assertNotIn("disable_web_security", linux)

        windows = generate_gn_args("windows", "debug", target_cpu="arm64")
        self.assertIn('target_os = "win"', windows)
        self.assertIn('target_cpu = "arm64"', windows)
        self.assertIn("is_debug = true", windows)

    def test_invalid_cpu_is_rejected(self) -> None:
        with self.assertRaises(ToolError):
            generate_gn_args("linux", "release", target_cpu="i686")

    def test_unsafe_browser_switches_are_rejected(self) -> None:
        with self.assertRaises(ToolError):
            _validate_browser_args(["--no-sandbox"])
        with self.assertRaises(ToolError):
            _validate_browser_args(["--disable-web-security"])
        with self.assertRaises(ToolError):
            _validate_browser_args(["--disable-features=Other,SitePerProcess"])
        _validate_browser_args(["--user-data-dir=/development-profile", "https://example.org"])

    def test_target_options_and_traversal_are_rejected(self) -> None:
        for target in ("", "-t", "chrome unit_tests", "../chrome"):
            with self.subTest(target=target), self.assertRaises(ToolError):
                _validate_target(target)
        self.assertEqual(_validate_target("//chrome/test:unit_tests"), "chrome/test:unit_tests")


class BootstrapOperationTests(unittest.TestCase):
    def setUp(self) -> None:
        environment = mock.patch.dict(os.environ, {}, clear=True)
        environment.start()
        self.addCleanup(environment.stop)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name) / "browser"
        self.repo.mkdir()
        write_pin(self.repo)
        self.paths = resolve_paths(self.repo, platform_name="linux")

    def make_source(self) -> None:
        self.paths.source_root.mkdir(parents=True)
        (self.paths.source_root / "DEPS").write_text("", encoding="utf-8")
        (self.paths.source_root.parent / ".gclient").write_text("", encoding="utf-8")

    def test_bootstrap_plan_clones_tag_verifies_and_syncs_pin_without_hooks(self) -> None:
        self.paths.source_root.parent.mkdir(parents=True)
        keep = self.paths.source_root.parent / "notes.txt"
        keep.write_text("keep", encoding="utf-8")
        with mock.patch.object(bootstrap, "run_command", return_value=0) as runner, contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(bootstrap.bootstrap_source(self.paths, "linux", yes=False, dry_run=True), 0)
        commands = [call.args[0] for call in runner.call_args_list]
        self.assertEqual(len(commands), 3)
        self.assertEqual(commands[0][1:], ["clone", "--depth", "1", "--branch", PIN["chromium_version"],
                                         "--single-branch", PIN["chromium_url"], "src"])
        self.assertEqual(commands[1][1:], ["config", "--name", "src", "--unmanaged", PIN["chromium_url"]])
        self.assertEqual(commands[2][1:], ["sync", "--no-history", "--nohooks", "--revision", "src@" + PIN["chromium_revision"]])
        self.assertIn("Verify pinned HEAD " + PIN["chromium_revision"], output.getvalue())
        self.assertNotIn("fetch chromium", output.getvalue())
        self.assertTrue(all(call.args[1] == self.paths.source_root.parent for call in runner.call_args_list))
        self.assertEqual(keep.read_text(encoding="utf-8"), "keep")
        self.assertFalse(self.paths.source_root.exists())

    def test_hash_mismatch_stops_bootstrap_before_setup_and_sync(self) -> None:
        wrong_head = mock.Mock(returncode=0, stdout="c" * 40 + "\n")
        with mock.patch.object(bootstrap, "_which", return_value="git"), mock.patch.object(bootstrap, "find_depot_tool", return_value="gclient"), mock.patch.object(bootstrap, "run_command", return_value=0) as runner, mock.patch.object(bootstrap.subprocess, "run", return_value=wrong_head), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ToolError, "Pinned revision verification failed"):
                bootstrap.bootstrap_source(self.paths, "linux", yes=True, dry_run=False)
        self.assertEqual(runner.call_count, 1)
        self.assertEqual(runner.call_args.args[0][1], "clone")
        self.assertFalse((self.paths.source_root.parent / ".gclient").exists())

    def test_missing_tools_fail_before_source_destination_is_created(self) -> None:
        with mock.patch.object(bootstrap, "find_depot_tool", return_value=None), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ToolError, "tools --yes"):
                bootstrap.bootstrap_source(self.paths, "linux", yes=True, dry_run=False)
        self.assertFalse((self.repo / ".local").exists())

    def test_tools_setup_uses_pinned_revision_and_preserves_existing_destination(self) -> None:
        with mock.patch.object(bootstrap, "run_command", return_value=0) as runner, contextlib.redirect_stdout(io.StringIO()) as output:
            bootstrap.bootstrap_tools(self.paths, "linux", yes=False, dry_run=True)
        commands = [call.args[0] for call in runner.call_args_list]
        self.assertIn("--no-checkout", commands[0])
        self.assertEqual(commands[1][-5:], ["fetch", "--depth", "1", "origin", PIN["depot_tools_revision"]])
        self.assertEqual(commands[2][-3:], ["checkout", "--detach", PIN["depot_tools_revision"]])
        self.assertIn("Verify pinned HEAD " + PIN["depot_tools_revision"], output.getvalue())
        tools = bootstrap.depot_tools_root(self.repo)
        self.assertFalse(tools.exists())
        tools.mkdir(parents=True)
        keep = tools / "local-change"
        keep.write_text("keep", encoding="utf-8")
        with self.assertRaisesRegex(ToolError, "already exists"):
            bootstrap.bootstrap_tools(self.paths, "linux", yes=True, dry_run=False)
        self.assertEqual(keep.read_text(encoding="utf-8"), "keep")

    def test_sync_defaults_to_pin_and_hooks_are_a_separate_command(self) -> None:
        self.make_source()
        with mock.patch.object(bootstrap, "run_command", return_value=0) as runner, contextlib.redirect_stdout(io.StringIO()):
            bootstrap.update_source(self.paths, yes=False, dry_run=True)
            self.assertEqual(runner.call_args.args[0][1:], ["sync", "--no-history", "--nohooks", "--revision", "src@" + PIN["chromium_revision"]])
            bootstrap.run_hooks(self.paths, yes=False, dry_run=True)
            self.assertEqual(runner.call_args.args[0][1:], ["runhooks"])
            self.assertEqual(runner.call_args.args[1], self.paths.source_root.parent)

    def test_source_operations_refuse_parent_repository(self) -> None:
        self.make_source()
        responses = [mock.Mock(returncode=0, stdout=""), mock.Mock(returncode=0, stdout=str(self.repo) + "\n")]
        with mock.patch.object(bootstrap, "_which", return_value="git"), mock.patch.object(bootstrap, "find_depot_tool", return_value="gclient"), mock.patch.object(bootstrap.subprocess, "run", side_effect=responses), mock.patch.object(bootstrap, "run_command") as runner, contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ToolError, "own Git checkout"):
                bootstrap.update_source(self.paths, yes=True, dry_run=False)
            runner.assert_not_called()

    def test_setup_will_not_overwrite_gclient(self) -> None:
        self.make_source()
        marker = self.paths.source_root.parent / ".gclient"
        marker.write_text("owned", encoding="utf-8")
        with mock.patch.object(bootstrap, "run_command") as runner:
            with self.assertRaisesRegex(ToolError, "will not overwrite"):
                bootstrap.setup_source(self.paths, yes=True, dry_run=False)
            runner.assert_not_called()
        self.assertEqual(marker.read_text(encoding="utf-8"), "owned")

    def test_invalid_upstream_configuration_has_no_unpinned_fallback(self) -> None:
        invalid = dict(PIN, chromium_revision="main")
        (self.repo / "config/upstream.json").write_text(json.dumps(invalid), encoding="utf-8")
        with mock.patch.object(bootstrap, "run_command") as runner:
            with self.assertRaisesRegex(ToolError, "valid upstream pins"):
                bootstrap.clone_source(self.paths, "linux", yes=False, dry_run=True)
            runner.assert_not_called()
        self.assertFalse((self.repo / ".local").exists())

    def test_local_tools_are_discovered_and_only_child_environment_is_changed(self) -> None:
        tools = bootstrap.depot_tools_root(self.repo)
        tools.mkdir(parents=True)
        for name in ("gclient", "autoninja", "gn"):
            utility = tools / name
            utility.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            utility.chmod(0o755)
        with mock.patch.object(bootstrap, "_which", return_value=None):
            self.assertEqual(bootstrap.find_depot_tool("gclient", self.repo), str(tools / "gclient"))
            self.assertEqual(bootstrap.find_ninja(self.paths.source_root, self.repo), str(tools / "autoninja"))
            self.assertEqual(bootstrap.find_gn(self.paths.source_root, "linux", self.repo), str(tools / "gn"))
        with mock.patch.dict(os.environ, {"PATH": "/existing", "DEPOT_TOOLS_UPDATE": "1", "DEPOT_TOOLS_WIN_TOOLCHAIN": "1"}):
            environment = bootstrap.subprocess_environment(self.repo, "windows")
            self.assertEqual(environment["PATH"], str(tools) + os.pathsep + "/existing")
            self.assertEqual(environment["DEPOT_TOOLS_UPDATE"], "0")
            self.assertEqual(environment["DEPOT_TOOLS_WIN_TOOLCHAIN"], "0")
            self.assertEqual(os.environ["PATH"], "/existing")
            self.assertEqual(os.environ["DEPOT_TOOLS_UPDATE"], "1")
            self.assertEqual(os.environ["DEPOT_TOOLS_WIN_TOOLCHAIN"], "1")
        with mock.patch.object(bootstrap.subprocess, "run", return_value=mock.Mock(returncode=0)) as runner, contextlib.redirect_stdout(io.StringIO()):
            bootstrap.run_command([str(tools / "gclient"), "runhooks"], self.repo,
                                  repo_root=self.repo, platform_name="windows")
            self.assertEqual(runner.call_args.kwargs["env"]["DEPOT_TOOLS_WIN_TOOLCHAIN"], "0")
            self.assertEqual(runner.call_args.kwargs["env"]["DEPOT_TOOLS_UPDATE"], "0")
            self.assertTrue(runner.call_args.kwargs["env"]["PATH"].startswith(str(tools) + os.pathsep))

    def test_configure_requires_integration_before_writes_but_dry_run_still_works(self) -> None:
        self.make_source()
        with mock.patch.object(build, "run_command", return_value=0) as runner, contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ToolError, "fork integration manifest"):
                build.configure(self.paths, "linux", "release", dry_run=False)
            runner.assert_not_called()
            self.assertFalse(self.paths.build_root.exists())
            self.assertEqual(build.configure(self.paths, "linux", "release", dry_run=True), 0)
            self.assertFalse(self.paths.build_root.exists())

    def test_invalid_integration_manifest_is_rejected_before_gn(self) -> None:
        self.make_source()
        manifest = self.paths.source_root / ".browser-final-overlay.json"
        for record in ("{", "[]", json.dumps({"revision": "0" * 40, "files": {}, "patches": {}})):
            with self.subTest(record=record):
                manifest.write_text(record, encoding="utf-8")
                with self.assertRaisesRegex(ToolError, "Invalid fork integration"):
                    build.configure(self.paths, "linux", "release", dry_run=False)
                self.assertFalse(self.paths.build_root.exists())

    def test_packaging_fails_closed_for_all_targets_even_dry_runs(self) -> None:
        for target in ("chrome/installer/linux:stable_deb", "mini_installer", "chrome"):
            for dry_run in (False, True):
                with self.subTest(target=target, dry_run=dry_run), mock.patch.object(build, "build_target") as runner:
                    with self.assertRaisesRegex(ToolError, "Release not ready.*identity.*security audit"):
                        build.package_target(self.paths, target, None, dry_run)
                    runner.assert_not_called()
        self.assertFalse((self.repo / ".local").exists())


class ToolCliTests(unittest.TestCase):
    def setUp(self) -> None:
        environment = mock.patch.dict(os.environ, {}, clear=True)
        environment.start()
        self.addCleanup(environment.stop)

    def test_build_options_are_parsed_after_action(self) -> None:
        args = build.parse_args(["build", "--target", "unit_tests", "--dry-run", "-j", "2"])
        self.assertTrue(args.dry_run)
        self.assertEqual(args.target, "unit_tests")
        self.assertEqual(args.jobs, 2)

    def test_run_arguments_are_only_forwarded_after_separator(self) -> None:
        args = build.parse_args(["run", "--dry-run", "--", "--user-data-dir=/tmp/profile", "https://example.org"])
        self.assertEqual(args.browser_args, ["--user-data-dir=/tmp/profile", "https://example.org"])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            build.parse_args(["run", "--no-sandbox"])

    def test_source_operation_conflicts_and_read_only_check_are_rejected(self) -> None:
        for argv in (["bootstrap", "--update-source"], ["tools", "--clone-source"],
                     ["setup", "--run-hooks"], ["sync", "--bootstrap-tools"],
                     ["--check", "--write-gn-args"], ["check", "--write-gn-args"],
                     ["--check", "--bootstrap-source"], ["--check", "--bootstrap-tools"],
                     ["--check", "--clone-source"], ["--check", "--setup-source"],
                     ["--check", "--run-hooks"], ["hooks", "--write-gn-args"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(bootstrap.main(argv), 2)

    def test_gn_args_print_does_not_check_or_create_paths(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(bootstrap.main(["--print-gn-args", "--platform", "windows"]), 0)
        self.assertIn('target_os = "win"', output.getvalue())
        self.assertNotIn("[MISSING]", output.getvalue())

    def test_write_args_does_not_create_a_partial_chromium_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            with mock.patch.object(bootstrap, "project_root", return_value=repo), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(bootstrap.main(["--write-gn-args"]), 2)
            self.assertFalse((repo / ".local" / "chromium").exists())

    def test_dry_run_does_not_execute_or_write(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            write_pin(repo)
            with mock.patch.object(bootstrap, "project_root", return_value=repo), mock.patch.object(build, "project_root", return_value=repo):
                with mock.patch.object(bootstrap.subprocess, "run") as subprocess_run, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    for action in ("tools", "clone", "setup", "bootstrap"):
                        self.assertEqual(bootstrap.main([action, "--dry-run"]), 0)
                    self.assertEqual(bootstrap.main(["--write-gn-args", "--dry-run"]), 0)
                    self.assertEqual(build.main(["configure", "--dry-run"]), 0)
                    self.assertEqual(build.main(["build", "--dry-run"]), 0)
                    self.assertEqual(build.main(["test", "--dry-run"]), 0)
                    self.assertEqual(build.main(["package", "--dry-run"]), 2)
                    self.assertEqual(build.main(["run", "--dry-run"]), 0)
                    subprocess_run.assert_not_called()
            self.assertFalse((repo / ".local").exists())

    def test_args_file_is_not_silently_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            paths = resolve_paths(repo, platform_name="linux")
            written = bootstrap.write_gn_args(paths, "linux", "release")
            written.write_text("# local configuration\n", encoding="utf-8")
            with self.assertRaisesRegex(ToolError, "overwrite-args"):
                bootstrap.write_gn_args(paths, "linux", "release")
            self.assertEqual(written.read_text(encoding="utf-8"), "# local configuration\n")
            bootstrap.write_gn_args(paths, "linux", "release", overwrite=True)
            self.assertEqual(written.read_bytes(), generate_gn_args("linux", "release").encode("utf-8"))

    def test_development_run_uses_separate_profile_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            paths = resolve_paths(repo, platform_name="linux")
            with mock.patch.object(build, "run_command", return_value=0) as runner:
                build.run_browser(paths, "linux", None, ["https://example.org"], dry_run=True)
                command = runner.call_args[0][0]
                self.assertIn("--user-data-dir={}".format(paths.build_root / "development-profile"), command)
                build.run_browser(paths, "linux", None, ["--user-data-dir=/explicit/profile"], dry_run=True)
                command = runner.call_args[0][0]
                self.assertEqual(sum(arg.startswith("--user-data-dir=") for arg in command), 1)

    def test_bootstrap_destination_is_exact_and_conflicting_wrapper_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            paths = resolve_paths(repo, platform_name="linux")
            self.assertEqual(bootstrap._checkout_cwd(paths.source_root), paths.source_root.parent)
            paths.source_root.parent.mkdir(parents=True)
            (paths.source_root.parent / ".gclient").write_text("important", encoding="utf-8")
            with self.assertRaisesRegex(ToolError, "conflicting gclient"):
                bootstrap.bootstrap_source(paths, "linux", yes=True, dry_run=True)

    def test_noninteractive_source_action_needs_explicit_confirmation(self) -> None:
        with mock.patch.object(bootstrap.sys.stdin, "isatty", return_value=False):
            with self.assertRaisesRegex(ToolError, "without confirmation"):
                bootstrap._confirmation("test operation", yes=False, dry_run=False)

    def test_dirty_checkout_update_is_rejected_without_running_sync(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            write_pin(repo)
            paths = resolve_paths(repo, platform_name="linux")
            paths.source_root.mkdir(parents=True)
            (paths.source_root / "DEPS").write_text("", encoding="utf-8")
            (paths.source_root.parent / ".gclient").write_text("", encoding="utf-8")
            result = mock.Mock(returncode=0, stdout=" M DEPS\n")
            with mock.patch.object(bootstrap, "_which", return_value="git"), mock.patch.object(bootstrap, "find_depot_tool", return_value="gclient"), mock.patch.object(bootstrap.subprocess, "run", return_value=result) as runner:
                with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ToolError, "local changes"):
                    bootstrap.update_source(paths, yes=True, dry_run=False)
                self.assertEqual(runner.call_count, 1)

    def test_update_revision_is_explicit_and_gclient_runs_from_wrapper(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "browser"
            repo.mkdir()
            paths = resolve_paths(repo, platform_name="linux")
            paths.source_root.mkdir(parents=True)
            (paths.source_root / "DEPS").write_text("", encoding="utf-8")
            (paths.source_root.parent / ".gclient").write_text("", encoding="utf-8")
            with mock.patch.object(bootstrap, "run_command", return_value=0) as runner, contextlib.redirect_stdout(io.StringIO()):
                bootstrap.update_source(paths, yes=False, dry_run=True, revision="refs/tags/123.0.0.1")
                command, cwd = runner.call_args[0]
                self.assertEqual(command[-2:], ["--revision", "src@refs/tags/123.0.0.1"])
                self.assertEqual(cwd, paths.source_root.parent)
                self.assertNotIn("--force", command)
                self.assertNotIn("--reset", command)
                self.assertIn("--no-history", command)
                self.assertIn("--nohooks", command)
            with self.assertRaisesRegex(ToolError, "revision"):
                bootstrap.update_source(paths, yes=False, dry_run=True, revision="--force")


if __name__ == "__main__":
    unittest.main()
