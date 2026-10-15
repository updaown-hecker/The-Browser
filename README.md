# Blynx

An independent **native Chromium distribution** for Linux and Windows. The
browser remains C++/Views, Blink, V8, Chromium networking and sandboxed processes.
No Electron, system WebView shell, application server, or proprietary Google
code is substituted for Chromium.

## Current truth

This is **development source, not a daily-driver release**. Chromium source,
dependencies and hooks have been bootstrapped, the five fork patches are
integrated, and the Linux release browser links successfully. The isolated
identity/appearance tests pass with Chromium's Clang/libc++ and standalone CMake.
Actual-browser smoke checks cover the Blynx new-tab canvas, `blynx://` routing,
Settings Apply/Cancel, local JavaScript/navigation, multiple tabs, downloads, a
local MV3 extension and sandbox status. Windows execution and release packaging
remain unverified. See `HANDOFF.md` for exact results and limitations.

The nine focused Chromium PrefService tests also pass, including disk-backed
preference recreation and managed/read-only write refusal. Full native policy,
incognito and inherited browser regression suites have not been executed.

Implemented browser-specific source:
- Separate branding and Linux/Windows profile-path patches.
- Designed new-tab canvas using the Blynx eye logo, Aurora/Midnight/Paper
  backgrounds and optional shortcuts; retains Chromium search and most-visited.
- `blynx://` navigation aliases and branded address-bar display, with legacy
  `chrome://` links accepted. Original internal origins/permissions stay intact.
- `blynx://webstore` opens the official Chrome Web Store; it is not a separate
  Blynx marketplace and installation compatibility remains unverified.
- Owned Linux/Views icons and a user-local application-menu launcher.
- Strict, bounded, versioned appearance presets and three built-in presets.
  Settings can import a JSON file into the preview and export the saved preset
  through Chromium downloads. Only Apply saves imported appearance.
- Chromium PrefService persistence and policy/private-profile guards.
- Settings → Appearance contains Blynx theme, accent, bookmarks, vertical-tab
  layout and new-tab canvas controls with Apply/Cancel and profile/policy guards.
  A local illustrative preview updates as you edit; Apply alone saves and Cancel
  discards staged edits. The former toolbar popup is removed. Upstream settings also provide fonts,
  zoom, hover-card options, tab layout and other ordinary browser preferences.
- Pinned, safety-checked bootstrap, overlay integration and build scripts.
- Automated tooling/config tests and a local headless benchmark harness.

Upstream supplies ordinary browsing, profiles, DevTools and extensions; inheriting
source is **not** evidence that these work in our distribution. Custom workspaces,
privacy blocker, further vertical-tab customization and other requested systems are not
implemented. Package creation fails closed until distribution identity and
security validation are complete.

## Quick start (Linux)

```sh
python3 scripts/bootstrap.py tools --yes
python3 scripts/bootstrap.py bootstrap --yes
# Review upstream build/install-build-deps.py before any system installation.
python3 scripts/bootstrap.py hooks --yes
python3 scripts/integrate.py --check
python3 scripts/integrate.py
python3 scripts/build.py configure
python3 scripts/build.py build -j 4
python3 scripts/build.py run -- --ozone-platform=wayland
```

For the branded development application on this host:

```sh
./blynx
# Optional: use X11 explicitly
./blynx --ozone-platform=x11
# Install/update the user-local application menu entry
python3 -B scripts/install_launcher.py
```

The application menu entry is **Blynx**. The previous `browser-final` command
remains a compatibility alias; legacy `BrowserFinal` disk paths and internal
preference identifiers are preserved so existing development data stays in place. The launcher selects Wayland
when available, preserves explicit platform flags and the development profile,
and uses the existing root-owned Chrome sandbox helper when available. It does
not build the browser or change OS policy. See `BUILD_LINUX.md` for details.

The existing machine already has the first four operations completed. Do not
clone over the existing checkout. `python3 scripts/bootstrap.py --check` is
read-only; consult `BUILD_LINUX.md` / `BUILD_WINDOWS.md` before proceeding.

```sh
python3 -B -m unittest discover -s tests -v
python3 scripts/build.py test --test-target browser:browser_identity_appearance_unittests -j 4
python3 scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4
cmake -S . -B .local/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .local/build/release
ctest --test-dir .local/build/release --output-on-failure
```

CMake checks **only the isolated library**, never builds the browser.

## Layout

`browser/` owned C++/GRIT/tests; `patches/` ordered upstream integration;
`config/upstream.json` exact pins; `scripts/` Python stdlib tooling;
`benchmarks/` local workload/runner; `tests/` tooling tests.
`.local/chromium/src` and `.local/tools/depot_tools` are ignored, separate Git
repositories inside this workspace. Outputs, development profiles, logs and raw
measurements stay ignored. No supplied credentials belong in these files.

Documentation: `ARCHITECTURE.md`, `SECURITY.md`, `EXTENSIONS.md`, `UPSTREAM.md`,
`PERFORMANCE.md`, `CHANGELOG.md`, `TODO.md`, and `HANDOFF.md`. The current
session status and exact commands/results are maintained in `HANDOFF.md`.

First-time regular profiles have a three-step Blynx setup in Settings → Appearance:
welcome, optional data transfer, and personalization. Existing profiles can choose
“Set up Blynx” there. The transfer button opens Chromium's browser-data importer;
choose a detected source or a bookmarks HTML file and confirm the supported data.
Available types vary by browser/platform; moving every password, extension and
session is not guaranteed. Apply saves appearance; Start browsing completes setup.
Incomplete setup resumes on the next native launch.

Browser-owned labels and product glyphs use Blynx. Google account/security
providers and the official external Chrome Web Store keep their real identities;
Blynx does not operate a separate extension store or Google-account backend.

On Linux, the pinned direct profile importer detects Firefox. For Chrome, Edge
or other browsers, use exported bookmarks HTML and the Password Manager's CSV
importer, both reachable from setup. Extensions need reinstalling; unsupported
history and session data do not transfer automatically.

For organized build, launch, test, debug and troubleshooting commands, see
[DEBUG_AND_TEST_COMMANDS.md](DEBUG_AND_TEST_COMMANDS.md).
