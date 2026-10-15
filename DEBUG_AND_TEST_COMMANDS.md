# Blynx debug and test command reference

Updated October 14, 2026. Commands match this repository's scripts and pinned
Chromium checkout. Linux/Bash is the primary environment; Windows commands have
an explicit section below.

## Contents

1. [Start here](#1-start-here)
2. [Quick checks](#2-quick-checks)
3. [Integrate, build, and launch](#3-integrate-build-and-launch)
4. [Automated browser smoke tests](#4-automated-browser-smoke-tests)
5. [Focused C++ and full Chromium tests](#5-focused-c-and-full-chromium-tests)
6. [Manual UI and onboarding tests](#6-manual-ui-and-onboarding-tests)
7. [Logs and failure reports](#7-logs-and-failure-reports)
8. [DevTools and remote debugging](#8-devtools-and-remote-debugging)
9. [Extension debugging](#9-extension-debugging)
10. [GN, formatting, and resource diagnostics](#10-gn-formatting-and-resource-diagnostics)
11. [Native debugger and debug builds](#11-native-debugger-and-debug-builds)
12. [Standalone library and sanitizers](#12-standalone-library-and-sanitizers)
13. [Performance harness](#13-performance-harness)
14. [Bootstrap and environment checks](#14-bootstrap-and-environment-checks)
15. [Windows commands](#15-windows-commands)
16. [Troubleshooting](#16-troubleshooting)
17. [Current validation and remaining coverage](#17-current-validation-and-remaining-coverage)

## 1. Start here

Run Linux commands from this directory unless a block explicitly changes it:

```bash
cd /home/blynx/Desktop/Browser-Final
mkdir -p .local/logs
```

| Item | Repository-relative location |
|---|---|
| Browser launcher | `./blynx` |
| Chromium checkout | `.local/chromium/src` |
| Release output | `.local/chromium/src/out/linux-release` |
| Native executable | `.local/chromium/src/out/linux-release/chrome` |
| Normal development profile | `.local/chromium/src/out/linux-release/development-profile` |
| Build arguments | `.local/chromium/src/out/linux-release/args.gn` |
| Successful browser build receipt | `.local/chromium/src/out/linux-release/.browser-final-build.json` |
| Owned sources | `browser/` |
| Reviewed upstream integration | `patches/series` and its patch files |
| Current status and remaining work | `HANDOFF.md`, `TODO.md` |

Run only **one build at a time per GN output directory**. Start with `-j 4` on
this shared desktop. Shared GRIT header changes can cause a large rebuild.

Use development or disposable profiles. Keep `.local/`, browser data, logs,
secrets, crash dumps and `PROMPT.txt` out of commits. Keep the sandbox, site
isolation and certificate checks enabled. Close disposable browser windows
normally when finished; no profile deletion is required by this guide.

## 2. Quick checks

These do not build or launch Chromium:

```bash
# Entire Python tooling suite.
python3 -B -m unittest discover -s tests -v

# Shorter output for the same suite.
python3 -B -m unittest discover -s tests -q

# Review installed patches and planned overlay writes without changing files.
python3 -B scripts/integrate.py --check

# Check whitespace in tracked changes.
git diff --check

# Inspect current changes and pins.
git status --short
cat config/upstream.json
```

Run individual tooling groups:

```bash
python3 -B -m unittest discover -s tests -p 'test_tooling.py' -v
python3 -B -m unittest discover -s tests -p 'test_integrate.py' -v
python3 -B -m unittest discover -s tests -p 'test_build_readiness.py' -v
python3 -B -m unittest discover -s tests -p 'test_launcher.py' -v
python3 -B -m unittest discover -s tests -p 'test_customization_catalog.py' -v
python3 -B -m unittest discover -s tests -p 'test_runtime_smoke.py' -v
python3 -B -m unittest discover -s tests -p 'test_benchmarks.py' -v
```

Python tooling tests validate the tools and guards. They do not prove browser UI
behavior. `integrate.py --check` is an integration check, not a compilation test.

## 3. Integrate, build, and launch

### Usual sequence after changing owned browser sources or patches

Run each step only after the previous step succeeds:

```bash
python3 -B scripts/integrate.py --check
python3 -B scripts/integrate.py
python3 -B scripts/build.py build -j 4
./blynx
```

Integration preserves local edits and refuses conflicts. Investigate a refusal;
do not force/reset the upstream checkout to hide it.

### Save a build log and preserve its exit status

```bash
python3 -B scripts/build.py build -j 4 > .local/logs/browser-build.log 2>&1
```

Or watch output while saving it, in Bash:

```bash
set -o pipefail
python3 -B scripts/build.py build -j 4 2>&1 | tee .local/logs/browser-build.log
```

### Configure or preview commands

Configure only when creating an output or deliberately changing its configuration:

```bash
python3 -B scripts/build.py configure --dry-run
python3 -B scripts/build.py configure
python3 -B scripts/build.py build -j 4 --dry-run
python3 -B scripts/build.py run --dry-run
```

The wrapper refuses different existing GN arguments instead of silently replacing
them. `--dry-run` belongs to `scripts/build.py`; `./blynx --dry-run` is **not** a
launcher dry run, because launcher arguments are forwarded to Chromium.

### Launch choices

```bash
# Recommended: selects desktop identity, available sandbox helper and Wayland.
./blynx

# Explicit native display backend.
./blynx --ozone-platform=wayland
./blynx --ozone-platform=x11

# Open a particular internal page.
./blynx blynx://settings/appearance
./blynx blynx://extensions/
```

`x11` requires a working X11/XWayland display. Reusing an already-running profile
can forward the request to that process; use a fresh profile for new-process
launch flags and debugging.

Install or refresh the user-local menu entry, without setting a default browser:

```bash
python3 -B scripts/install_launcher.py
```

## 4. Automated browser smoke tests

A completed, consistent Linux browser build is required. Run these sequentially,
especially native tests and other GUI automation:

```bash
# Headless: local fixtures, disposable profile, DevTools assertions, restarts.
python3 -B scripts/runtime_smoke.py

# Native Wayland: page checks plus actual browser chrome through AT-SPI.
python3 -B scripts/native_smoke.py
```

To save their console output:

```bash
python3 -B scripts/runtime_smoke.py > .local/logs/runtime-smoke.log 2>&1
python3 -B scripts/native_smoke.py > .local/logs/native-smoke.log 2>&1
```

Run the second command only after checking the first result. Native smoke needs
this desktop's Wayland session, Python GI and AT-SPI support. It is not a generic
Windows or headless CI test.

Both runners create their own profiles and local HTTP fixtures, test browser
behavior, then cleanly restart the browser to verify persistence. Current coverage
includes onboarding, importer open/cancel, password/extension setup links,
appearance preview and presets, bookmarks, navigation, downloads, private-context
restrictions, local MV3 extension state and sandbox status. They do not import
personal browser data. OS-native file chooser interaction remains a separate test.

| Output | Headless | Native |
|---|---|---|
| Report | `.local/runtime-smoke/results.json` | `.local/native-smoke/results.json` |
| Browser log | `.local/runtime-smoke/browser.log` | `.local/native-smoke/browser.log` |
| Screenshots | `.local/runtime-smoke/*.png` | `.local/native-smoke/*.png` |

Inspect reports:

```bash
python3 -m json.tool .local/runtime-smoke/results.json
python3 -m json.tool .local/native-smoke/results.json
```

A short summary of both reports:

```bash
python3 -B - <<'PY'
import json
from pathlib import Path
for name in ('runtime-smoke', 'native-smoke'):
    path = Path('.local') / name / 'results.json'
    if not path.is_file():
        print(name, 'not run: report missing')
        continue
    report = json.loads(path.read_text())
    print(name, 'passed=', report.get('passed'),
          'checks=', len(report.get('checks', [])),
          'error=', report.get('error', ''))
PY
```

A nonzero command exit or `passed: false` is a failure. Check the report and log
from that run, rather than relying on an older screenshot or historical result.

## 5. Focused C++ and full Chromium tests

### Small real Chromium-linked targets

```bash
python3 -B scripts/build.py test --test-target browser:browser_identity_appearance_unittests -j 4
python3 -B scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4
```

These build and run the executable. The second target exercises the real
PrefService adapter without linking the entire inherited `unit_tests` binary.

### List or filter focused PrefService tests

```bash
python3 -B scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4 -- --gtest_list_tests
python3 -B scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4 -- --gtest_filter='AppearancePrefStoreTest.FileBackedPrefServiceSurvivesRecreation'
```

Gtest flags apply to the PrefService/gtest executable. The identity/appearance
executable has its own small test main; do not assume it accepts gtest flags.

### Large inherited test executables

These may require substantial compile time, disk space and runtime dependencies.
A gtest filter narrows execution, **not** how much of the binary must be built.

```bash
# PrefService tests inside Chromium's broad unit-test executable.
python3 -B scripts/build.py test --test-target unit_tests -j 4 -- --gtest_filter='AppearancePrefStoreTest.*'

# Native appearance, private/policy behavior and dialog coverage.
python3 -B scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='NativeAppearanceBrowserTest.*:NativeAppearancePolicyBrowserTest.*'

# Real unpacked-extension integration gtest.
python3 -B scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='BrowserFinalExtensionTest.*'

# List browser tests after building that executable.
python3 -B scripts/build.py test --test-target browser_tests -j 4 -- --gtest_list_tests
```

Native browser tests need a usable display. A whole inherited suite is a separate,
large operation; the focused commands above do not constitute that suite.

## 6. Manual UI and onboarding tests

### A fresh profile, without resetting your development profile

```bash
mkdir -p .local/manual-profiles
BLYNX_TEST_PROFILE="$(mktemp -d "$PWD/.local/manual-profiles/onboarding-XXXXXX")"
./blynx --user-data-dir="$BLYNX_TEST_PROFILE" --no-first-run
```

`--no-first-run` suppresses upstream first-run behavior. Blynx still detects a
new preference store and presents its own native-window onboarding. Keep this
terminal open: `$BLYNX_TEST_PROFILE` identifies this specific test profile.

1. Confirm Welcome appears automatically once in the fresh normal window.
2. Use Tab/Shift+Tab and Enter/Space to move through controls. Continue should
   focus the next step heading; focus must stay visible.
3. On transfer, open the importer, inspect available sources, then Cancel.
   Confirm you return to setup without an import being claimed.
4. Open Password Manager and Extensions. They should open in separate tabs and
   leave setup progress intact.
5. Close all windows belonging to this test profile while setup is unfinished.
   Relaunch the same profile and confirm setup resumes:

   ```bash
   ./blynx --user-data-dir="$BLYNX_TEST_PROFILE" --no-first-run
   ```

6. Complete setup, close its windows normally, then repeat that launch command.
   Confirm the wizard stays completed. Chromium may restore a Settings tab;
   that is different from showing an unfinished setup wizard.
7. In Appearance, choose **Set up Blynx** to explicitly reopen setup.
8. Check light/dark mode, accent, tab layout, bookmarks and new-tab canvas.
   Staged edits should not save until Apply. Cancel should restore saved choices.
9. Test valid and invalid appearance preset files using disposable files. Imports
   stage choices; exporting reads the saved preset.
10. Check a private window: Blynx appearance/setup writes should be refused.

For actual data transfer, use a disposable source profile and known test files.
The pinned Linux direct profile detector supports Firefox. Other browsers can
provide bookmarks HTML and password CSV; extensions need reinstalling and
unsupported history/session data does not move automatically. Verify imported
fixture entries and persistence after restart before claiming migration works.

Useful addresses to enter in the address bar:

| Address | What to inspect |
|---|---|
| `blynx://settings/appearance` | Setup, appearance, preset import/export |
| `blynx://settings/importData` | Upstream browser/bookmarks importer |
| `blynx://password-manager/settings` | Password CSV import controls |
| `blynx://extensions/` | Extensions, errors, worker inspection |
| `blynx://version/` | Version, executable/profile paths, command line |
| `blynx://sandbox/` | Sandbox status |
| `blynx://gpu/` | Graphics diagnostics |
| `blynx://downloads/` | Download behavior |
| `blynx://history/` | History UI |
| `blynx://bookmarks/` | Bookmark UI |
| `blynx://net-export/` | Optional network capture; treat its output as sensitive |

The `blynx://` aliases display the Blynx address while retaining the original
trusted `chrome://` document origin. External provider/store identities remain
accurate; the Web Store alias opens the official external Chrome Web Store.

## 7. Logs and failure reports

Capture a browser launch log in a fresh profile:

```bash
mkdir -p .local/manual-profiles
BLYNX_LOG_PROFILE="$(mktemp -d "$PWD/.local/manual-profiles/logging-XXXXXX")"
./blynx --user-data-dir="$BLYNX_LOG_PROFILE" --no-first-run --enable-logging=stderr --v=1 > .local/logs/browser-launch.log 2>&1
```

Close that test browser normally to return to the shell. Verbose logs can contain
URLs and local paths; review before sharing them.

```bash
# Read the end of your captured logs.
tail -n 80 .local/logs/browser-build.log
tail -n 80 .local/logs/browser-launch.log
tail -n 80 .local/runtime-smoke/browser.log
tail -n 80 .local/native-smoke/browser.log

# Find likely diagnostics in the captured build log.
rg -n 'FAILED|error:|error TS|Traceback|Build Failure|✖' .local/logs/browser-build.log

# Inspect the generated failing build commands without executing them.
sed -n '1,100p' .local/chromium/src/out/linux-release/siso_failed_commands.sh
```

The generated failing-command file is relevant only when the latest Siso build
produced it. Prefer rerunning the normal build wrapper after fixing a failure so
the successful-browser receipt is regenerated correctly. No search matches from
`rg` returns exit status 1; that alone does not mean the build failed.

For a useful bug report, record the exact command, exit status, revision/output,
reproduction steps, relevant diagnostic lines and the report's `error` field.
Include a screenshot when the problem is visual. Keep raw profiles, credentials,
crash dumps and unreviewed network captures private.

## 8. DevTools and remote debugging

For a page or Settings WebUI, use **Ctrl+Shift+I** (or the browser menu's developer
tools). Check Console errors, Network failures and the Elements panel. For Lit or
Polymer WebUI, expand shadow roots to inspect the actual component controls.

For a temporary local DevTools endpoint:

```bash
mkdir -p .local/manual-profiles
BLYNX_CDP_PROFILE="$(mktemp -d "$PWD/.local/manual-profiles/devtools-XXXXXX")"
./blynx --user-data-dir="$BLYNX_CDP_PROFILE" --no-first-run --remote-debugging-address=127.0.0.1 --remote-debugging-port=0
```

In a second terminal, use the exact profile directory created above; the shell
variable does not automatically exist in a new terminal. The first line of this
file is the selected port:

```bash
cat /absolute/path/to/that/test-profile/DevToolsActivePort
```

With that port substituted, inspect metadata and format the JSON:

```bash
python3 -m json.tool < <(curl --fail --silent --show-error http://127.0.0.1:PORT/json/list)
```

The metadata command needs `curl` and Bash process substitution. Replace `PORT`
with the numeric port. Keep this endpoint local and close the test browser when
finished. DevTools target metadata may show a `blynx://` virtual URL while the
page's actual origin is `chrome://`; do not treat that display difference as a
navigation failure.

## 9. Extension debugging

The committed test fixture is Manifest V3 at `browser/tests/runtime_extension`.
Its background worker is ephemeral; persistent test state belongs in extension
storage. Keeping its worker inspector open can alter worker lifetime, so close
it when checking suspend/restart behavior.

Automated extension checks are included in:

```bash
python3 -B scripts/runtime_smoke.py
python3 -B scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='BrowserFinalExtensionTest.*'
```

For manual inspection:

1. Open a disposable profile and visit `blynx://extensions/`.
2. Enable Developer mode and choose **Load unpacked** for the repository fixture
   directory, or another extension you explicitly intend to test.
3. Use **Errors** for manifest/runtime errors and the service-worker inspection
   link for its Console. Inspect a popup separately from the worker.
4. Test disable/re-enable, browser restart and stored-state persistence with the
   worker inspector closed. Reload the extension after editing its files.
5. Test permission prompts and private-window opt-in separately. A successful
   local MV3 fixture does not establish compatibility with every extension.

Store installation is not validated by loading an unpacked fixture. Preserve
upstream install verification, permission prompts and restricted-origin rules.
This guide does not publish or change any extension. The local guidance used for
this section is [Chrome extensions skill](/home/blynx/.agents/skills/chrome-extensions/SKILL.md)
with its service-worker and DevTools references.

## 10. GN, formatting, and resource diagnostics

### GN include/dependency checks

These check the configured graph and source includes, not runtime behavior:

```bash
(
  cd .local/chromium/src
  buildtools/linux64/gn check out/linux-release //browser:appearance_prefs
  buildtools/linux64/gn check out/linux-release //chrome/browser/ui:ui
  buildtools/linux64/gn check out/linux-release //chrome/browser/ui/views/payments:impl
)
```

Run `configure` if a changed BUILD.gn needs its graph regenerated. A normal browser
build also updates its generated build manifest as required.

### Owned C++ formatting check

This prints formatting violations without rewriting source:

```bash
rg --files browser -g '*.cc' -g '*.h' -0 | xargs -0 .local/chromium/src/buildtools/linux64-format/clang-format --dry-run --Werror
```

### JavaScript smoke-harness syntax

```bash
.local/chromium/src/third_party/node/linux/node-linux-x64/bin/node --check tests/runtime_smoke.mjs
```

### Settings CSS lint, without automatic fixes

```bash
.local/chromium/src/third_party/node/linux/node-linux-x64/bin/node .local/chromium/src/third_party/node/node_modules/stylelint/bin/stylelint.mjs --config .local/chromium/src/ui/webui/resources/tools/stylelint.config_base.mjs .local/chromium/src/chrome/browser/resources/settings/appearance_page/appearance_page.css
```

The real browser build also runs the upstream WebUI TypeScript/CSS checks. Lint
alone does not prove that generated resources or UI controls function.

The owned customization GRIT catalog and its adjacent-GRD added-file copy in
patch 0002 must be updated together. Integration verifies their equality. Changing
only one copy is not fixed by repeatedly rebuilding.

## 11. Native debugger and debug builds

### A separate debug output

This can consume significant additional disk, RAM and compile time. Keep release
output intact; do not overwrite its arguments to create a debug build.

```bash
python3 -B scripts/build.py configure --config debug
python3 -B scripts/build.py build --config debug -j 4
```

On this host, select its verified existing sandbox helper for a wrapper launch:

```bash
CHROME_DEVEL_SANDBOX=/opt/google/chrome/chrome-sandbox python3 -B scripts/build.py run --config debug -- --ozone-platform=wayland
```

Debug output is `.local/chromium/src/out/linux-debug`. It needs its own completed
browser build receipt. Do not run a debug launch until that build succeeds.

### Browser-process debugging with GDB (optional)

Requires an installed GDB and usable symbols. First complete the release build
through the wrapper; the direct debugger command below does not apply launcher
readiness checks or desktop identity automatically.

```bash
mkdir -p .local/manual-profiles
BLYNX_GDB_PROFILE="$(mktemp -d "$PWD/.local/manual-profiles/gdb-XXXXXX")"
CHROME_DEVEL_SANDBOX=/opt/google/chrome/chrome-sandbox gdb --args .local/chromium/src/out/linux-release/chrome --user-data-dir="$BLYNX_GDB_PROFILE" --no-first-run --ozone-platform=wayland
```

Inside GDB:

```text
set follow-fork-mode parent
break browser::ShouldShowBlynxOnboarding
run
thread apply all bt
continue
quit
```

This targets the browser process. Optimized release code can limit source-level
stepping; use the separately built debug executable when necessary. Renderer
attachment has additional sandbox/host-policy constraints. Do not disable those
safeguards to make a debugger attach. Do not publish raw dumps or stack variables
without reviewing them for sensitive information.

## 12. Standalone library and sanitizers

CMake builds only the isolated identity/appearance library. It does **not** build
Blynx, PrefService integration, Views or WebUI.

```bash
cmake -S . -B .local/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .local/build/release --parallel 2
ctest --test-dir .local/build/release --output-on-failure
```

Optional Linux ASan/UBSan library run with an available Clang toolchain:

```bash
cmake -S . -B .local/build/asan-guide -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build .local/build/asan-guide --parallel 2
ctest --test-dir .local/build/asan-guide --output-on-failure
```

This is a separate output and does not change the Chromium toolchain or GN
configuration. `clang++`, Ninja and CMake must be available locally. A sanitizer
pass here is not a whole-browser sanitizer result.

## 13. Performance harness

Run when you deliberately want measurements, after completing the browser build
and closing unrelated heavy work. This command measures only Blynx:

```bash
CHROME_DEVEL_SANDBOX=/opt/google/chrome/chrome-sandbox python3 -B benchmarks/run.py --browser blynx="$PWD/.local/chromium/src/out/linux-release/chrome" --configuration blynx=release --build-config blynx="$PWD/.local/chromium/src/out/linux-release/args.gn" --repetitions 5 --profile-mode both --timeout 30
```

The helper path applies to this host; other machines need their own supported
sandbox setup. Raw results are written under `.local/benchmarks/`. To compare
browsers, add another `--browser LABEL=/absolute/path/to/executable` and record
its configuration. The harness uses temporary profiles and a fixed local
headless workload. Summed RSS is not PSS; launch-to-DOM is not desktop first paint.
Read `PERFORMANCE.md` before interpreting results. No speed/memory advantage is
established by smoke tests, and a development link is not a release benchmark.

## 14. Bootstrap and environment checks

### Inspect without downloading or installing

```bash
python3 -B scripts/bootstrap.py --check
python3 -B scripts/bootstrap.py --print-gn-args
python3 -B scripts/bootstrap.py tools --dry-run
python3 -B scripts/bootstrap.py bootstrap --dry-run
python3 -B scripts/bootstrap.py hooks --dry-run
python3 .local/chromium/src/build/install-build-deps.py --quick-check --no-arm --no-chromeos-fonts --no-syms
stat -c '%U %a %n' /opt/google/chrome/chrome-sandbox
```

The dependency quick-check reports broad upstream requirements, including test
and packaging packages; a browser link alone does not establish they all exist.
The installed sandbox helper on this host should be root-owned mode 4755.

### Initial setup only: network/download operations

These fetch substantial dependencies and may run upstream hooks. Do not repeat
them as a routine fix for a dirty, already-integrated checkout.

```bash
python3 -B scripts/bootstrap.py tools --yes
python3 -B scripts/bootstrap.py bootstrap --yes
python3 -B scripts/bootstrap.py hooks --yes
```

Keep the selected pins. Source updates, different toolchains and OS package or
security-policy changes need their own deliberate maintenance work. See
`UPSTREAM.md` and `BUILD_LINUX.md`.

## 15. Windows commands

**Run on a native supported Windows development machine, not this Linux host or
WSL sharing the same checkout. These commands are not Windows validation evidence.**
Read `BUILD_WINDOWS.md` for the pinned VS/SDK requirements. In `cmd.exe`, from the
project root:

```bat
python -B scripts\bootstrap.py --check
python -B scripts\integrate.py --check
python -B scripts\integrate.py
python -B scripts\build.py configure --platform windows
python -B scripts\build.py build --platform windows -j 4
python -B scripts\build.py run --platform windows
python -B -m unittest discover -s tests -v
python -B scripts\build.py test --platform windows --test-target browser:browser_identity_appearance_unittests -j 4
python -B scripts\build.py test --platform windows --test-target browser:appearance_prefs_unittests -j 4
python -B scripts\build.py test --platform windows --test-target browser_tests -j 4 -- --gtest_filter=NativeAppearanceBrowserTest.*:NativeAppearancePolicyBrowserTest.*
```

Native smoke and the Linux-specific Node/GI commands above are not Windows test
commands. Windows icon/installer/COM identity and runtime validation remain work.

## 16. Troubleshooting

| Symptom | Next action |
|---|---|
| Launcher says build is incomplete or inconsistent | Run integration preflight, resolve discrepancies, integrate if needed, then complete `scripts/build.py build`; do not hand-edit the receipt. |
| Overlay or patch conflict | Inspect `git -C .local/chromium/src diff --stat` and the named files; preserve edits and reconcile them before integration. |
| GRIT catalog mismatch | Update both `browser/resources/customization_strings.grdp` and the required catalog copy in patch 0002. |
| CSS/TypeScript lint failure | Read the first specific diagnostic in the build log; fix source, refresh the owning patch, reintegrate, rebuild. |
| Browser-test build is taking a long time despite a filter | The full executable still needs compilation; start with the focused targets or smoke scripts. |
| `No usable sandbox!` | Inspect `BUILD_LINUX.md` and the verified installed helper; rebuilding does not change host sandbox policy. |
| Empty `CHROME_DEVEL_SANDBOX` is refused | Remove the accidental empty override from your launch environment or supply a valid helper; do not use an empty helper to bypass security. |
| Native smoke cannot find display/GI/AT-SPI | Run in the supported desktop session; headless smoke is a separate check. |
| Native accessibility snapshot is stale | Avoid parallel GUI automation. The harness retries transient objects; investigate a persistent timeout. |
| Setup does not automatically appear in an old profile | Expected: use Set up Blynx in Appearance, or a fresh disposable profile to test first run. |
| Appearance returns after a restart | A restored Settings tab is possible; inspect whether the wizard is completed before calling it a setup regression. |
| `blynx://` in DevTools metadata differs from `location` | Virtual display URL and original trusted document origin intentionally differ. |
| Native Wayland logs Vulkan or GCM warnings | Record relevant symptoms and logs; do not disable security or claim a full GPU/network audit. |
| Packaging says Release not ready | Expected: packaging fails closed until the release gates in TODO.md are satisfied. |

Do not use global process-kill commands as test cleanup. Close the windows or
process you explicitly started, and preserve logs before investigating a failure.

## 17. Current validation and remaining coverage

Before this command guide was written, the latest Linux slice passed:

| Check | Recorded result |
|---|---|
| Python tooling | 93 tests |
| Focused PrefService executable | 9 tests |
| Headless browser smoke | 46 checks |
| Combined native Wayland/AT-SPI smoke | 51 checks |
| Existing/completed/future setup fixtures | Passed independent native checks |
| GN UI/payment header checks and completed browser build | Passed |

These are historical results for that slice, not a promise every command above
was executed while writing this guide. Test counts may change as coverage grows.

Still separate: full inherited unit/browser suites; Windows; actual browser-data
and password import through OS file selection; payment flow; real Store install;
full privacy/security/GPU/media/accessibility audits; performance comparisons;
installer identities and signed release packaging. Current smoke opens/cancels
the importer and verifies the available password-import UI; it does not prove a
complete transfer of every source browser's data.

Related project references: `HANDOFF.md`, `TODO.md`, `BUILD_LINUX.md`,
`BUILD_WINDOWS.md`, `EXTENSIONS.md`, `PERFORMANCE.md`, `SECURITY.md`, `UPSTREAM.md`.
