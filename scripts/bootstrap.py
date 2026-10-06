#!/usr/bin/env python3
"""Prepare and validate an external Chromium checkout.

This project intentionally keeps the very large Chromium checkout outside of
the repository.  The default layout is::

    <repository>/.local/chromium/src   # ignored Chromium source
    <repository>/.local/chromium/src/out/<platform>-<config> # GN output

No command in this module downloads source or installs packages implicitly.
Source bootstrap and source updates are opt-in, confirmation-gated operations.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence


SUPPORTED_PLATFORMS = ("linux", "windows")
SUPPORTED_CONFIGS = ("debug", "release")


class ToolError(RuntimeError):
    """A user-actionable tooling error."""


@dataclass(frozen=True)
class ToolPaths:
    """Resolved project, Chromium, and generated-output paths."""

    repo_root: Path
    source_root: Path
    build_root: Path


@dataclass(frozen=True)
class CheckResult:
    name: str
    ok: bool
    required: bool
    detail: str


@dataclass(frozen=True)
class UpstreamPin:
    chromium_version: str
    chromium_revision: str
    chromium_url: str
    depot_tools_revision: str
    depot_tools_url: str


def project_root() -> Path:
    """Return this checkout's root, independent of the current directory."""

    return Path(__file__).resolve().parents[1]


def load_upstream(repo_root: Path) -> UpstreamPin:
    """Read the reviewed release/tool pins; never substitute upstream main."""

    path = repo_root / "config" / "upstream.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("expected an object")
        pin = UpstreamPin(**{key: data[key] for key in UpstreamPin.__dataclass_fields__})
        for revision in (pin.chromium_revision, pin.depot_tools_revision):
            if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
                raise ValueError("revisions must be full lowercase Git hashes")
        if not isinstance(pin.chromium_version, str) or not re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}", pin.chromium_version):
            raise ValueError("chromium_version must be a four-part release tag")
        for url in (pin.chromium_url, pin.depot_tools_url):
            if not isinstance(url, str) or not re.fullmatch(r"https://[A-Za-z0-9./_-]+", url):
                raise ValueError("upstream URLs must be plain HTTPS Git URLs")
        return pin
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise ToolError("Cannot read valid upstream pins from {}: {}".format(path, exc)) from exc


def depot_tools_root(repo_root: Optional[Path] = None) -> Path:
    return (repo_root or project_root()) / ".local" / "tools" / "depot_tools"


