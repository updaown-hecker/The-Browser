#!/usr/bin/env python3
"""Benchmark explicitly supplied Chromium-family binaries against a local file.

No installation, remote debugging, arbitrary evaluation, custom browser flags,
or normal profiles. Linux is required for scoped /proc process-tree sampling.
Use --help for the CLI. JSON retains raw trials, samples, commands and caveats.
"""

from __future__ import annotations

import argparse
from contextlib import ExitStack, contextmanager
import ctypes
import hashlib
import json
import math
import os
import platform
import re
import secrets
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path
from typing import Optional, Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WORKLOAD = Path(__file__).resolve().parent / "fixtures" / "local-workload.html"
ITEM_COUNT = 2000
CHECKSUM = sum((index * 17) % 997 for index in range(ITEM_COUNT))
OUTPUT_LIMIT = 8 * 1024 * 1024
PROCESS_TOKEN_ENV = "BROWSER_FINAL_BENCHMARK_PROCESS_TOKEN"
LABEL_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}\Z")
SAFE_FLAGS = (
    "--headless=new",
    "--no-first-run",
    "--no-default-browser-check",
    "--disable-background-networking",
    "--disable-component-update",
    "--disable-sync",
    "--metrics-recording-only",
    "--window-size=1280,720",
)
CAVEATS = [
    "Launch-to-DOM-complete is launch to observation of a complete, validated "
    "--dump-dom serialization, not an in-browser event timestamp. It includes "
    "launch, profile initialization, local-file loading, synchronous work and "
    "serialization; polling introduces uncertainty. Exit time also includes teardown.",
    "First-run uses a new empty temporary profile each trial. Warm uses one "
    "isolated profile per executable, initialized by one unmeasured workload run. "
    "Neither mode is cold OS-cache startup; caches are not flushed.",
    "Headless, fixed-window, extension-free local work is not desktop startup, "
    "interactive browsing, network performance, battery life or extension support.",
    "RSS is sampled process-tree RSS summed across processes. Shared pages are "
    "double-counted; this is NOT PSS, unique memory, an exact peak or idle memory.",
    "CPU is the sum of maximum observed per-process /proc utime+stime counters "
    "without child counters. It is a lower bound: short-lived or inaccessible "
    "processes and activity after their last sample can be missed. The sampler "
    "itself adds overhead; same-session and observed descendants are tracked.",
    "Executables may be installed launcher scripts. Their digest identifies only "
    "the supplied entrypoint, not every loaded library, resource or actual browser binary. "
    "Revision/configuration labels are user-supplied, not attested release status.",
    "Background-network reduction flags and the fixture CSP do not guarantee "
    "offline operation by every browser distribution. No personal sites are loaded.",
    "Comparisons are descriptive, not marketing claims. Use matching release "
    "configurations and the same machine, power state and background workload. "
    "Candidate order rotates per repetition, but earlier launches warm OS caches.",
    "Executable/workload metadata hashing and version probes run before trials "
    "and can warm OS caches. No OS-cache-cold results are claimed.",
    "The serial runner temporarily enables Linux child-subreaper behavior to "
    "adopt, stop and reap orphaned descendants. Only trusted local entrypoints "
    "that honor the supplied profile/headless flags are supported; launchers "
    "delegating via IPC to an existing external service cannot be isolated. "
    "An unobserved detached descendant that removes its inherited process token "
    "cannot be safely identified; unrelated children are never swept up by parent PID alone.",
    "JSON is atomically checkpointed before and after each subprocess step. "
    "SIGTERM/SIGINT request graceful cancellation; SIGKILL cannot be handled and "
    "may leave descendants/profiles. Its last checkpoint remains in_progress, "
    "not proof that the run is still alive or completed.",
]


class BenchmarkError(ValueError):
    pass


class BenchmarkCancelled(Exception):
    pass


class Cancellation:
    def __init__(self) -> None:
        self.signal_number: Optional[int] = None

    def request(self, signum: int, _frame=None) -> None:
        # A flag, not an asynchronous exception: never interrupt Popen or cleanup.
        if self.signal_number is None:
            self.signal_number = signum

    def check(self) -> None:
        if self.signal_number is not None:
            raise BenchmarkCancelled()


@contextmanager
def cancellation_signals(cancellation: Cancellation):
    previous = {}
    try:
        for signum in (signal.SIGTERM, signal.SIGINT):
            previous[signum] = signal.signal(signum, cancellation.request)
        yield
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)


