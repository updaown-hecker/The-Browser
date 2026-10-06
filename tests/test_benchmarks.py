from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from benchmarks import run


def proc_stat(pid: int, *, ppid: int = 1, session: int = 300, start: int = 100,
              cpu: tuple[int, int] = (10, 5), rss: int = 20, state: str = "S") -> str:
    fields = ["0"] * 22
    fields[0] = state
    fields[1] = str(ppid)
    fields[2] = str(session)
    fields[3] = str(session)
    fields[11] = str(cpu[0])
    fields[12] = str(cpu[1])
    fields[19] = str(start)
    fields[21] = str(rss)
    return "{} (browser (helper) with spaces) {}".format(pid, " ".join(fields))


def valid_dom() -> str:
    return '<html><body><output id="benchmark-result" data-status="complete" data-items="{}" data-checksum="{}">done</output></body></html>\n'.format(run.ITEM_COUNT, run.CHECKSUM)


class CliAndTimeTests(unittest.TestCase):
    def test_defaults_and_multiple_candidates(self) -> None:
        args = run.parse_args(["--browser", "final=/build/chrome", "--browser", "chrome=/usr/bin/google-chrome"])
        self.assertEqual(args.repetitions, 5)
        self.assertEqual(args.profile_mode, "both")
        self.assertEqual(args.browser, [("final", "/build/chrome"), ("chrome", "/usr/bin/google-chrome")])

    def test_assignments_preserve_equals_in_paths(self) -> None:
        self.assertEqual(run.parse_assignment("final=/build=release/chrome"), ("final", "/build=release/chrome"))
        for value in ("final", "../final=/browser", "a/b=/browser", "=value", "final=", "final=x\x00y"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                run.parse_assignment(value)

    def test_no_arbitrary_flags_profiles_or_urls(self) -> None:
        for option in ("--no-sandbox", "--browser-arg=--disable-web-security", "--url=https://personal.invalid", "--user-data-dir=/normal"):
            with self.subTest(option=option), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                run.parse_args(["--browser", "final=/browser", option])

    def test_bounded_repetitions_and_times(self) -> None:
        self.assertEqual(run.positive_repetitions("7"), 7)
        for value in ("0", "-1", "1001", "1.5"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                run.positive_repetitions(value)
        self.assertEqual(run.bounded_seconds("0.125"), 0.125)
        for value in ("nan", "inf", "-inf", "0", "-1", "301", "bad"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                run.bounded_seconds(value)
        self.assertEqual(run.elapsed_seconds(10, 1_500_000_010), 1.5)
        with self.assertRaises(run.BenchmarkError):
            run.elapsed_seconds(11, 10)


class PathAndCommandTests(unittest.TestCase):
    def test_explicit_executable_path_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "browser with spaces"
            binary.write_text("test fixture; never executed", encoding="utf-8")
            binary.chmod(0o700)
            self.assertEqual(run.explicit_executable(str(binary)), binary)
            binary.chmod(0o600)
            with self.assertRaises(run.BenchmarkError):
                run.explicit_executable(str(binary))
        for value in ("google-chrome", "", "bad\x00path", "/definitely/missing/browser"):
            with self.subTest(value=value), self.assertRaises(run.BenchmarkError):
                run.explicit_executable(value)

    def test_root_refuses_symlinked_local_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory) / "project"
            project.mkdir()
            (project / ".local").symlink_to(directory, target_is_directory=True)
            with self.assertRaises(run.BenchmarkError):
                run.benchmark_root(project)

    def test_commands_confine_profiles_and_only_use_local_workload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = run.benchmark_root(Path(directory))
            profile = root / "private-profile"
            profile.mkdir()
            command = run.browser_command(Path("/binary with spaces/chrome"), profile, root)
            self.assertEqual(command[0], "/binary with spaces/chrome")
            self.assertEqual(command[-1], run.WORKLOAD.as_uri())
            self.assertIn("--user-data-dir={}".format(profile), command)
            self.assertIn("--dump-dom", command)
            self.assertIn("--headless=new", command)
            forbidden = ("--no-sandbox", "--disable-web-security", "--remote-debugging-port", "--disable-gpu-sandbox")
            self.assertFalse(any(value.startswith(forbidden) for value in command))
            self.assertEqual(run.browser_command(Path("/browser"), profile, root, version=True),
                             ["/browser", "--user-data-dir={}".format(profile), "--version"])
            outside = Path(directory) / "normal-profile"
            outside.mkdir()
            with self.assertRaises(run.BenchmarkError):
                run.browser_command(Path("/browser"), outside, root)
            linked = root / "linked"
            linked.symlink_to(outside, target_is_directory=True)
            with self.assertRaises(run.BenchmarkError):
                run.browser_command(Path("/browser"), linked, root)

    def test_output_is_confined_and_never_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = run.benchmark_root(Path(directory))
            run_dir = root / "run-one"
            run_dir.mkdir()
            self.assertEqual(run.confined_output(None, root, run_dir), run_dir / "results.json")
            with self.assertRaises(run.BenchmarkError):
                run.confined_output(str(Path(directory) / "outside.json"), root, run_dir)
            existing = root / "existing.json"
            existing.write_text("keep", encoding="utf-8")
            with self.assertRaises(run.BenchmarkError):
                run.confined_output(str(existing), root, run_dir)
            self.assertEqual(existing.read_text(encoding="utf-8"), "keep")

    def test_metadata_labels_do_not_silently_overwrite(self) -> None:
        for argv in (["--browser", "x=/browser", "--browser", "x=/other"],
                     ["--browser", "x=/browser", "--revision", "y=rev"],
                     ["--browser", "x=/browser", "--configuration", "x=release", "--configuration", "x=debug"]):
            with self.subTest(argv=argv), mock.patch.object(run, "explicit_executable", side_effect=lambda value: Path(value)), self.assertRaises(run.BenchmarkError):
                run.validate_options(run.parse_args(argv))


class ResultParsingTests(unittest.TestCase):
    def test_complete_dom_and_invalid_results(self) -> None:
        self.assertTrue(run.parse_dom_result(valid_dom())["valid"])
        for text in ("", valid_dom().replace('data-status="complete"', 'data-status="pending"'),
                     valid_dom().replace('data-checksum="{}"'.format(run.CHECKSUM), 'data-checksum="0"'),
                     valid_dom().replace("</html>", ""), valid_dom() + valid_dom(),
                     '<html><script>const fake = \'<output id="benchmark-result" data-status="complete">\';</script></html>'):
            with self.subTest(text=text):
                self.assertFalse(run.parse_dom_result(text)["valid"])

    def test_proc_stat_with_nested_parentheses_and_spaces(self) -> None:
        result = run.parse_proc_stat(proc_stat(300, ppid=42, start=987, cpu=(23, 9), rss=123))
        self.assertEqual(result["pid"], 300)
        self.assertEqual(result["ppid"], 42)
        self.assertEqual(result["cpu_ticks"], 32)
        self.assertEqual(result["rss_pages"], 123)
        self.assertEqual(result["start_ticks"], 987)
        for value in ("bad", "300 (name) S", proc_stat(300, cpu=(-10, 0))):
            with self.subTest(value=value), self.assertRaises(run.BenchmarkError):
                run.parse_proc_stat(value)

    def test_fixture_has_no_external_workload_and_is_stable(self) -> None:
        text = run.WORKLOAD.read_text(encoding="utf-8")
        self.assertNotIn("https://", text)
        self.assertNotIn("http://", text)
        self.assertIn("i < 2000", text)
        self.assertIn("(i * 17) % 997", text)
        self.assertIn("connect-src 'none'", text)
        self.assertEqual(run.file_metadata(run.WORKLOAD), run.file_metadata(run.WORKLOAD))

    def test_summary_excludes_warmup_and_failures(self) -> None:
        base = {"browser": "final", "profile_mode": "warm", "success": True,
                "launch_to_dom_complete_seconds": 2.0, "launch_to_exit_seconds": 2.1,
                "proc_metrics": {"peak_sampled_rss_sum_bytes": 100, "observed_cpu_seconds_lower_bound": 0.5}}
        trials = [dict(base, phase="warmup"), dict(base, phase="measurement"),
                  dict(base, phase="measurement", launch_to_dom_complete_seconds=4.0),
                  dict(base, phase="measurement", success=False)]
        summary = run.summarize(trials)["final:warm"]
        self.assertEqual(summary["attempted"], 3)
        self.assertEqual(summary["successful"], 2)
        self.assertEqual(summary["metrics"]["launch_to_dom_complete_seconds"]["median"], 3.0)


class ProcTreeTests(unittest.TestCase):
    def test_tree_samples_track_descendants_and_exclude_other_sessions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def write(pid: int, **kwargs) -> None:
                (root / str(pid)).mkdir(exist_ok=True)
                (root / str(pid) / "stat").write_text(proc_stat(pid, **kwargs), encoding="utf-8")
            write(300)
            write(301, ppid=300, session=999, cpu=(20, 5), rss=7)
            write(400, ppid=1, session=400, rss=1000)
            tree = run.ProcTree(300, root)
            tree.sample(0.1)
            self.assertEqual(tree.samples[0]["process_count"], 2)
            self.assertEqual(tree.samples[0]["rss_sum_bytes"], 27 * tree.page_bytes)
            write(301, ppid=1, session=999, cpu=(40, 5), rss=8)
            (root / "300" / "stat").unlink()
            tree.sample(0.2)
            self.assertEqual(tree.samples[1]["process_count"], 1)
            self.assertEqual(tree.metrics()["observed_cpu_seconds_lower_bound"], 60 / tree.clock_ticks)
            # PID reuse in an unrelated session must not retain membership.
            write(301, ppid=1, session=999, start=101)
            self.assertEqual(tree.members(), [])

    def test_signals_only_identified_members(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for pid, session in ((300, 300), (400, 400)):
                (root / str(pid)).mkdir()
                (root / str(pid) / "stat").write_text(proc_stat(pid, session=session), encoding="utf-8")
            tree = run.ProcTree(300, root)
            with mock.patch.object(run.os, "kill") as kill:
                tree.signal_members(signal.SIGTERM)
                kill.assert_called_once_with(300, signal.SIGTERM)

    def test_adopted_children_exclude_preexisting_host_children(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for pid, ppid in ((300, 42), (301, 42), (400, 42), (500, 42)):
                (root / str(pid)).mkdir()
                (root / str(pid) / "stat").write_text(proc_stat(pid, ppid=ppid, session=pid), encoding="utf-8")
                token = "other" if pid == 500 else "owned"
                (root / str(pid) / "environ").write_bytes((run.PROCESS_TOKEN_ENV + "=" + token).encode() + b"\0")
            tree = run.ProcTree(300, root, reaper_pid=42, preexisting={(400, 100)}, adopted_token="owned")
            self.assertEqual([item["pid"] for item in tree.members()], [300, 301])

    def test_missing_samples_do_not_claim_zero_memory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tree = run.ProcTree(300, Path(directory))
            tree.sample(0.01)
            self.assertIsNone(tree.metrics()["peak_sampled_rss_sum_bytes"])
            self.assertIsNone(tree.metrics()["observed_cpu_seconds_lower_bound"])


class ProcessRunnerTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "linux", "Linux session and /proc tests")
    def test_real_local_subprocess_output_and_validation_not_a_browser(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = run.run_process([sys.executable, "-c", "print({!r})".format(valid_dom())],
                                     Path(directory), "fixture", 2.0, 0.005)
            self.assertTrue(result["success"], result)
            self.assertEqual(result["returncode"], 0)
            self.assertIsNotNone(result["launch_to_dom_complete_seconds"])
            self.assertTrue(result["cleanup"]["complete"])
            self.assertTrue(result["dom_validation"]["valid"])
            self.assertGreater(result["proc_metrics"]["processes_observed"], 0)

    @unittest.skipUnless(sys.platform == "linux", "Linux session and /proc tests")
    def test_timeout_cleans_up_sleeping_child_tree(self) -> None:
        code = "import subprocess, sys, time; subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)']); time.sleep(60)"
        with tempfile.TemporaryDirectory() as directory:
            result = run.run_process([sys.executable, "-c", code], Path(directory), "timeout", 0.2, 0.005)
            self.assertFalse(result["success"])
            self.assertEqual(result["status"], "timeout")
            self.assertTrue(result["cleanup"]["complete"], result)
            self.assertIsNotNone(result["returncode"])
            self.assertGreaterEqual(result["proc_metrics"]["processes_observed"], 2)
            self.assertLess(result["launch_to_exit_seconds"], 1.5)

    @unittest.skipUnless(sys.platform == "linux", "Linux session and /proc tests")
    def test_failed_launch_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = run.run_process(["/definitely/missing/binary"], Path(directory), "missing", 0.2, 0.005)
            self.assertEqual(result["status"], "launch_error")
            self.assertFalse(result["success"])

    @unittest.skipUnless(sys.platform == "linux", "Linux subreaper tests")
    def test_detached_orphan_is_stopped_and_reaped(self) -> None:
        child_code = "import os, pathlib, time; os.setsid(); pathlib.Path('child.pid').write_text(str(os.getpid())); time.sleep(60)"
        parent_code = "import pathlib, subprocess, sys, time\nsubprocess.Popen([sys.executable, '-c', {!r}])\nwhile not pathlib.Path('child.pid').exists(): time.sleep(0.005)\nprint({!r})\n".format(child_code, valid_dom())
        with tempfile.TemporaryDirectory() as directory:
            result = run.run_process([sys.executable, "-c", parent_code], Path(directory), "orphan", 2.0, 0.005)
            self.assertTrue(result["success"], result)
            child_pid = int((Path(directory) / "child.pid").read_text())
            self.assertFalse(Path("/proc/{}/stat".format(child_pid)).exists(), "adopted child must be reaped, not merely left zombie")

    @unittest.skipUnless(sys.platform == "linux", "Linux runner tests")
    def test_invalid_dom_and_nonzero_exit_are_not_success(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = run.run_process([sys.executable, "-c", "print('<html></html>')"], Path(directory), "invalid", 2.0, 0.005)
            self.assertEqual(result["status"], "invalid_dom")
            result = run.run_process([sys.executable, "-c", "raise SystemExit(3)"], Path(directory), "nonzero", 2.0, 0.005)
            self.assertEqual(result["status"], "process_error")
            self.assertEqual(result["returncode"], 3)

    @unittest.skipUnless(sys.platform == "linux", "Linux runner tests")
    def test_output_limit_stops_subprocess(self) -> None:
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(run, "OUTPUT_LIMIT", 1024):
            result = run.run_process([sys.executable, "-c", "import sys, time; sys.stdout.write('x' * 4096); sys.stdout.flush(); time.sleep(60)"],
                                     Path(directory), "too-much", 2.0, 0.005)
            self.assertEqual(result["status"], "output_limit")
            self.assertFalse(result["success"])
            self.assertTrue(result["cleanup"]["complete"])


class OrchestrationTests(unittest.TestCase):
    def test_profiles_are_distinct_first_run_reused_warm_and_cleaned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = run.benchmark_root(Path(directory))
            binary = Path(directory) / "fake-browser"
            binary.write_text("not executed", encoding="utf-8")
            binary.chmod(0o700)
            commands: list[list[str]] = []
            def fake_run(command, run_dir, stem, timeout, interval, *, require_dom=True, cancellation=None):
                commands.append(command)
                checkpoint = json.loads((run_dir / "results.json").read_text())
                self.assertEqual(checkpoint["status"], "in_progress")
                self.assertTrue(checkpoint["in_progress"])
                self.assertEqual(checkpoint["active_step"]["command"], command)
                if len(commands) == 2:
                    self.assertIn("version_probe", checkpoint["browsers"]["final"])
                if len(commands) == 3:
                    self.assertEqual(len(checkpoint["results"]), 1)
                if len(commands) == 4:
                    self.assertEqual(len(checkpoint["results"]), 2)
                profile = Path(command[1].split("=", 1)[1])
                self.assertTrue(profile.is_dir())
                self.assertIn(root, profile.parents)
                return {"success": True, "status": "completed", "launch_to_dom_complete_seconds": 1.0,
                        "launch_to_exit_seconds": 1.1, "proc_metrics": {"peak_sampled_rss_sum_bytes": 100,
                        "observed_cpu_seconds_lower_bound": 0.2}, "command": command}
            with mock.patch.object(run, "benchmark_root", return_value=root), mock.patch.object(run.os, "geteuid", return_value=1000), mock.patch.object(run, "run_process", side_effect=fake_run), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run.main(["--browser", "final={}".format(binary), "--repetitions", "2"]), 0)
            self.assertEqual(len(commands), 6)  # version, warmup, two trials in each mode
            first_profiles = [command[1] for command in commands if "first-profile-" in command[1]]
            warm_profiles = [command[1] for command in commands if "warm-profile-" in command[1]]
            self.assertEqual(len(set(first_profiles)), 2)
            self.assertEqual(len(set(warm_profiles)), 1)
            for command in commands:
                self.assertFalse(Path(command[1].split("=", 1)[1]).exists())
            output = next(root.glob("run-*/results.json"))
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(len(payload["results"]), 5)
            self.assertEqual(payload["summary"]["final:first-run"]["successful"], 2)
            self.assertEqual(payload["browsers"]["final"]["entrypoint"]["size_bytes"], len("not executed"))
            self.assertEqual(payload["status"], "completed")
            self.assertFalse(payload["in_progress"])
            self.assertIsNone(payload["active_step"])

    def test_failed_warmup_is_recorded_and_not_reused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = run.benchmark_root(Path(directory))
            binary = Path(directory) / "fake-browser"
            binary.write_text("not executed", encoding="utf-8")
            binary.chmod(0o700)
            def fake_run(command, run_dir, stem, timeout, interval, *, require_dom=True, cancellation=None):
                return {"success": not require_dom, "status": "invalid_dom" if require_dom else "completed"}
            with mock.patch.object(run, "benchmark_root", return_value=root), mock.patch.object(run.os, "geteuid", return_value=1000), mock.patch.object(run, "run_process", side_effect=fake_run) as runner, contextlib.redirect_stdout(io.StringIO()):
                code = run.main(["--browser", "final={}".format(binary), "--profile-mode", "warm", "--repetitions", "2"])
            self.assertEqual(code, 1)
            self.assertEqual(runner.call_count, 2)  # version and failed warmup only
            payload = json.loads(next(root.glob("run-*/results.json")).read_text())
            self.assertEqual([item["status"] for item in payload["results"]],
                             ["invalid_dom", "skipped_failed_warmup", "skipped_failed_warmup"])
            self.assertEqual(payload["summary"]["final:warm"]["attempted"], 2)
            self.assertEqual(payload["summary"]["final:warm"]["successful"], 0)

    def test_incomplete_cleanup_aborts_and_retains_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = run.benchmark_root(Path(directory))
            binary = Path(directory) / "fake-browser"
            binary.write_text("not executed", encoding="utf-8")
            binary.chmod(0o700)
            failed = {"success": False, "status": "process_error",
                      "cleanup": {"complete": False, "remaining_processes": [{"pid": 123, "start_ticks": 456}]}}
            with mock.patch.object(run, "benchmark_root", return_value=root), mock.patch.object(run.os, "geteuid", return_value=1000), mock.patch.object(run, "run_process", return_value=failed) as runner, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(run.main(["--browser", "final={}".format(binary)]), 2)
            runner.assert_called_once()
            payload = json.loads(next(root.glob("run-*/results.json")).read_text())
            self.assertIn("cleanup incomplete", payload["abort_reason"])
            self.assertEqual(payload["results"], [])


class CheckpointTests(unittest.TestCase):
    def test_atomic_updates_preserve_old_snapshot_on_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "results.json"
            writer = run.CheckpointWriter(output)
            payload = {"results": [], "status": "in_progress", "in_progress": True}
            writer.write(payload)
            original = output.read_bytes()
            payload["example"] = "second checkpoint"
            with mock.patch.object(run.os, "replace", side_effect=OSError("injected replace failure")), self.assertRaises(OSError):
                writer.write(payload)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(list(Path(directory).glob(".checkpoint-*")), [])
            writer.write(payload)
            self.assertEqual(json.loads(output.read_text())["example"], "second checkpoint")
            self.assertEqual(json.loads(output.read_text())["checkpoint_sequence"], 2)
            payload["invalid_number"] = float("nan")
            original = output.read_bytes()
            with self.assertRaises(ValueError):
                writer.write(payload)
            self.assertEqual(output.read_bytes(), original)

    def test_never_overwrites_existing_or_externally_replaced_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "results.json"
            output.write_text("unrelated output")
            with self.assertRaises(FileExistsError):
                run.CheckpointWriter(output).write({"results": []})
            self.assertEqual(output.read_text(), "unrelated output")
            output.unlink()
            writer = run.CheckpointWriter(output)
            writer.write({"results": []})
            replacement = Path(directory) / "replacement"
            replacement.write_text("keep replacement")
            replacement.replace(output)
            with self.assertRaises(run.BenchmarkError):
                writer.write({"results": []})
            self.assertEqual(output.read_text(), "keep replacement")


class CancellationTests(unittest.TestCase):
    def test_handlers_only_set_a_flag_and_restore_previous_handlers(self) -> None:
        cancellation = run.Cancellation()
        previous = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
        with run.cancellation_signals(cancellation):
            signal.getsignal(signal.SIGTERM)(signal.SIGTERM, None)
            signal.getsignal(signal.SIGINT)(signal.SIGINT, None)
            self.assertEqual(cancellation.signal_number, signal.SIGTERM)
            with self.assertRaises(run.BenchmarkCancelled):
                cancellation.check()
        for sig, handler in previous.items():
            self.assertEqual(signal.getsignal(sig), handler)

    def test_pre_cancelled_process_never_launches(self) -> None:
        cancellation = run.Cancellation()
        cancellation.request(signal.SIGTERM)
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(run.subprocess, "Popen") as launch:
            result = run.run_process(["/must/not/launch"], Path(directory), "cancelled", 30, 0.02,
                                     cancellation=cancellation)
        launch.assert_not_called()
        self.assertEqual(result["status"], "cancelled")
        self.assertEqual(result["cancel_signal"], signal.SIGTERM)
        self.assertTrue(result["cleanup"]["complete"])


@unittest.skipUnless(sys.platform == "linux", "Linux signal/process-tree regression tests")
class IsolatedInterruptionTests(unittest.TestCase):
    def fixture_runner(self, root: Path, *, unrelated_child: bool = False) -> str:
        grandchild = "import os, pathlib, signal, time; signal.signal(signal.SIGTERM, signal.SIG_IGN); pathlib.Path('grandchild.pid').write_text(str(os.getpid())); time.sleep(60)"
        hanging = "import os, pathlib, signal, subprocess, sys, time\nsignal.signal(signal.SIGTERM, signal.SIG_IGN)\nsubprocess.Popen([sys.executable, '-c', {!r}])\nwhile not pathlib.Path('grandchild.pid').exists(): time.sleep(0.005)\npathlib.Path('hang.ready').write_text(str(os.getpid()))\ntime.sleep(60)\n".format(grandchild)
        # All launches are Python fixtures, never installed browsers or a shell.
        return """
import pathlib, subprocess, sys
from unittest import mock
from benchmarks import run
root = pathlib.Path({root!r})
calls = 0
unrelated = None
original_popen = subprocess.Popen
def fixture_command(executable, profile, storage, *, version=False):
    global calls
    calls += 1
    if version:
        code = "print('Fixture 1.0.0.0')"
    elif calls < 4:
        code = {printing!r}
    else:
        code = {hanging!r}
    return [sys.executable, '-c', code]
def launch(*args, **kwargs):
    global unrelated
    child = original_popen(*args, **kwargs)
    if {unrelated!r} and 'hang.ready' in args[0][-1]:
        # Spawn AFTER the runner's preexisting-child snapshot, without its token.
        unrelated = original_popen([sys.executable, '-c', 'import time; time.sleep(60)'])
        (root / 'unrelated.pid').write_text(str(unrelated.pid))
    return child
try:
    with mock.patch.object(run, 'benchmark_root', return_value=root), mock.patch.object(run.os, 'geteuid', return_value=1000), mock.patch.object(run, 'browser_command', side_effect=fixture_command), mock.patch.object(run.subprocess, 'Popen', side_effect=launch):
        code = run.main(['--browser', 'fixture=' + sys.executable, '--repetitions', '2', '--timeout', '30'])
finally:
    if unrelated is not None:
        (root / 'unrelated.survived').write_text(str(unrelated.poll() is None))
        unrelated.terminate()
        try: unrelated.wait(timeout=1)
        except subprocess.TimeoutExpired:
            unrelated.kill()
            unrelated.wait(timeout=1)
raise SystemExit(code)
""".format(root=str(root), printing="print({!r})".format(valid_dom()), hanging=hanging, unrelated=unrelated_child)

    def wait_ready(self, process: subprocess.Popen, root: Path) -> Path:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            ready = list(root.glob("run-*/hang.ready"))
            if ready:
                return ready[0].parent
            if process.poll() is not None:
                stdout, stderr = process.communicate(timeout=1)
                self.fail("fixture runner exited before hanging step: {} {}".format(stdout, stderr))
            time.sleep(0.01)
        self.fail("fixture runner failed to reach hanging step in bounded time")

    def test_sigterm_checkpoints_cancelled_trial_and_cleans_only_owned_tree(self) -> None:
        with tempfile.TemporaryDirectory() as directory, run.child_subreaper():
            root = run.benchmark_root(Path(directory))
            process = subprocess.Popen([sys.executable, "-c", self.fixture_runner(root, unrelated_child=True)],
                                       cwd=run.PROJECT_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, start_new_session=True)
            tree = run.ProcTree(process.pid)
            try:
                run_dir = self.wait_ready(process, root)
                tree.members()  # Track this test's descendants for emergency cleanup.
                owned_pids = [int((run_dir / name).read_text()) for name in ("hang.ready", "grandchild.pid")]
                started = time.monotonic()
                process.terminate()  # Only the Popen handle created by this test.
                stdout, stderr = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 143, (stdout, stderr))
                self.assertLess(time.monotonic() - started, 5)
                payload = json.loads((run_dir / "results.json").read_text())
                self.assertEqual(payload["status"], "interrupted")
                self.assertTrue(payload["interrupted"])
                self.assertFalse(payload["in_progress"])
                self.assertEqual(payload["cancellation"]["signal_name"], "SIGTERM")
                self.assertIn("version_probe", payload["browsers"]["fixture"])
                self.assertEqual([item["status"] for item in payload["results"]],
                                 ["completed", "completed", "cancelled"])
                cancelled = payload["results"][-1]
                self.assertTrue(cancelled["cleanup"]["complete"], cancelled)
                self.assertLess(cancelled["cleanup"]["elapsed_seconds"], 2)
                self.assertEqual((root / "unrelated.survived").read_text(), "True")
                for pid in owned_pids:
                    self.assertFalse(Path("/proc/{}/stat".format(pid)).exists())
                self.assertEqual(list(run_dir.glob("*profile-*")), [])
            finally:
                run.cleanup_tree(process, tree)
                process.communicate(timeout=1)

    def test_sigkill_retains_prior_evidence_and_explicit_in_progress_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as directory, run.child_subreaper():
            root = run.benchmark_root(Path(directory))
            process = subprocess.Popen([sys.executable, "-c", self.fixture_runner(root)],
                                       cwd=run.PROJECT_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, start_new_session=True)
            tree = run.ProcTree(process.pid)
            try:
                run_dir = self.wait_ready(process, root)
                tree.members()
                before = (run_dir / "results.json").read_bytes()
                process.kill()  # Deliberate SIGKILL of this isolated fixture, not a browser.
                process.communicate(timeout=2)
                self.assertEqual(process.returncode, -signal.SIGKILL)
                self.assertEqual((run_dir / "results.json").read_bytes(), before)
                payload = json.loads(before)
                self.assertEqual(payload["status"], "in_progress")
                self.assertTrue(payload["in_progress"])
                self.assertFalse(payload["interrupted"])
                self.assertEqual(payload["active_step"]["phase"], "measurement")
                self.assertEqual(payload["active_step"]["profile_mode"], "warm")
                self.assertEqual(len(payload["results"]), 2)
                self.assertIn("version_probe", payload["browsers"]["fixture"])
                self.assertEqual(payload["summary"]["fixture:first-run"]["successful"], 1)
            finally:
                cleanup = run.cleanup_tree(process, tree)
                process.communicate(timeout=1)
                self.assertTrue(cleanup["complete"], cleanup)


if __name__ == "__main__":
    unittest.main()