def subprocess_environment(
    repo_root: Optional[Path] = None, platform_name: Optional[str] = None
) -> Dict[str, str]:
    """Configure only child processes, not the user's shell or global PATH."""

    environment = os.environ.copy()
    tools = depot_tools_root(repo_root)
    if tools.is_dir():
        environment["PATH"] = str(tools) + os.pathsep + environment.get("PATH", os.defpath)
    environment["DEPOT_TOOLS_UPDATE"] = "0"
    environment["DEPOT_TOOLS_METRICS"] = "0"
    environment["DEPOT_TOOLS_COLLECT_METRICS"] = "0"
    if platform_name == "windows" or (platform_name is None and os.name == "nt"):
        environment["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
    return environment


def detect_platform(value: str = "auto") -> str:
    """Resolve a CLI platform name to one of the supported Chromium targets."""

    if value != "auto":
        normalized = value.lower()
        if normalized not in SUPPORTED_PLATFORMS:
            raise ToolError(
                "Unsupported platform {!r}; choose linux, windows, or auto.".format(
                    value
                )
            )
        return normalized

    if sys.platform.startswith("linux"):
        return "linux"
    if sys.platform.startswith("win"):
        return "windows"
    raise ToolError(
        "This host ({}) is not supported by the bootstrap tooling. "
        "Pass --platform linux or --platform windows only when the required "
        "cross-build environment is already configured.".format(sys.platform)
    )


def _absolute_path(value: str, base: Path) -> Path:
    if "\x00" in value:
        raise ToolError("Paths must not contain NUL characters.")
    candidate = Path(value).expanduser()
    if not candidate.is_absolute():
        candidate = base / candidate
    return candidate.resolve(strict=False)


def _within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _default_source_root(repo_root: Path) -> Path:
    """Find the conventional external checkout without creating anything."""

    container = repo_root / ".local" / "chromium"
    src = container / "src"
    # A normal `fetch chromium` checkout has a wrapper directory and src.
    # Also accept a source-only checkout at <parent>/chromium for convenience.
    if src.is_dir() or (src / "DEPS").is_file():
        return src.resolve(strict=False)
    if (container / "DEPS").is_file() or (container / "build").is_dir():
        return container.resolve(strict=False)
    # Keep the expected fetch destination deterministic before it exists.
    return src.resolve(strict=False)


def resolve_paths(
    repo_root: Path,
    source_root: Optional[str] = None,
    build_root: Optional[str] = None,
    platform_name: str = "auto",
    config: str = "release",
) -> ToolPaths:
    """Resolve and validate paths without requiring Chromium to exist yet.

    ``source_root`` and ``build_root`` may be absolute or project-relative.
    Environment variables are useful for CI and are treated like explicit
    overrides: ``CHROMIUM_SOURCE_ROOT`` and ``CHROMIUM_BUILD_ROOT``.
    """

    repo = repo_root.resolve(strict=False)
    if not repo.is_dir():
        raise ToolError("Project root does not exist or is not a directory: {}".format(repo))

    platform_value = detect_platform(platform_name)
    if config not in SUPPORTED_CONFIGS:
        raise ToolError(
            "Unsupported configuration {!r}; choose debug or release.".format(config)
        )

    source_value = source_root or os.environ.get("CHROMIUM_SOURCE_ROOT")
    if source_root is not None and not source_root.strip():
        raise ToolError("--source-root must not be empty.")
    if build_root is not None and not build_root.strip():
        raise ToolError("--build-root must not be empty.")
    if source_value:
        source = _absolute_path(source_value, repo)
    else:
        source = _default_source_root(repo)

    build_value = build_root or os.environ.get("CHROMIUM_BUILD_ROOT")
    if build_value:
        build = _absolute_path(build_value, repo)
    else:
        build = (source / "out" / (platform_value + "-" + config)).resolve(strict=False)

    if _within(repo, source):
        raise ToolError(
            "Chromium source must not be the browser project root. Use an external "
            "checkout (the default is {}) or pass --source-root explicitly.".format(
                _default_source_root(repo)
            )
        )
    if source == build:
        raise ToolError("Chromium source and build roots must be different: {}".format(source))
    if not _within(build, source):
        raise ToolError(
            "GN build root must be a subdirectory of the Chromium source root. "
            "Choose --build-root {}/out/<configuration>, not {}.".format(source, build)
        )
    relative_build = build.relative_to(source)
    if relative_build.parts[0] in (".git", ".cipd"):
        raise ToolError("Refusing to use checkout metadata as build output: {}".format(build))
    if relative_build.parts[0] != "out" and build.exists():
        protected = (build / "BUILD.gn").exists() or (build / "DEPS").exists()
        if protected:
            raise ToolError("Refusing to use a source-code directory as build output: {}".format(build))
    if source.exists() and not source.is_dir():
        raise ToolError("Chromium source root is not a directory: {}".format(source))
    if build.exists() and not build.is_dir():
        raise ToolError("Build root is not a directory: {}".format(build))

    return ToolPaths(repo_root=repo, source_root=source, build_root=build)


def _gn_bool(value: bool) -> str:
    return "true" if value else "false"


def generate_gn_args(platform_name: str, config: str, target_cpu: str = "x64") -> str:
    """Return stable, intentionally small GN configuration text.

    Only build-shape and diagnostics settings are owned here.  Chromium's
    platform security, process isolation, and sandbox defaults are left to the
    upstream toolchain rather than being overridden by this project.
    """

    platform_value = detect_platform(platform_name)
    if config not in SUPPORTED_CONFIGS:
        raise ToolError(
            "Unsupported configuration {!r}; choose debug or release.".format(config)
        )
    if target_cpu not in ("x64", "arm64"):
        raise ToolError("Unsupported target CPU {!r}; choose x64 or arm64.".format(target_cpu))

    is_debug = config == "debug"
    symbol_level = 2 if is_debug else 1
    # Keep ordering alphabetical-ish and fixed: this file is suitable for
    # reproducible reviews and does not inherit values from the host shell.
    gn_target_os = "win" if platform_value == "windows" else "linux"
    lines = [
        "# Generated by scripts/bootstrap.py; edit the script, not this file.",
        "blink_symbol_level = {}".format(2 if is_debug else 0),
        "dcheck_always_on = {}".format(_gn_bool(is_debug)),
        'ffmpeg_branding = "Chromium"',
        "is_chrome_branded = false",
        "is_component_build = false",
        "is_debug = {}".format(_gn_bool(is_debug)),
        "is_official_build = false",
        "proprietary_codecs = false",
        "symbol_level = {}".format(symbol_level),
        'target_cpu = "{}"'.format(target_cpu),
        'target_os = "{}"'.format(gn_target_os),
    ]
    return "\n".join(lines) + "\n"


def write_gn_args(
    paths: ToolPaths,
    platform_name: str,
    config: str,
    target_cpu: str = "x64",
    overwrite: bool = False,
) -> Path:
    """Write the deterministic args.gn file and return its path."""

    text = generate_gn_args(platform_name, config, target_cpu)
    args_path = paths.build_root / "args.gn"
    if args_path.is_symlink():
        raise ToolError("Refusing to overwrite a symlinked args.gn: {}".format(args_path))
    if args_path.is_file() and args_path.read_text(encoding="utf-8") != text and not overwrite:
        raise ToolError(
            "Existing {} differs from the requested configuration. Use a separate "
            "--build-root or explicitly pass --overwrite-args to replace it.".format(args_path)
        )
    paths.build_root.mkdir(parents=True, exist_ok=True)
    # Explicit newline avoids host-dependent CRLF in otherwise deterministic args.
    with args_path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(text)
    return args_path


def _which(candidates: Iterable[str]) -> Optional[str]:
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return found
    return None


def find_gn(source_root: Path, platform_name: str, repo_root: Optional[Path] = None) -> Optional[str]:
    """Locate GN in Chromium's checked-in buildtools or on PATH."""

    platform_value = detect_platform(platform_name)
    if platform_value == "linux":
        names = ("linux64/gn", "linux/gn")
    else:
        names = ("win/gn.exe", "win/gn")
    for relative in names:
        candidate = source_root / "buildtools" / relative
        if candidate.is_file():
            return str(candidate)
    return find_depot_tool("gn", repo_root) or _which(("gn.exe", "gn"))


def find_ninja(source_root: Path, repo_root: Optional[Path] = None) -> Optional[str]:
    return find_depot_tool("autoninja", repo_root) or find_depot_tool("ninja", repo_root) or _which(("ninja", "ninja.exe"))


def find_depot_tool(name: str, repo_root: Optional[Path] = None) -> Optional[str]:
    names = (name + ".bat", name + ".cmd", name) if os.name == "nt" else (name,)
    for filename in names:
        candidate = depot_tools_root(repo_root) / filename
        if candidate.is_file() and (os.name == "nt" or os.access(candidate, os.X_OK)):
            return str(candidate)
    return _which(names)


def _source_looks_valid(source: Path) -> bool:
    return source.is_dir() and (source / "DEPS").is_file()


def _check_command(name: str, candidates: Sequence[str], required: bool) -> CheckResult:
    found = _which(candidates)
    if found:
        return CheckResult(name, True, required, found)
    return CheckResult(name, False, required, "not found (looked for {})".format(", ".join(candidates)))


def _visual_studio_root() -> Optional[str]:
    configured = os.environ.get("VCToolsInstallDir")
    if configured and Path(configured).is_dir():
        return configured
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if os.name == "nt" and vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires",
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            capture_output=True, text=True, check=False,
        )
        root = result.stdout.strip()
        if result.returncode == 0 and root and (Path(root) / "VC" / "Tools" / "MSVC").is_dir():
            return root
    return None