def parse_assignment(value: str) -> tuple[str, str]:
    label, separator, content = value.partition("=")
    if not separator or not LABEL_PATTERN.fullmatch(label) or not content or "\x00" in content:
        raise argparse.ArgumentTypeError("expected LABEL=VALUE (safe, nonempty label and value)")
    return label, content


def positive_repetitions(value: str) -> int:
    try:
        number = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("repetitions must be an integer") from error
    if not 1 <= number <= 1000:
        raise argparse.ArgumentTypeError("repetitions must be between 1 and 1000")
    return number


def bounded_seconds(value: str, minimum: float = 0.1, maximum: float = 300.0) -> float:
    try:
        seconds = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("time must be a finite number of seconds") from error
    if not math.isfinite(seconds) or not minimum <= seconds <= maximum:
        raise argparse.ArgumentTypeError("time must be between {} and {} seconds".format(minimum, maximum))
    return seconds


def elapsed_seconds(start_ns: int, end_ns: int) -> float:
    if end_ns < start_ns:
        raise BenchmarkError("monotonic clock moved backwards")
    return (end_ns - start_ns) / 1_000_000_000


def explicit_executable(value: str) -> Path:
    # No PATH discovery: a bare command is not an explicitly supplied path.
    if "\x00" in value or not value or (not Path(value).is_absolute() and "/" not in value):
        raise BenchmarkError("supply an absolute or ./relative executable path, not a command name")
    candidate = Path(os.path.abspath(Path(value).expanduser()))
    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        raise BenchmarkError("executable is missing or not executable: {}".format(candidate))
    return candidate


def benchmark_root(project: Path = PROJECT_ROOT) -> Path:
    # Refuse symlinked local storage rather than risking another profile tree.
    root = project.resolve()
    for candidate in (root / ".local", root / ".local" / "benchmarks"):
        if candidate.is_symlink():
            raise BenchmarkError("benchmark storage must not be a symlink: {}".format(candidate))
        if candidate.exists() and not candidate.is_dir():
            raise BenchmarkError("benchmark storage is not a directory: {}".format(candidate))
        candidate.mkdir(exist_ok=True, mode=0o700)
    return root / ".local" / "benchmarks"


def confined_output(value: Optional[str], root: Path, run_dir: Path) -> Path:
    candidate = Path(value).expanduser() if value else run_dir / "results.json"
    if not candidate.is_absolute():
        candidate = PROJECT_ROOT / candidate
    # Reject existing destinations, symlinks and missing parent directories.
    if candidate.exists() or candidate.is_symlink():
        raise BenchmarkError("refusing to overwrite output: {}".format(candidate))
    parent = candidate.parent.resolve(strict=True)
    if parent != root and root not in parent.parents:
        raise BenchmarkError("output must stay inside {}".format(root))
    return parent / candidate.name


def isolated_profile(profile: Path, root: Path) -> Path:
    resolved = profile.resolve(strict=True)
    if not profile.is_dir() or profile.is_symlink() or root.resolve() not in resolved.parents:
        raise BenchmarkError("profile must be a harness-created directory inside benchmark storage")
    return resolved


def browser_command(executable: Path, profile: Path, root: Path, *, version: bool = False) -> list[str]:
    profile = isolated_profile(profile, root)
    command = [str(executable), "--user-data-dir={}".format(profile)]
    if version:
        return command + ["--version"]
    # Fixed allowlist only: the CLI cannot inject profile, URL or security flags.
    return command + list(SAFE_FLAGS) + ["--dump-dom", WORKLOAD.as_uri()]


class DOMResultParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.markers: list[dict[str, Optional[str]]] = []
        self.closed_html = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, Optional[str]]]) -> None:
        values = dict(attrs)
        if tag == "output" and values.get("id") == "benchmark-result":
            self.markers.append(values)

    def handle_endtag(self, tag: str) -> None:
        if tag == "html":
            self.closed_html = True


def parse_dom_result(text: str) -> dict:
    parser = DOMResultParser()
    parser.feed(text)
    parser.close()
    if not parser.closed_html or len(parser.markers) != 1:
        return {"valid": False, "reason": "missing complete HTML or unique workload marker"}
    marker = parser.markers[0]
    valid = (marker.get("data-status") == "complete"
             and marker.get("data-items") == str(ITEM_COUNT)
             and marker.get("data-checksum") == str(CHECKSUM))
    return {"valid": valid, "status": marker.get("data-status"),
            "items": marker.get("data-items"), "checksum": marker.get("data-checksum")}


