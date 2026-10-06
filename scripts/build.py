#!/usr/bin/env python3
"""Configure, build, run, test, and package the Chromium checkout.

This is a thin, cross-platform delegation layer around GN and Ninja.  It does
not use CMake, does not install dependencies, and never adds unsafe browser
runtime switches.  Run ``python scripts/build.py --help`` for the complete
interface.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Sequence

try:
    from bootstrap import (
        SUPPORTED_CONFIGS,
        ToolError,
        ToolPaths,
        detect_platform,
        find_ninja,
        find_gn,
        generate_gn_args,
        load_upstream,
        project_root,
        resolve_paths,
        run_command,
        subprocess_environment,
        write_gn_args,
    )
except ImportError:  # pragma: no cover - supports `python -m scripts.build`
    from scripts.bootstrap import (
        SUPPORTED_CONFIGS,
        ToolError,
        ToolPaths,
        detect_platform,
        find_ninja,
        find_gn,
        generate_gn_args,
        load_upstream,
        project_root,
        resolve_paths,
        run_command,
        subprocess_environment,
        write_gn_args,
    )


def _ninja(source_root: Path, repo_root: Optional[Path] = None) -> str:
    # autoninja knows Chromium's preferred compiler/linker settings.  Plain
    # ninja remains a useful fallback for environments without depot_tools.
    return find_ninja(source_root, repo_root) or "autoninja"


def _gn(source_root: Path, platform_name: str, repo_root: Optional[Path] = None) -> str:
    return find_gn(source_root, platform_name, repo_root) or ("gn.exe" if platform_name == "windows" else "gn")


def _require_source(paths: ToolPaths) -> None:
    if not paths.source_root.is_dir() or not (paths.source_root / "DEPS").is_file():
        raise ToolError(
            "Chromium source is unavailable or incomplete at {} (missing DEPS). "
            "Run `python scripts/bootstrap.py --bootstrap-source --yes` only "
            "after reviewing the source operation, or pass --source-root.".format(
                paths.source_root
            )
        )


def _require_args(paths: ToolPaths, generated: bool = False) -> Path:
    args_path = paths.build_root / "args.gn"
    if not args_path.is_file():
        raise ToolError(
            "No {}. Run `python scripts/build.py configure` first.".format(args_path)
        )
    if generated and not (paths.build_root / "build.ninja").is_file():
        raise ToolError("No generated build.ninja in {}; run configure successfully first.".format(paths.build_root))
    return args_path


def _require_integration(paths: ToolPaths) -> None:
    """Require a current overlay receipt AND the installed reviewed patches."""

    manifest = paths.source_root / ".browser-final-overlay.json"
    instruction = "Run `python scripts/integrate.py --source-root {}` first; refusing to build stock Chromium.".format(paths.source_root)
    if manifest.is_symlink() or not manifest.is_file():
        raise ToolError("Missing or symlinked fork integration manifest {}. {}".format(manifest, instruction))
    try:
        record = json.loads(manifest.read_text(encoding="utf-8"))
        pin = load_upstream(paths.repo_root)
        if not isinstance(record, dict) or record.get("revision") != pin.chromium_revision:
            raise ValueError("integration revision does not match config/upstream.json")
        files = {}
        for origin in sorted((paths.repo_root / "browser").rglob("*")):
            if origin.is_symlink():
                raise ValueError("symlinked overlay input: {}".format(origin))
            if not origin.is_file():
                continue
            relative = origin.relative_to(paths.repo_root).as_posix()
            target = paths.source_root / relative
            for parent in [target, *target.parents]:
                if parent == paths.source_root:
                    break
                if parent.is_symlink():
                    raise ValueError("symlinked overlay destination: {}".format(parent))
            wanted = hashlib.sha256(origin.read_bytes()).hexdigest()
            if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != wanted:
                raise ValueError("missing or edited overlay: {}".format(relative))
            files[relative] = wanted
        patches = {}
        patch_paths = []
        for entry in (paths.repo_root / "patches" / "series").read_text(encoding="utf-8").splitlines():
            if not entry or entry.startswith("#"):
                continue
            if Path(entry).name != entry or not entry.endswith(".patch"):
                raise ValueError("invalid patch series entry: {}".format(entry))
            patch = paths.repo_root / "patches" / entry
            if patch.is_symlink() or not patch.is_file():
                raise ValueError("missing or symlinked patch: {}".format(patch))
            patches[entry] = hashlib.sha256(patch.read_bytes()).hexdigest()
            patch_paths.append(str(patch))
        if not files or not patches or record.get("files") != files or record.get("patches") != patches:
            raise ValueError("integration manifest is incomplete or stale")
        for args in (["rev-parse", "HEAD"], ["rev-parse", "--show-toplevel"],
                     ["apply", "--reverse", "--check", *patch_paths]):
            result = subprocess.run(["git", *args], cwd=str(paths.source_root), capture_output=True,
                                    text=True, check=False, env=subprocess_environment(paths.repo_root))
            if result.returncode:
                raise ValueError("cannot verify installed fork patches/checkout: {}".format(result.stderr.strip()))
            if args == ["rev-parse", "HEAD"] and result.stdout.strip() != pin.chromium_revision:
                raise ValueError("source HEAD no longer matches the pinned revision")
            if args == ["rev-parse", "--show-toplevel"] and Path(result.stdout.strip()).resolve() != paths.source_root:
                raise ValueError("source root is not its own Git checkout")
    except (OSError, ValueError, TypeError) as exc:
        raise ToolError("Invalid fork integration at {}: {}. {}".format(manifest, exc, instruction)) from exc


def _binary_name(target: str, platform_name: str) -> str:
    leaf = Path(target.replace("\\", "/")).name.rsplit(":", 1)[-1]
    if platform_name == "windows" and not leaf.lower().endswith(".exe"):
        return leaf + ".exe"
    return leaf


UNSAFE_BROWSER_SWITCHES = frozenset(
    {
        "--no-sandbox",
        "--disable-setuid-sandbox",
        "--disable-web-security",
        "--disable-site-isolation-trials",
        "--disable-gpu-sandbox",
        "--disable-seccomp-filter-sandbox",
        "--single-process",
        "--in-process-gpu",
        "--ignore-certificate-errors",
        "--allow-running-insecure-content",
    }
)


def _validate_browser_args(browser_args: Sequence[str]) -> None:
    unsafe = []
    for value in browser_args:
        switch = value.split("=", 1)[0]
        security_features = {"IsolateOrigins", "SitePerProcess", "site-per-process", "StrictOriginIsolation"}
        disables_security = switch == "--disable-features" and bool(
            security_features.intersection(value.partition("=")[2].split(","))
        )
        if switch in UNSAFE_BROWSER_SWITCHES or disables_security:
            unsafe.append(value)
    if unsafe:
        raise ToolError(
            "Refusing unsafe browser switch(es): {}. The build tooling preserves "
            "Chromium sandboxing, web security, and site isolation.".format(
                ", ".join(unsafe)
            )
        )


def configure(
    paths: ToolPaths,
    platform_name: str,
    config: str,
    dry_run: bool,
    target_cpu: str = "x64",
    overwrite_args: bool = False,
) -> int:
    if not dry_run:
        _require_source(paths)
        _require_integration(paths)
        if find_gn(paths.source_root, platform_name, paths.repo_root) is None:
            raise ToolError("GN is missing. Complete Chromium's source sync/hooks or put GN on PATH; inspect bootstrap.py --check.")
    args_text = generate_gn_args(platform_name, config, target_cpu)
    args_path = paths.build_root / "args.gn"
    if dry_run:
        print("Dry run: would write {} with deterministic GN args:".format(args_path))
        print(args_text, end="")
    else:
        print("Wrote {}".format(write_gn_args(paths, platform_name, config, target_cpu, overwrite_args)))
    return run_command([_gn(paths.source_root, platform_name, paths.repo_root), "gen", str(paths.build_root)],
                       paths.source_root, dry_run, repo_root=paths.repo_root, platform_name=platform_name)


def build_target(paths: ToolPaths, target: str, jobs: Optional[int], dry_run: bool) -> int:
    target = _validate_target(target)
    if not dry_run:
        _require_source(paths)
        _require_integration(paths)
        _require_args(paths, generated=True)
        if find_ninja(paths.source_root, paths.repo_root) is None:
            raise ToolError("autoninja/ninja is missing; run bootstrap.py tools --yes or configure depot_tools; inspect bootstrap.py --check.")
    ninja = _ninja(paths.source_root, paths.repo_root)
    command: List[str] = [ninja, "-C", str(paths.build_root)]
    if jobs is not None:
        command.extend(["-j", str(jobs)])
    command.append(target)
    return run_command(command, paths.source_root, dry_run, repo_root=paths.repo_root)


def run_browser(
    paths: ToolPaths,
    platform_name: str,
    executable: Optional[str],
    browser_args: Sequence[str],
    dry_run: bool,
) -> int:
    _validate_browser_args(browser_args)
    if not dry_run:
        _require_args(paths)
    if executable:
        executable_path = Path(executable).expanduser()
        if not executable_path.is_absolute():
            executable_path = paths.build_root / executable_path
    else:
        executable_path = paths.build_root / _binary_name("chrome", platform_name)
    if not executable_path.is_file() and not dry_run:
        raise ToolError(
            "Browser executable not found at {}. Build it first with "
            "`python scripts/build.py build`.".format(executable_path)
        )
    command = [str(executable_path)]
    # Never let a development launch accidentally reuse the user's installed
    # Chromium profile. An explicit profile argument remains the user's choice.
    if not any(value.split("=", 1)[0] == "--user-data-dir" for value in browser_args):
        command.append("--user-data-dir={}".format(paths.build_root / "development-profile"))
    command.extend(browser_args)
    return run_command(command, paths.build_root, dry_run, repo_root=paths.repo_root, platform_name=platform_name)


def test_target(
    paths: ToolPaths,
    platform_name: str,
    target: str,
    executable: Optional[str],
    jobs: Optional[int],
    test_args: Sequence[str],
    dry_run: bool,
) -> int:
    _validate_browser_args(test_args)
    build_target(paths, target, jobs, dry_run)
    if executable:
        executable_path = Path(executable).expanduser()
        if not executable_path.is_absolute():
            executable_path = paths.build_root / executable_path
    else:
        executable_path = paths.build_root / _binary_name(target, platform_name)
    if not executable_path.is_file() and not dry_run:
        raise ToolError(
            "Test executable not found at {} after building target {!r}. "
            "Pass --test-executable if the target outputs elsewhere.".format(
                executable_path, target
            )
        )
    return run_command([str(executable_path)] + list(test_args), paths.build_root, dry_run,
                       repo_root=paths.repo_root, platform_name=platform_name)


def _validate_target(target: str) -> str:
    if not target or target.startswith("-") or not re.fullmatch(r"[A-Za-z0-9_./:+@-]+", target):
        raise ToolError("Invalid Ninja target {!r}; use a target name or GN label, not flags or whitespace.".format(target))
    if ".." in target.split("/"):
        raise ToolError("Ninja targets must not contain parent-directory traversal.")
    return target[2:] if target.startswith("//") else target


def package_target(
    paths: ToolPaths,
    target: str,
    jobs: Optional[int],
    dry_run: bool,
) -> int:
    raise ToolError(
        "Release not ready: packaging is disabled until the browser installer identity "
        "(package IDs, executable/profile paths, icons, associations and signing) and "
        "security audit are complete. Upstream Chromium packages must not be shipped "
        "under this browser's branding. No package command was run, including in dry-run mode."
    )


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Delegate Chromium GN/Ninja configure, build, run and test operations; packaging is disabled pending the release identity/security audit.",
        epilog="For run/test arguments, use -- (example: run -- --user-data-dir=/path/to/development-profile)."
    )
    parser.add_argument(
        "action",
        choices=("configure", "build", "run", "test", "package"),
        help="operation to perform",
    )
    parser.add_argument("--source-root", help="Chromium source root")
    parser.add_argument("--build-root", help="GN/Ninja output root")
    parser.add_argument("--platform", default="auto", choices=("auto", "linux", "windows"))
    parser.add_argument("--config", default="release", choices=SUPPORTED_CONFIGS)
    parser.add_argument("--target-cpu", default="x64", choices=("x64", "arm64"))
    parser.add_argument("--overwrite-args", action="store_true", help="explicitly replace a different existing args.gn")
    parser.add_argument("--target", default="chrome", help="Ninja target for build")
    parser.add_argument("--package-target", help="reserved package target override; packaging currently fails closed (release not ready)")
    parser.add_argument("--test-target", default="unit_tests", help="Ninja test target")
    parser.add_argument("--test-executable", help="test executable path override")
    parser.add_argument("--executable", help="browser executable path override for run")
    parser.add_argument("-j", "--jobs", type=int, help="parallel Ninja jobs")
    parser.add_argument("--dry-run", action="store_true", help="print commands without executing them")
    return parser


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    raw = list(sys.argv[1:] if argv is None else argv)
    browser_args: List[str] = []
    if "--" in raw:
        separator = raw.index("--")
        browser_args = raw[separator + 1:]
        raw = raw[:separator]
    parser = make_parser()
    args = parser.parse_args(raw)
    if browser_args and args.action not in ("run", "test"):
        parser.error("arguments after -- are only supported with run/test")
    args.browser_args = browser_args
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        if args.jobs is not None and args.jobs < 1:
            raise ToolError("--jobs must be a positive integer.")
        platform_value = detect_platform(args.platform)
        if not args.dry_run and platform_value != detect_platform():
            raise ToolError("Cross-host builds are not configured by this wrapper. Use a native {} host, or --dry-run for a plan.".format(platform_value))
        paths = resolve_paths(
            project_root(), args.source_root, args.build_root, platform_value, args.config
        )
        if args.action == "configure":
            return configure(paths, platform_value, args.config, args.dry_run, args.target_cpu, args.overwrite_args)
        if args.action == "build":
            return build_target(paths, args.target, args.jobs, args.dry_run)
        if args.action == "run":
            return run_browser(
                paths,
                platform_value,
                args.executable,
                args.browser_args,
                args.dry_run,
            )
        if args.action == "test":
            return test_target(
                paths,
                platform_value,
                args.test_target,
                args.test_executable,
                args.jobs,
                args.browser_args,
                args.dry_run,
            )
        default_package = "mini_installer" if platform_value == "windows" else "chrome/installer/linux:stable_deb"
        return package_target(
            paths,
            args.package_target or default_package,
            args.jobs,
            args.dry_run,
        )
    except ToolError as exc:
        print("build.py: error: {}".format(exc), file=sys.stderr)
        return 2
    except OSError as exc:
        print("build.py: error: {}".format(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