def collect_checks(paths: ToolPaths, platform_name: str, source_operation: bool = False) -> List[CheckResult]:
    """Collect deterministic prerequisite and path checks for ``--check``."""

    platform_value = detect_platform(platform_name)
    results: List[CheckResult] = [
        CheckResult("project root", paths.repo_root.is_dir(), True, str(paths.repo_root)),
        CheckResult("host platform", sys.platform.startswith("win" if platform_value == "windows" else "linux"),
                    True, "{} (requested {})".format(sys.platform, platform_value)),
        CheckResult("Python 3.8+", sys.version_info >= (3, 8), True, sys.version.split()[0]),
        CheckResult(
            "Chromium source",
            _source_looks_valid(paths.source_root),
            True,
            str(paths.source_root),
        ),
    ]

    if platform_value == "linux":
        results.extend(
            [
                _check_command("git", ("git",), True),
                _check_command("pkg-config", ("pkg-config",), True),
            ]
        )
    else:
        results.extend(
            [
                _check_command("git", ("git.exe", "git"), True),
            ]
        )

    clang_name = "clang-cl.exe" if platform_value == "windows" else "clang"
    clang = paths.source_root / "third_party" / "llvm-build" / "Release+Asserts" / "bin" / clang_name
    results.append(CheckResult("Chromium clang", clang.is_file(), True, str(clang)))
    ninja = find_ninja(paths.source_root, paths.repo_root)
    results.append(CheckResult("autoninja/ninja", ninja is not None, True, ninja or "not found; run bootstrap.py tools --yes or configure depot_tools on PATH"))
    gclient = find_depot_tool("gclient", paths.repo_root)
    results.append(CheckResult("depot_tools gclient", gclient is not None, True, gclient or "not found; run bootstrap.py tools --yes or configure depot_tools on PATH"))
    if platform_value == "windows":
        visual_studio = _visual_studio_root()
        results.append(CheckResult("Visual Studio C++ tools", visual_studio is not None, True,
                                  visual_studio or "not found; install Visual Studio C++ desktop tools and a Chromium-supported SDK"))
        sdk = os.environ.get("WindowsSdkDir")
        if not sdk:
            sdk = str(Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits" / "10")
        sdk_exists = (Path(sdk) / "Include").is_dir() and (Path(sdk) / "Lib").is_dir()
        results.append(CheckResult("Windows 10/11 SDK", sdk_exists, True, sdk))
        results.append(CheckResult("local Windows toolchain", True, False,
                                  "Child processes use DEPOT_TOOLS_WIN_TOOLCHAIN=0 (your own Visual Studio installation)."))

    existing = paths.build_root
    while not existing.exists() and existing.parent != existing:
        existing = existing.parent
    free_gib = shutil.disk_usage(existing).free / (1024 ** 3)
    results.append(CheckResult("free disk (150 GiB recommended)", free_gib >= 150, False, "{:.1f} GiB available at {}".format(free_gib, existing)))

    gn = find_gn(paths.source_root, platform_value, paths.repo_root)
    results.append(
        CheckResult(
            "GN",
            gn is not None,
            True,
            gn or "not found in Chromium buildtools or PATH",
        )
    )

    if source_operation:
        results.append(CheckResult("gclient (source sync)", gclient is not None, True,
                                   gclient or "not found in local depot_tools or PATH"))
    return results


def print_checks(results: Sequence[CheckResult]) -> bool:
    all_required_ok = True
    for result in results:
        state = "OK" if result.ok else ("MISSING" if result.required else "WARN")
        print("[{}] {:<28} {}".format(state, result.name, result.detail))
        if result.required and not result.ok:
            all_required_ok = False
    return all_required_ok


def _command_text(command: Sequence[str], cwd: Path) -> str:
    display = subprocess.list2cmdline(list(command)) if os.name == "nt" else " ".join(shlex.quote(part) for part in command)
    return "[cwd: {}] {}".format(cwd, display)


def run_command(
    command: Sequence[str], cwd: Path, dry_run: bool = False,
    repo_root: Optional[Path] = None, platform_name: Optional[str] = None,
) -> int:
    """Run a subprocess without a shell, or print it in dry-run mode."""

    print(_command_text(command, cwd))
    if dry_run:
        return 0
    # Windows launches batch wrappers through cmd.exe even with shell=False.
    # Reject shell metacharacters rather than letting user-supplied paths/args
    # be reinterpreted by a depot_tools .bat/.cmd wrapper.
    if os.name == "nt" and Path(command[0]).suffix.lower() in (".bat", ".cmd"):
        if any(any(char in part for char in "&|<>^%\r\n") for part in command[1:]):
            raise ToolError("Windows batch-tool arguments must not contain shell metacharacters; choose simpler paths/arguments.")
    try:
        completed = subprocess.run(list(command), cwd=str(cwd), check=False,
                                   env=subprocess_environment(repo_root, platform_name))
    except OSError as exc:
        raise ToolError(
            "Required command {!r} was not found. Install/configure Chromium's "
            "depot_tools and build prerequisites, then retry. ({})".format(command[0], exc)
        ) from exc
    if completed.returncode != 0:
        raise ToolError(
            "Command failed with exit code {}: {}".format(
                completed.returncode, " ".join(command)
            )
        )
    return completed.returncode


def _confirmation(operation: str, yes: bool, dry_run: bool) -> None:
    if dry_run:
        print("Dry run: no source changes will be made.")
        return
    if yes:
        return
    if not sys.stdin.isatty():
        raise ToolError(
            "Refusing to run {} without confirmation in a non-interactive shell. "
            "Review the printed command and pass --yes explicitly.".format(operation)
        )
    answer = input("{} may download/update source or toolchains. Hooks run only with the explicit hooks action; no OS packages are installed. Type 'yes' to continue: ".format(operation))
    if answer.strip().lower() != "yes":
        raise ToolError("{} cancelled; no source changes were made.".format(operation))


def _checkout_cwd(source_root: Path) -> Path:
    """Return the wrapper directory containing the ``src`` solution."""

    return source_root.parent


def _require_bootstrap_destination(paths: ToolPaths) -> Path:
    if paths.source_root.exists():
        raise ToolError(
            "Source root already exists: {}. Use setup/sync for an existing "
            "checkout, or choose a different --source-root.".format(paths.source_root)
        )
    if paths.source_root.name != "src":
        raise ToolError(
            "Source bootstrap requires a gclient source root ending in 'src' "
            "so the destination is unambiguous. Use --source-root /path/to/chromium/src "
            "or bootstrap manually and use this path for an existing checkout."
        )
    cwd = _checkout_cwd(paths.source_root)
    if cwd.exists():
        if not cwd.is_dir():
            raise ToolError("Checkout wrapper is not a directory: {}".format(cwd))
        conflicts = [entry for entry in cwd.iterdir() if entry.name.startswith(".gclient")]
        if conflicts:
            raise ToolError("Preexisting conflicting gclient files in checkout wrapper: {}".format(
                ", ".join(str(entry) for entry in conflicts)))
    return cwd


def _git(repo_root: Path, dry_run: bool) -> str:
    command = _which(("git", "git.exe"))
    if command is None and not dry_run:
        raise ToolError("git is missing; install/configure Git before this operation.")
    return command or "git"


def _gclient(paths: ToolPaths, dry_run: bool) -> str:
    command = find_depot_tool("gclient", paths.repo_root)
    if command is None and not dry_run:
        raise ToolError("gclient is missing; run `python scripts/bootstrap.py tools --yes` "
                        "to clone pinned local depot_tools, or configure depot_tools on PATH.")
    return command or "gclient"


def verify_revision(checkout: Path, revision: str, repo_root: Path, dry_run: bool) -> None:
    """Fail closed if a release tag does not resolve to the reviewed pin."""

    command = [_git(repo_root, dry_run), "-C", str(checkout), "rev-parse", "HEAD"]
    print("Verify pinned HEAD {}: {}".format(revision, _command_text(command, repo_root)))
    if dry_run:
        return
    result = subprocess.run(command, cwd=str(repo_root), capture_output=True, text=True,
                            check=False, env=subprocess_environment(repo_root))
    if result.returncode or result.stdout.strip() != revision:
        raise ToolError("Pinned revision verification failed at {}: expected {}, got {}. "
                        "Refusing further operations; preserve and inspect this checkout.".format(
                            checkout, revision, result.stdout.strip() or "unknown"))


def clone_source(paths: ToolPaths, platform_name: str, yes: bool, dry_run: bool) -> int:
    """Clone only the pinned release tag; dependencies and hooks stay separate."""

    if not dry_run and platform_name != detect_platform():
        raise ToolError("Source operations require the native platform; use --dry-run to review the plan.")
    cwd = _require_bootstrap_destination(paths)
    pin = load_upstream(paths.repo_root)
    command = [_git(paths.repo_root, dry_run), "clone", "--depth", "1", "--branch",
               pin.chromium_version, "--single-branch", pin.chromium_url, "src"]
    print("Planned pinned source clone: {}".format(_command_text(command, cwd)))
    _confirmation("Chromium source clone", yes, dry_run)
    if not dry_run:
        cwd.mkdir(parents=True, exist_ok=True)
    run_command(command, cwd, dry_run, repo_root=paths.repo_root, platform_name=platform_name)
    verify_revision(paths.source_root, pin.chromium_revision, paths.repo_root, dry_run)
    return 0


def bootstrap_tools(paths: ToolPaths, platform_name: str, yes: bool, dry_run: bool) -> int:
    """Opt-in, pinned depot_tools clone without changing global PATH."""

    pin = load_upstream(paths.repo_root)
    tools = depot_tools_root(paths.repo_root)
    if tools.exists() or tools.is_symlink():
        raise ToolError("depot_tools destination already exists: {}. Preserve it; "
                        "this command never resets or replaces existing tools.".format(tools))
    git = _git(paths.repo_root, dry_run)
    commands = [
        [git, "clone", "--depth", "1", "--no-checkout", pin.depot_tools_url, str(tools)],
        [git, "-C", str(tools), "fetch", "--depth", "1", "origin", pin.depot_tools_revision],
        [git, "-C", str(tools), "checkout", "--detach", pin.depot_tools_revision],
    ]
    for command in commands:
        print("Planned depot_tools setup: {}".format(_command_text(command, paths.repo_root)))
    _confirmation("Pinned depot_tools clone", yes, dry_run)
    if not dry_run:
        tools.parent.mkdir(parents=True, exist_ok=True)
    for command in commands:
        run_command(command, paths.repo_root, dry_run, repo_root=paths.repo_root, platform_name=platform_name)
    verify_revision(tools, pin.depot_tools_revision, paths.repo_root, dry_run)
    if platform_name == "linux":
        run_command([str(tools / "ensure_bootstrap")], paths.repo_root, dry_run,
                    repo_root=paths.repo_root, platform_name=platform_name)
    print("Local tools are discovered automatically; child processes use DEPOT_TOOLS_UPDATE=0.")
    return 0


def _require_own_checkout(paths: ToolPaths, git: str) -> None:
    top = subprocess.run([git, "-C", str(paths.source_root), "rev-parse", "--show-toplevel"],
                         capture_output=True, text=True, check=False,
                         env=subprocess_environment(paths.repo_root))
    if top.returncode or Path(top.stdout.strip()).resolve() != paths.source_root:
        raise ToolError("The source root must be its own Git checkout; refusing to modify an unrelated parent repository.")


def setup_source(paths: ToolPaths, yes: bool, dry_run: bool, platform_name: Optional[str] = None) -> int:
    """Create an unmanaged .gclient only after checking the cloned release."""

    pin = load_upstream(paths.repo_root)
    if paths.source_root.name != "src":
        raise ToolError("gclient setup requires a source root ending in 'src'.")
    cwd = _checkout_cwd(paths.source_root)
    if cwd.exists() and any(entry.name.startswith(".gclient") for entry in cwd.iterdir()):
        raise ToolError("Preexisting conflicting gclient files; setup will not overwrite {}.".format(cwd / ".gclient"))
    gclient = _gclient(paths, dry_run)
    if not dry_run:
        if not _source_looks_valid(paths.source_root):
            raise ToolError("Source is incomplete (missing DEPS); run clone/bootstrap first.")
        _require_own_checkout(paths, _git(paths.repo_root, dry_run))
    verify_revision(paths.source_root, pin.chromium_revision, paths.repo_root, dry_run)
    command = [gclient, "config", "--name", "src", "--unmanaged", pin.chromium_url]
    print("Planned gclient setup: {}".format(_command_text(command, cwd)))
    _confirmation("gclient setup", yes, dry_run)
    return run_command(command, cwd, dry_run, repo_root=paths.repo_root, platform_name=platform_name)


def bootstrap_source(paths: ToolPaths, platform_name: str, yes: bool, dry_run: bool) -> int:
    """Pinned clone, verification, gclient setup and no-history/no-hooks sync."""

    _require_bootstrap_destination(paths)
    pin = load_upstream(paths.repo_root)
    _git(paths.repo_root, dry_run)
    _gclient(paths, dry_run)
    print("Pinned bootstrap: tag {} -> {}; clone, verify, setup, sync --no-history --nohooks. "
          "Run hooks separately after reviewing them.".format(pin.chromium_version, pin.chromium_revision))
    _confirmation("Pinned Chromium source bootstrap", yes, dry_run)
    clone_source(paths, platform_name, yes=True, dry_run=dry_run)
    setup_source(paths, yes=True, dry_run=dry_run, platform_name=platform_name)
    # The just-created clone is pinned and clean; no force/reset/deletion flags.
    cwd = _checkout_cwd(paths.source_root)
    command = [_gclient(paths, dry_run), "sync", "--no-history", "--nohooks",
               "--revision", "src@" + pin.chromium_revision]
    run_command(command, cwd, dry_run, repo_root=paths.repo_root, platform_name=platform_name)
    verify_revision(paths.source_root, pin.chromium_revision, paths.repo_root, dry_run)
    return 0


def _gclient_cwd(source_root: Path) -> Optional[Path]:
    candidates = [source_root.parent / ".gclient", source_root / ".gclient"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.parent
    return None


def update_source(
    paths: ToolPaths, yes: bool, dry_run: bool, revision: Optional[str] = None,
    platform_name: Optional[str] = None,
) -> int:
    if not _source_looks_valid(paths.source_root):
        raise ToolError(
            "Cannot update {}; it does not look like a Chromium source checkout "
            "(missing DEPS). Run --bootstrap-source first.".format(paths.source_root)
        )
    cwd = _gclient_cwd(paths.source_root)
    if cwd is None:
        raise ToolError(
            "Cannot locate .gclient for {}. Run gclient from the checkout wrapper "
            "directory or pass the correct --source-root.".format(paths.source_root)
        )
    revision = revision if revision is not None else load_upstream(paths.repo_root).chromium_revision
    if revision.startswith("-") or not re.fullmatch(r"[A-Za-z0-9_./-]+", revision):
        raise ToolError("--revision must be a Git hash, tag, or ref using letters, numbers, '_', '.', '/', or '-'; no leading '-'.")
    gclient = _gclient(paths, dry_run)
    solution = paths.source_root.relative_to(cwd).as_posix()
    command = [gclient, "sync", "--no-history", "--nohooks", "--revision", "{}@{}".format(solution, revision)]
    print("Planned source update: {}".format(_command_text(command, cwd)))
    print("No reset, force, or deletion flags are used. Review upstream changes and preserve fork commits before syncing.")
    if not dry_run:
        git = _git(paths.repo_root, dry_run)
        status = subprocess.run([git, "-C", str(paths.source_root), "status", "--porcelain"],
                                capture_output=True, text=True, check=False,
                                env=subprocess_environment(paths.repo_root, platform_name))
        if status.returncode:
            raise ToolError("Cannot inspect the Git worktree; refusing to update source.")
        if status.stdout.strip():
            raise ToolError("Chromium worktree has local changes; preserve them before updating. No source changes were made.")
        _require_own_checkout(paths, git)
    _confirmation("Chromium source update", yes, dry_run)
    result = run_command(command, cwd, dry_run=dry_run, repo_root=paths.repo_root, platform_name=platform_name)
    if re.fullmatch(r"[0-9a-f]{40}", revision):
        verify_revision(paths.source_root, revision, paths.repo_root, dry_run)
    return result


def run_hooks(paths: ToolPaths, yes: bool, dry_run: bool, platform_name: Optional[str] = None) -> int:
    """Explicit upstream hooks may download toolchains, never OS packages."""

    if not _source_looks_valid(paths.source_root):
        raise ToolError("Cannot run hooks: Chromium source is incomplete (missing DEPS).")
    cwd = _gclient_cwd(paths.source_root)
    if cwd is None:
        raise ToolError("Cannot run hooks: missing .gclient; run setup first.")
    gclient = _gclient(paths, dry_run)
    if not dry_run:
        _require_own_checkout(paths, _git(paths.repo_root, dry_run))
    command = [gclient, "runhooks"]
    print("Planned explicit hooks (may download Chromium toolchains): {}".format(_command_text(command, cwd)))
    _confirmation("Explicit Chromium hooks", yes, dry_run)
    return run_command(command, cwd, dry_run, repo_root=paths.repo_root, platform_name=platform_name)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate or safely bootstrap an external Chromium checkout."
    )
    parser.add_argument(
        "action",
        nargs="?",
        choices=("check", "tools", "clone", "setup", "bootstrap", "sync", "update", "hooks", "gn-args"),
        default=None,
        help="check (default); tools; clone; setup; bootstrap (clone/setup/sync); sync/update; hooks; gn-args",
    )
    parser.add_argument("--source-root", help="Chromium source root (default: .local/chromium/src)")
    parser.add_argument("--build-root", help="GN/Ninja output root (default: Chromium source/out/<platform>-<config>)")
    parser.add_argument("--platform", default="auto", choices=("auto", "linux", "windows"))
    parser.add_argument("--config", default="release", choices=SUPPORTED_CONFIGS)
    parser.add_argument("--target-cpu", default="x64", choices=("x64", "arm64"))
    parser.add_argument("--revision", help="explicit sync/update override (default: config/upstream.json pin)")
    parser.add_argument("--check", action="store_true", help="validate paths and prerequisites")
    parser.add_argument("--bootstrap-tools", action="store_true", help="clone pinned depot_tools into .local/tools/depot_tools")
    parser.add_argument("--clone-source", action="store_true", help="shallow-clone and verify the pinned Chromium release only")
    parser.add_argument("--setup-source", action="store_true", help="create .gclient for an already cloned pinned source")
    parser.add_argument("--bootstrap-source", action="store_true", help="pinned source clone, verification, .gclient setup and sync (no hooks)")
    parser.add_argument("--update-source", action="store_true", help="explicit pinned gclient sync --no-history --nohooks")
    parser.add_argument("--run-hooks", action="store_true", help="explicitly run upstream hooks/toolchain downloads (no OS packages)")
    parser.add_argument("--dry-run", action="store_true", help="print commands without executing them or writing files")
    parser.add_argument("--yes", action="store_true", help="confirm the selected opt-in operation without prompting")
    parser.add_argument("--print-gn-args", action="store_true", help="print deterministic GN args")
    parser.add_argument("--write-gn-args", action="store_true", help="write args.gn under --build-root")
    parser.add_argument("--overwrite-args", action="store_true", help="explicitly replace a different existing args.gn")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        platform_value = detect_platform(args.platform)
        paths = resolve_paths(
            project_root(), args.source_root, args.build_root, platform_value, args.config
        )
        action_flags = {
            "tools": "bootstrap_tools", "clone": "clone_source", "setup": "setup_source",
            "bootstrap": "bootstrap_source", "sync": "update_source", "update": "update_source",
            "hooks": "run_hooks",
        }
        if args.action in action_flags:
            setattr(args, action_flags[args.action], True)
        operations = sum(int(getattr(args, flag)) for flag in set(action_flags.values()))
        if operations > 1:
            raise ToolError("Choose one opt-in tools/source/setup/sync/hooks operation at a time.")
        if operations and not args.dry_run and platform_value != detect_platform():
            raise ToolError("Source operations must use the native host platform; use --dry-run to review another platform.")
        read_only = args.check or args.action == "check"
        if read_only and operations:
            raise ToolError("--check is read-only and cannot be combined with a source operation.")
        if read_only and args.write_gn_args:
            raise ToolError("--check is read-only and cannot be combined with --write-gn-args.")
        if operations and args.write_gn_args:
            raise ToolError("Source operations and --write-gn-args must be run separately.")
        if args.revision and not args.update_source:
            raise ToolError("--revision is only used with update/--update-source.")

        if args.print_gn_args or args.action == "gn-args":
            print(generate_gn_args(platform_value, args.config, args.target_cpu), end="")

        if args.write_gn_args:
            if args.dry_run:
                print("Dry run: would write {}".format(paths.build_root / "args.gn"))
            else:
                if not _source_looks_valid(paths.source_root):
                    raise ToolError("Chromium source is missing at {}; refusing to create a partial checkout just to write args.gn.".format(paths.source_root))
                print("Wrote {}".format(write_gn_args(paths, platform_value, args.config, args.target_cpu, args.overwrite_args)))

        if read_only or (not operations and args.action is None and not (args.print_gn_args or args.write_gn_args)):
            results = collect_checks(paths, platform_value, source_operation=operations > 0)
            success = print_checks(results)
            print("Read-only check: no downloads or packages installed. Upstream Chromium still validates toolchain versions and distro libraries.")
            if platform_value == "linux":
                print("For distro-specific prerequisites, review Chromium's build/install-build-deps.sh manually; this wrapper never runs it.")
            return 0 if success else 1

        if args.bootstrap_source:
            return bootstrap_source(paths, platform_value, args.yes, args.dry_run)
        if args.bootstrap_tools:
            return bootstrap_tools(paths, platform_value, args.yes, args.dry_run)
        if args.clone_source:
            return clone_source(paths, platform_value, args.yes, args.dry_run)
        if args.setup_source:
            return setup_source(paths, args.yes, args.dry_run, platform_value)
        if args.update_source:
            return update_source(paths, args.yes, args.dry_run, args.revision, platform_value)
        if args.run_hooks:
            return run_hooks(paths, args.yes, args.dry_run, platform_value)
        return 0
    except ToolError as exc:
        print("bootstrap.py: error: {}".format(exc), file=sys.stderr)
        return 2
    except OSError as exc:
        print("bootstrap.py: error: {}".format(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