def parse_proc_stat(text: str) -> dict:
    # comm may contain spaces and parentheses; split after the LAST closing ')'.
    opening = text.find("(")
    closing = text.rfind(")")
    if opening < 1 or closing <= opening:
        raise BenchmarkError("invalid /proc stat")
    try:
        pid = int(text[:opening].strip())
        fields = text[closing + 1:].split()
        result = {"pid": pid, "state": fields[0], "ppid": int(fields[1]),
                  "pgrp": int(fields[2]), "session": int(fields[3]),
                  "cpu_ticks": int(fields[11]) + int(fields[12]),
                  "start_ticks": int(fields[19]), "rss_pages": max(0, int(fields[21]))}
        if pid <= 0 or result["start_ticks"] < 0 or result["cpu_ticks"] < 0:
            raise ValueError("negative process counter")
        return result
    except (IndexError, ValueError) as error:
        raise BenchmarkError("invalid /proc stat fields") from error


@contextmanager
def child_subreaper():
    """Adopt orphaned grandchildren during one serial launch; restore the flag."""
    libc = ctypes.CDLL(None, use_errno=True)
    previous = ctypes.c_int()
    # Linux PR_GET_CHILD_SUBREAPER and PR_SET_CHILD_SUBREAPER (no dependencies).
    if libc.prctl(37, ctypes.byref(previous), 0, 0, 0) != 0:
        raise BenchmarkError("Linux child-subreaper setup unavailable")
    if libc.prctl(36, 1, 0, 0, 0) != 0:
        raise BenchmarkError("cannot enable Linux child-subreaper cleanup")
    try:
        yield
    finally:
        if libc.prctl(36, previous.value, 0, 0, 0) != 0:
            raise BenchmarkError("cannot restore Linux child-subreaper setting")


def existing_children() -> set[tuple[int, int]]:
    # Protect any child processes that predate this launch (e.g. in a test host).
    children = Path("/proc/self/task/{}/children".format(os.getpid())).read_text().split()
    identities: set[tuple[int, int]] = set()
    for pid in children:
        try:
            stat = parse_proc_stat(Path("/proc/{}/stat".format(pid)).read_text(errors="replace"))
            identities.add((stat["pid"], stat["start_ticks"]))
        except FileNotFoundError:
            pass
    return identities


class ProcTree:
    """Track only the fresh subprocess session and identified descendants."""

    def __init__(self, leader: int, proc_root: Path = Path("/proc"), *,
                 reaper_pid: Optional[int] = None, preexisting: Optional[set[tuple[int, int]]] = None,
                 adopted_token: Optional[str] = None) -> None:
        self.leader = leader
        self.proc_root = proc_root
        self.identities: set[tuple[int, int]] = set()
        self.cpu_max: dict[tuple[int, int], int] = {}
        self.samples: list[dict] = []
        self.scan_errors = 0
        self.reaper_pid = reaper_pid
        self.preexisting = preexisting or set()
        self.adopted_token = adopted_token
        self.signal_errors = 0
        self.clock_ticks = os.sysconf("SC_CLK_TCK")
        self.page_bytes = os.sysconf("SC_PAGE_SIZE")

    def members(self) -> list[dict]:
        processes: dict[int, dict] = {}
        try:
            entries = list(self.proc_root.iterdir())
        except OSError:
            self.scan_errors += 1
            return []
        for entry in entries:
            if not entry.name.isdecimal():
                continue
            try:
                stat = parse_proc_stat((entry / "stat").read_text(encoding="utf-8", errors="replace"))
                processes[stat["pid"]] = stat
            except FileNotFoundError:
                continue  # Normal process-exit race.
            except (OSError, BenchmarkError):
                self.scan_errors += 1
        selected = {pid for pid, stat in processes.items()
                    if stat["session"] == self.leader
                    or (pid, stat["start_ticks"]) in self.identities
                    or (self.reaper_pid is not None and stat["ppid"] == self.reaper_pid
                        and (pid, stat["start_ticks"]) not in self.preexisting
                        and self.owns_adopted(pid))}
        # Include children that start another session, when observed before reparenting.
        while True:
            children = {pid for pid, stat in processes.items() if stat["ppid"] in selected}
            if children.issubset(selected):
                break
            selected.update(children)
        members = [processes[pid] for pid in sorted(selected)]
        self.identities.update((stat["pid"], stat["start_ticks"]) for stat in members)
        return members

    def owns_adopted(self, pid: int) -> bool:
        # An unrelated child launched concurrently must not be mistaken for an orphan.
        if self.adopted_token is None:
            return False
        marker = "{}={}".format(PROCESS_TOKEN_ENV, self.adopted_token).encode()
        try:
            return marker in (self.proc_root / str(pid) / "environ").read_bytes().split(b"\0")
        except OSError:
            return False

    def sample(self, elapsed: float) -> None:
        members = self.members()
        for stat in members:
            identity = (stat["pid"], stat["start_ticks"])
            self.cpu_max[identity] = max(self.cpu_max.get(identity, 0), stat["cpu_ticks"])
        self.samples.append({"elapsed_seconds": elapsed, "process_count": len(members),
                             "rss_sum_bytes": sum(stat["rss_pages"] for stat in members) * self.page_bytes,
                             "observed_cpu_seconds": sum(self.cpu_max.values()) / self.clock_ticks})

    def signal_members(self, sig: int) -> None:
        for stat in reversed(self.members()):
            if stat["state"] == "Z":
                continue
            try:
                # Re-read the start counter just before signaling, avoiding PID reuse.
                current = parse_proc_stat((self.proc_root / str(stat["pid"]) / "stat").read_text(errors="replace"))
                if current["start_ticks"] == stat["start_ticks"]:
                    os.kill(stat["pid"], sig)
            except ProcessLookupError:
                pass
            except FileNotFoundError:
                pass
            except (OSError, BenchmarkError):
                self.signal_errors += 1

    def metrics(self) -> dict:
        return {"source": "linux /proc sampled process tree", "sample_count": len(self.samples),
                "availability": "sampled_lower_bound" if self.cpu_max else "no_process_samples",
                "processes_observed": len(self.identities), "scan_errors": self.scan_errors,
                "peak_sampled_rss_sum_bytes": max((item["rss_sum_bytes"] for item in self.samples), default=None)
                if self.cpu_max else None,
                "observed_cpu_seconds_lower_bound": sum(self.cpu_max.values()) / self.clock_ticks
                if self.cpu_max else None,
                "clock_ticks_per_second": self.clock_ticks, "page_bytes": self.page_bytes,
                "samples": self.samples}


