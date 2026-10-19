#!/usr/bin/env python3
"""Install the owned overlay and reviewed patch series onto the pinned upstream.

Preflight before writes; refuse a different revision, symlinks, edited overlays,
or mixed/partially applied patches. No reset, clean, commit, or network operation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

try:
    from bootstrap import ToolError, project_root, resolve_paths
except ImportError:
    from scripts.bootstrap import ToolError, project_root, resolve_paths


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git(source: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=source, capture_output=True,
                          text=True, check=False)


def validate_customization_catalog(repo: Path) -> None:
    """Keep owned GRIT catalogs identical to their adjacent-GRD patch copies."""
    catalogs = (
        ("customization_strings.grdp", "0002-customization.patch",
         "browser_final_customization_strings.grdp"),
        ("assistant_strings.grdp", "0005-blynx-experience.patch",
         "blynx_assistant_strings.grdp"),
    )
    for owned, patch_name, installed_name in catalogs:
        catalog = repo / "browser/resources" / owned
        if not catalog.is_file():
            continue
        patch = repo / "patches" / patch_name
        header = (f"diff --git a/chrome/app/{installed_name} "
                  f"b/chrome/app/{installed_name}\n")
        if not patch.is_file():
            raise ToolError("Missing reviewed GRIT catalog patch: " + patch_name)
        sections = patch.read_text(encoding="utf-8").split(header)
        if len(sections) != 2:
            raise ToolError("Missing or ambiguous GRIT catalog patch: " + owned)
        section = sections[1].split("diff --git ", 1)[0]
        if "new file mode " not in section:
            raise ToolError("GRIT catalog must be a reviewed added file: " + owned)
        installed = "".join(line[1:] + "\n" for line in section.splitlines()
                            if line.startswith("+") and not line.startswith("+++"))
        if installed != catalog.read_text(encoding="utf-8"):
            raise ToolError("Owned GRIT catalog differs from patches/" + patch_name +
                            "; update both copies together before integration.")


def integrate(repo: Path, source: Path, check_only: bool = False) -> None:
    pin = json.loads((repo / "config/upstream.json").read_text(encoding="utf-8"))
    head = git(source, "rev-parse", "HEAD")
    if head.returncode or head.stdout.strip() != pin["chromium_revision"]:
        raise ToolError("Integration requires pinned Chromium " + pin["chromium_revision"])
    top = git(source, "rev-parse", "--show-toplevel")
    if top.returncode or Path(top.stdout.strip()).resolve() != source.resolve():
        raise ToolError("Source must be its own Git checkout, not a parent repository.")
    series = (repo / "patches/series").read_text(encoding="utf-8").splitlines()
    patches = []
    for entry in series:
        if not entry or entry.startswith("#"):
            continue
        if Path(entry).name != entry or not entry.endswith(".patch"):
            raise ToolError("Invalid patch series entry: " + entry)
        path = repo / "patches" / entry
        if not path.is_file() or path.is_symlink():
            raise ToolError("Missing or symlinked patch: " + str(path))
        patches.append(str(path.resolve()))
    if not patches:
        raise ToolError("Empty patch series.")
    validate_customization_catalog(repo)
    ready = git(source, "apply", "--check", *patches)
    installed = git(source, "apply", "--reverse", "--check", *patches)
    if ready.returncode and installed.returncode:
        raise ToolError("Patch preflight failed; preserve local edits and inspect conflicts:\n" + ready.stderr)

    manifest_path = source / ".browser-final-overlay.json"
    if manifest_path.is_symlink():
        raise ToolError("Refusing symlinked overlay manifest.")
    previous = {}
    if manifest_path.exists():
        record = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(record, dict) or not isinstance(record.get("files"), dict):
            raise ToolError("Invalid overlay manifest; preserve it and inspect before integration.")
        previous = record["files"]
        if any(not isinstance(key, str) or not isinstance(value, str)
               for key, value in previous.items()):
            raise ToolError("Invalid overlay file hashes; refusing integration.")
    desired = {}
    writes = []
    for origin in sorted((repo / "browser").rglob("*")):
        if origin.is_symlink():
            raise ToolError("Refusing symlinked overlay input: " + str(origin))
        if not origin.is_file():
            continue
        relative = origin.relative_to(repo).as_posix()
        target = source / relative
        for parent in [target, *target.parents]:
            if parent == source:
                break
            if parent.is_symlink():
                raise ToolError("Refusing symlinked overlay destination: " + str(parent))
        data = origin.read_bytes()
        wanted = digest(data)
        desired[relative] = wanted
        temporary = target.with_name(target.name + ".browser-final-tmp")
        if temporary.exists() or temporary.is_symlink():
            raise ToolError("Unexpected temporary file: " + str(temporary))
        if target.exists():
            if not target.is_file():
                raise ToolError("Overlay destination is not a file: " + str(target))
            actual = digest(target.read_bytes())
            if actual == wanted:
                continue
            if previous.get(relative) != actual:
                raise ToolError("Overlay contains unowned/local edits; refusing overwrite: " + str(target))
        writes.append((target, data))
    removed = set(previous) - set(desired)
    if removed:
        raise ToolError("Overlay files were removed; review/remove explicitly: " + ", ".join(sorted(removed)))
    state = "already applied" if installed.returncode == 0 else "ready"
    print("Chromium {}: patches {}; {} overlay writes".format(pin["chromium_version"], state, len(writes)))
    if check_only:
        print("Read-only preflight passed. GN/compiler validation is separate.")
        return
    if installed.returncode != 0:
        applied = git(source, "apply", *patches)
        if applied.returncode:
            raise ToolError("Patch application failed: " + applied.stderr)
    for target, data in writes:
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + ".browser-final-tmp")
        if temporary.exists() or temporary.is_symlink():
            raise ToolError("Unexpected temporary file: " + str(temporary))
        with temporary.open("xb") as output:
            output.write(data)
        temporary.replace(target)
    record = {"revision": pin["chromium_revision"], "files": desired,
              "patches": {Path(p).name: digest(Path(p).read_bytes()) for p in patches}}
    manifest_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("Overlay installed; no upstream commits or unrelated files changed.")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        repo = project_root()
        source = resolve_paths(repo, source_root=args.source_root).source_root
        integrate(repo, source, args.check)
        return 0
    except (ToolError, OSError, ValueError) as error:
        print("integrate.py: error: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