def cleanup_tree(process: subprocess.Popen, tree: ProcTree) -> dict:
    # Bound both phases. A fresh session prevents targeting a personal browser.
    started = time.monotonic()
    for sig, grace in ((signal.SIGTERM, 0.25), (signal.SIGKILL, 0.5)):
        deadline = time.monotonic() + grace
        while True:
            tree.signal_members(sig)
            # The Popen child handle is still safe if /proc cannot enumerate it.
            if process.poll() is None:
                try:
                    process.send_signal(sig)
                except ProcessLookupError:
                    pass
                except OSError:
                    tree.signal_errors += 1
            process.poll()
            reap_adopted(tree, process.pid)
            alive = [item for item in tree.members() if item["state"] != "Z"]
            if (not alive and process.poll() is not None) or time.monotonic() >= deadline:
                break
            time.sleep(0.01)
    try:
        process.wait(timeout=0.1)
    except subprocess.TimeoutExpired:
        pass
    reap_adopted(tree, process.pid)
    remaining = [{"pid": stat["pid"], "start_ticks": stat["start_ticks"]}
                 for stat in tree.members() if stat["state"] != "Z"]
    # A previously identified but now unreadable process is unknown, not proven gone.
    for pid, start_ticks in tree.identities:
        try:
            stat = parse_proc_stat((tree.proc_root / str(pid) / "stat").read_text(errors="replace"))
        except (FileNotFoundError, ProcessLookupError):
            continue
        except (OSError, BenchmarkError):
            if not any(item["pid"] == pid for item in remaining):
                remaining.append({"pid": pid, "start_ticks": start_ticks, "state": "unreadable"})
            continue
        if stat["start_ticks"] == start_ticks and stat["state"] != "Z" and not any(item["pid"] == pid for item in remaining):
            remaining.append({"pid": pid, "start_ticks": start_ticks})
    if process.poll() is None and not any(item["pid"] == process.pid for item in remaining):
        remaining.append({"pid": process.pid, "start_ticks": None})
    return {"complete": not remaining, "remaining_processes": remaining,
            "signal_errors": tree.signal_errors, "elapsed_seconds": time.monotonic() - started,
            "scope": "new session, identified descendants and token-verified adopted orphans via Linux child-subreaper"}


def reap_adopted(tree: ProcTree, leader: int) -> None:
    for stat in tree.members():
        if stat["pid"] == leader or stat["ppid"] != os.getpid():
            continue
        try:
            os.waitpid(stat["pid"], os.WNOHANG)
        except ChildProcessError:
            pass


def complete_dump_present(path: Path) -> bool:
    with path.open("rb") as stream:
        stream.seek(max(0, path.stat().st_size - 256))
        return stream.read().rstrip().lower().endswith(b"</html>")


def read_limited(path: Path, limit: int) -> str:
    with path.open("rb") as stream:
        return stream.read(limit).decode("utf-8", errors="replace")


def run_process(command: Sequence[str], run_dir: Path, stem: str, timeout: float,
                sample_interval: float, *, require_dom: bool = True,
                cancellation: Optional[Cancellation] = None) -> dict:
    cancellation = cancellation or Cancellation()
    if cancellation.signal_number is not None:
        return {"command": list(command), "status": "cancelled", "success": False,
                "cancel_signal": cancellation.signal_number,
                "cleanup": {"complete": True, "remaining_processes": []}}
    stdout_path = run_dir / (stem + ".stdout.txt")
    stderr_path = run_dir / (stem + ".stderr.txt")
    start_ns = time.perf_counter_ns()
    observed_ns: Optional[int] = None
    detection_interval: Optional[float] = None
    previous_check_ns = start_ns
    tree: Optional[ProcTree] = None
    process: Optional[subprocess.Popen] = None
    status = "completed"
    cleanup = {"complete": True, "remaining_processes": []}
    try:
        with child_subreaper(), stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            preexisting = existing_children()
            process_token = secrets.token_hex(16)
            environment = dict(os.environ, **{PROCESS_TOKEN_ENV: process_token})
            start_ns = time.perf_counter_ns()
            previous_check_ns = start_ns
            process = subprocess.Popen(list(command), cwd=run_dir, stdin=subprocess.DEVNULL,
                                       stdout=stdout, stderr=stderr, start_new_session=True,
                                       close_fds=True, env=environment)
            tree = ProcTree(process.pid, reaper_pid=os.getpid(), preexisting=preexisting,
                            adopted_token=process_token)
            try:
                while True:
                    now_ns = time.perf_counter_ns()
                    elapsed = elapsed_seconds(start_ns, now_ns)
                    tree.sample(elapsed)
                    if cancellation.signal_number is not None:
                        status = "cancelled"
                        break
                    if stdout_path.stat().st_size > OUTPUT_LIMIT or stderr_path.stat().st_size > OUTPUT_LIMIT:
                        status = "output_limit"
                        break
                    if require_dom and observed_ns is None:
                        if complete_dump_present(stdout_path):
                            observed_ns = time.perf_counter_ns()
                            detection_interval = elapsed_seconds(previous_check_ns, observed_ns)
                        previous_check_ns = time.perf_counter_ns()
                    if process.poll() is not None:
                        break
                    if elapsed >= timeout:
                        status = "timeout"
                        break
                    time.sleep(min(sample_interval, max(0, timeout - elapsed)))
                end_ns = time.perf_counter_ns()
            except KeyboardInterrupt:
                cancellation.request(signal.SIGINT)
                status = "cancelled"
                end_ns = time.perf_counter_ns()
            finally:
                cleanup = cleanup_tree(process, tree)
    except OSError as error:
        # Failed launches are preserved in the raw result instead of silently dropped.
        return {"command": list(command), "status": "launch_error", "error": str(error),
                "success": False, "cleanup": cleanup,
                "launch_to_exit_seconds": elapsed_seconds(start_ns, time.perf_counter_ns())}
    stdout_text = read_limited(stdout_path, OUTPUT_LIMIT)
    dom = parse_dom_result(stdout_text) if require_dom else None
    success = (status == "completed" and process is not None and process.returncode == 0
               and cleanup["complete"] and (not require_dom or dom["valid"]))
    if status == "completed" and not success:
        status = "invalid_dom" if process is not None and process.returncode == 0 and require_dom and not dom["valid"] else "process_error"
    return {"command": list(command), "status": status, "success": success,
            "cancel_signal": cancellation.signal_number if status == "cancelled" else None,
            "returncode": process.returncode if process else None,
            "launch_to_exit_seconds": elapsed_seconds(start_ns, end_ns),
            "launch_to_dom_complete_seconds": elapsed_seconds(start_ns, observed_ns)
            if success and observed_ns is not None else None,
            "completion_detection_interval_seconds": detection_interval,
            "dom_validation": dom, "cleanup": cleanup,
            "stdout_path": str(stdout_path), "stderr_path": str(stderr_path),
            "stdout_bytes": stdout_path.stat().st_size, "stderr_bytes": stderr_path.stat().st_size,
            "proc_metrics": tree.metrics() if tree else None}


def file_metadata(path: Path, *, include_text: bool = False) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    stat = path.stat()
    result = {"path": str(path), "resolved_path": str(path.resolve()), "sha256": digest.hexdigest(),
              "size_bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns}
    if include_text:
        if stat.st_size > 1024 * 1024:
            raise BenchmarkError("build config exceeds 1 MiB: {}".format(path))
        result["text"] = path.read_text(encoding="utf-8")
    return result


def require_cleanup(result: dict) -> None:
    if not result.get("cleanup", {}).get("complete", True):
        raise BenchmarkError("subprocess cleanup incomplete; stopping instead of contaminating later trials: {}".format(
            result["cleanup"]["remaining_processes"]))


def host_metadata() -> dict:
    result = {"platform": platform.platform(), "machine": platform.machine(),
              "cpu_count": os.cpu_count(), "python": platform.python_version(),
              "cpu_affinity": sorted(os.sched_getaffinity(0)), "load_average": list(os.getloadavg())}
    try:
        cpuinfo = Path("/proc/cpuinfo").read_text(errors="replace")
        result["cpu_models"] = sorted({line.partition(":")[2].strip() for line in cpuinfo.splitlines()
                                       if line.partition(":")[0].strip() in ("model name", "Hardware")})
        meminfo = Path("/proc/meminfo").read_text()
        match = re.search(r"^MemTotal:\s+(\d+) kB$", meminfo, flags=re.MULTILINE)
        result["memory_total_bytes"] = int(match.group(1)) * 1024 if match else None
    except OSError:
        result["optional_proc_metadata_unavailable"] = True
    return result


def summarize(results: list[dict]) -> dict:
    summaries: dict = {}
    for result in results:
        if result["phase"] != "measurement":
            continue
        key = "{}:{}".format(result["browser"], result["profile_mode"])
        summaries.setdefault(key, {"attempted": 0, "successful": 0, "values": {}})
        summary = summaries[key]
        summary["attempted"] += 1
        if not result["success"]:
            continue
        summary["successful"] += 1
        metrics = {"launch_to_dom_complete_seconds": result["launch_to_dom_complete_seconds"],
                   "launch_to_exit_seconds": result["launch_to_exit_seconds"],
                   "peak_sampled_rss_sum_bytes": result["proc_metrics"]["peak_sampled_rss_sum_bytes"],
                   "observed_cpu_seconds_lower_bound": result["proc_metrics"]["observed_cpu_seconds_lower_bound"]}
        for name, value in metrics.items():
            if value is not None:
                summary["values"].setdefault(name, []).append(value)
    for summary in summaries.values():
        summary["metrics"] = {name: {"n": len(values), "median": statistics.median(values),
                                      "min": min(values), "max": max(values),
                                      "mean": statistics.mean(values),
                                      "sample_stdev": statistics.stdev(values) if len(values) > 1 else None}
                              for name, values in summary.pop("values").items()}
    return summaries


class CheckpointWriter:
    """Publish complete JSON snapshots; never replace somebody else's output."""

    def __init__(self, output: Path) -> None:
        self.output = output
        self.identity: Optional[tuple[int, int]] = None
        self.sequence = 0

    def write(self, payload: dict) -> None:
        payload["checkpoint_utc"] = datetime.now(timezone.utc).isoformat()
        payload["checkpoint_sequence"] = self.sequence + 1
        payload["summary"] = summarize(payload["results"])
        descriptor, name = tempfile.mkstemp(prefix=".checkpoint-", suffix=".json", dir=self.output.parent)
        temporary = Path(name)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(payload, stream, indent=2, allow_nan=False)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            if self.identity is None:
                # An exclusive atomic first publication, not an empty reservation file.
                os.link(temporary, self.output)
            else:
                stat = self.output.lstat()
                if (stat.st_dev, stat.st_ino) != self.identity:
                    raise BenchmarkError("checkpoint destination was replaced externally; refusing overwrite")
                os.replace(temporary, self.output)
            stat = self.output.lstat()
            self.identity = (stat.st_dev, stat.st_ino)
            self.sequence += 1
            directory = os.open(self.output.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        finally:
            temporary.unlink(missing_ok=True)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, epilog="Run as an ordinary user with sandbox support; never add --no-sandbox. All profiles are temporary and JSON/logs remain in .local/benchmarks.")
    parser.add_argument("--browser", action="append", required=True, type=parse_assignment,
                        metavar="LABEL=/PATH", help="explicit built/installed executable; repeat for comparison")
    parser.add_argument("--repetitions", default=5, type=positive_repetitions)
    parser.add_argument("--profile-mode", choices=("first-run", "warm", "both"), default="both")
    parser.add_argument("--timeout", default=30.0, type=bounded_seconds, metavar="SECONDS")
    parser.add_argument("--sample-interval", default=0.02,
                        type=lambda value: bounded_seconds(value, 0.005, 1.0), metavar="SECONDS")
    parser.add_argument("--configuration", action="append", type=parse_assignment, default=[], metavar="LABEL=release",
                        help="user-supplied build configuration label")
    parser.add_argument("--revision", action="append", type=parse_assignment, default=[], metavar="LABEL=REVISION")
    parser.add_argument("--build-config", action="append", type=parse_assignment, default=[], metavar="LABEL=/PATH/args.gn",
                        help="explicit build configuration file, recorded verbatim and hashed")
    parser.add_argument("--output", help="new JSON path within .local/benchmarks (default: run directory/results.json)")
    return parser.parse_args(argv)


def validate_options(args: argparse.Namespace) -> tuple[list[tuple[str, Path]], dict]:
    browsers: list[tuple[str, Path]] = []
    labels: set[str] = set()
    for label, value in args.browser:
        if label in labels:
            raise BenchmarkError("duplicate browser label: {}".format(label))
        labels.add(label)
        browsers.append((label, explicit_executable(value)))
    metadata = {label: {} for label in labels}
    for name in ("configuration", "revision", "build_config"):
        seen: set[str] = set()
        for label, value in getattr(args, name):
            if label not in labels or label in seen:
                raise BenchmarkError("unknown or duplicate {} label: {}".format(name, label))
            seen.add(label)
            if name == "build_config":
                path = Path(value).expanduser().resolve(strict=True)
                if not path.is_file() or path.stat().st_size > 1024 * 1024:
                    raise BenchmarkError("build config must be a file of at most 1 MiB")
                metadata[label][name] = file_metadata(path, include_text=True)
            else:
                metadata[label][name] = value
    return browsers, metadata


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        if sys.platform != "linux" or not Path("/proc/self/stat").is_file():
            raise BenchmarkError("this harness requires Linux with readable /proc")
        if os.geteuid() == 0:
            raise BenchmarkError("run as a non-root user; sandbox-disabling workarounds are not supported")
        browsers, labels = validate_options(args)
        root = benchmark_root()
        run_dir = Path(tempfile.mkdtemp(prefix="run-", dir=root))
        output = confined_output(args.output, root, run_dir)
        results: list[dict] = []
        payload = {"schema_version": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
                   "host": host_metadata(),
                   "settings": {"repetitions": args.repetitions, "profile_mode": args.profile_mode,
                                "timeout_seconds": args.timeout, "sample_interval_seconds": args.sample_interval,
                                "candidate_order": "rotate by repetition; first-run then warm within each candidate",
                                "browser_labels": [label for label, _ in browsers],
                                "browser_flags": list(SAFE_FLAGS)},
                   "workload": file_metadata(WORKLOAD), "browsers": {}, "results": results,
                   "caveats": CAVEATS, "run_directory": str(run_dir), "interrupted": False,
                   "status": "in_progress", "in_progress": True, "active_step": None}
        cancellation = Cancellation()
        writer = CheckpointWriter(output)

        def begin_step(command: list[str], label: str, phase: str, mode: Optional[str], repetition: int) -> None:
            cancellation.check()
            payload["active_step"] = {"browser": label, "phase": phase, "profile_mode": mode,
                                      "repetition": repetition, "command": command,
                                      "started_utc": datetime.now(timezone.utc).isoformat()}
            writer.write(payload)

        def finish_step(result: dict) -> None:
            payload["active_step"] = None
            writer.write(payload)
            require_cleanup(result)
            cancellation.check()

        def finalize() -> None:
            payload["in_progress"] = False
            payload["finished_utc"] = datetime.now(timezone.utc).isoformat()
            if cancellation.signal_number is not None:
                payload["interrupted"] = True
                payload["status"] = "interrupted"
                payload["cancellation"] = {"signal": cancellation.signal_number,
                                            "signal_name": signal.Signals(cancellation.signal_number).name}
            elif "abort_reason" in payload:
                payload["status"] = "aborted"
            else:
                payload["status"] = "completed" if results and all(item["success"] for item in results) else "failed"
            writer.write(payload)

        # Only the harness creates profiles, under its unique private run directory.
        # Keep profile cleanup in ExitStack so interruptions cannot retain profiles.
        with cancellation_signals(cancellation):
            writer.write(payload)
            try:
                with ExitStack() as profiles:
                    warm_profiles: dict[str, Path] = {}
                    warm_ok: dict[str, bool] = {}
                    for label, executable in browsers:
                        cancellation.check()
                        metadata = {"entrypoint": file_metadata(executable), "provided_build_metadata": labels[label]}
                        payload["browsers"][label] = metadata
                        with tempfile.TemporaryDirectory(prefix="version-profile-", dir=run_dir) as profile:
                            command = browser_command(executable, Path(profile), root, version=True)
                            begin_step(command, label, "version_probe", None, 0)
                            version = run_process(command, run_dir, "{}-version".format(label),
                                                  min(5.0, args.timeout), args.sample_interval, require_dom=False,
                                                  cancellation=cancellation)
                            metadata["version_probe"] = version
                            if "stdout_path" in version:
                                metadata["version_stdout"] = read_limited(Path(version["stdout_path"]), 16384).strip()
                                metadata["version_stderr"] = read_limited(Path(version["stderr_path"]), 16384).strip()
                            finish_step(version)
                        if args.profile_mode in ("warm", "both"):
                            profile = Path(profiles.enter_context(tempfile.TemporaryDirectory(prefix="{}-warm-profile-".format(label), dir=run_dir)))
                            warm_profiles[label] = profile
                            command = browser_command(executable, profile, root)
                            begin_step(command, label, "warmup", "warm", 0)
                            warmup = run_process(command, run_dir, "{}-warmup".format(label),
                                                 args.timeout, args.sample_interval, cancellation=cancellation)
                            warmup.update(browser=label, profile_mode="warm", phase="warmup", repetition=0)
                            results.append(warmup)
                            finish_step(warmup)
                            warm_ok[label] = warmup["success"]
                    modes = ("first-run", "warm") if args.profile_mode == "both" else (args.profile_mode,)
                    for repetition in range(args.repetitions):
                        offset = repetition % len(browsers)
                        for label, executable in browsers[offset:] + browsers[:offset]:
                            for mode in modes:
                                cancellation.check()
                                stem = "{}-{}-{:04d}".format(label, mode, repetition + 1)
                                with ExitStack() as trial_profile:
                                    if mode == "warm" and not warm_ok[label]:
                                        result = {"success": False, "status": "skipped_failed_warmup"}
                                    else:
                                        profile = warm_profiles[label] if mode == "warm" else Path(trial_profile.enter_context(
                                            tempfile.TemporaryDirectory(prefix="{}-first-profile-".format(label), dir=run_dir)))
                                        command = browser_command(executable, profile, root)
                                        begin_step(command, label, "measurement", mode, repetition + 1)
                                        result = run_process(command, run_dir, stem, args.timeout,
                                                             args.sample_interval, cancellation=cancellation)
                                    result.update(browser=label, profile_mode=mode, phase="measurement", repetition=repetition + 1)
                                    results.append(result)
                                    # Save the result before potentially slow profile deletion.
                                    finish_step(result)
                                print("{} {} {}/{}: {}".format(label, mode, repetition + 1, args.repetitions, result["status"]))
                                # Do not continue reusing a profile after a failed/dirty launch.
                                if mode == "warm" and not result["success"]:
                                    warm_ok[label] = False
            except BenchmarkCancelled:
                pass
            except KeyboardInterrupt:
                cancellation.request(signal.SIGINT)
            except (BenchmarkError, OSError, UnicodeError) as error:
                payload["abort_reason"] = str(error)
                raise
            finally:
                final_signal = cancellation.signal_number
                finalize()
                # Cover cancellation received while the terminal snapshot was written.
                if cancellation.signal_number != final_signal:
                    finalize()
        print("Raw JSON: {}".format(output))
        print("Headless local-file results only; read JSON caveats before comparing.")
        if payload["interrupted"]:
            return 128 + cancellation.signal_number
        return 0 if results and all(item["success"] for item in results) else 1
    except (BenchmarkError, OSError, UnicodeError) as error:
        print("Benchmark error: {}".format(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
