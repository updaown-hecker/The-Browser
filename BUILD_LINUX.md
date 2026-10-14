# Linux build

Primary host: KDE neon 24.04, x86-64, KDE Wayland. Initial inspection: Ryzen 7
5700 / 16 threads, 31 GiB RAM, 269 GiB free. Allow >=150 GiB headroom and more for
multiple symbol-heavy outputs. Start at `-j 4` on this shared desktop; avoid
exhausting RAM with 16 concurrent compiles/linkers. Do not alter system swap or
kernel controls automatically.

Upstream guide for the pinned release:
https://chromium.googlesource.com/chromium/src/+/154.0.8037.97/docs/linux/build_instructions.md

## Bootstrap (once, from repository root)

```sh
python3 scripts/bootstrap.py tools --dry-run
python3 scripts/bootstrap.py tools --yes
python3 scripts/bootstrap.py bootstrap --dry-run
python3 scripts/bootstrap.py bootstrap --yes
python3 .local/chromium/src/build/install-build-deps.py --quick-check --no-arm --no-chromeos-fonts --no-syms
```

`--quick-check` is read-only and reports broad upstream requirements. Review the
script and necessary package list before installation. It currently reports
missing dev/test/packaging packages. Linux GN generation and isolated GN tests
nevertheless succeed using Chromium's fetched sysroot; that is not proof that
all runtime/test/packaging dependencies exist. If installation is needed, run
upstream's supported dependency installer with appropriate system authorization,
without placing any password in a command or repository file.

```sh
python3 scripts/bootstrap.py hooks --yes
python3 scripts/bootstrap.py --check
python3 scripts/integrate.py --check
python3 scripts/integrate.py
python3 scripts/build.py configure
python3 scripts/build.py build -j 4
python3 scripts/build.py run -- --ozone-platform=wayland
# For an X11 session:
python3 scripts/build.py run -- --ozone-platform=x11
```

The run wrapper always supplies an isolated `out/linux-release/development-profile`
unless explicitly overridden. Do not point development binaries at daily profiles.
Native output remains `out/linux-release/chrome`. The Linux link and targeted
runtime/native UI smoke checks pass; no production package is claimed. The latest
incremental browser build regenerated the successful-build receipt after the
final new-tab compatibility fix. See `HANDOFF.md` for precise coverage and the
one broad-test timeout.

## Branded development launcher

The launcher checks a successful-build receipt against the current integration,
GN arguments and runtime artifacts. If an interrupted build leaves mismatched
resources, finish `python3 -B scripts/build.py build -j 4` before launching.

After a successful build, run `./blynx` from this checkout, or choose
**Blynx** in KDE's application menu. `python3 -B scripts/install_launcher.py`
installs the user-local menu entry with the owned icon; it does not set a default
browser, install a production package or modify system policy. The development
profile remains `out/linux-release/development-profile`.

The launcher uses the existing root-owned installed Chrome sandbox helper when
no explicit `CHROME_DEVEL_SANDBOX` is configured. Empty helper settings are
rejected. Wayland is selected when the session advertises it; an explicit
`--ozone-platform=x11` is preserved. On other hosts, sandbox setup remains a
prerequisite if unprivileged namespaces are restricted.

## Checks

```sh
python3 -B -m unittest discover -s tests -v
python3 scripts/build.py test --test-target browser:browser_identity_appearance_unittests -j 4
python3 scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4
python3 scripts/build.py test --test-target unit_tests -j 4 -- --gtest_filter='AppearancePrefStoreTest.*'
python3 scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='NativeAppearanceBrowserTest.*:NativeAppearancePolicyBrowserTest.*'
python3 scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='BrowserFinalExtensionTest.*'
```

Additional actual-browser smoke checks (disposable profiles, local fixtures):

```sh
python3 -B scripts/runtime_smoke.py
python3 -B scripts/native_smoke.py
```

The runtime smoke uses the pinned Chromium Node runtime and a loopback-only,
random DevTools port to check the built-in new-tab page, local navigation/tabs,
downloads, a local MV3 extension, and `chrome://sandbox`. The native smoke uses
this host's GI/AT-SPI accessibility library and Wayland display to operate the
actual Settings page, save accent/tab/canvas choices, reject invalid colors and
verify Cancel and page reload preserve the saved configuration. The native run
also inspects the visible vertical strip and branded address bar through AT-SPI. Logs/results/screenshots stay ignored under `.local/`. These
checks are narrower than full inherited or native in-process browser tests.

The last two browser-test commands above need a substantial Chromium build; UI browser tests also need a
usable display. The in-process tests must not be replaced with manifest parsing
or compilation-only claims. Full inherited browser/security regression coverage
is a release gate, not just the custom gtest filter.

The targeted identity/appearance executable and nine PrefService tests passed in
the current checkout. A full `unit_tests` target was started once and timed out
after 10 minutes while compiling unrelated upstream tests; it did not report a
test failure. Retry it from the existing output when broad coverage is useful.

Standalone library fallback (not the browser):
```sh
cmake -S . -B .local/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .local/build/release --parallel 2
ctest --test-dir .local/build/release --output-on-failure
```

## Paths, configuration and diagnostics

Default `.local/chromium/src`; output `out/linux-release` or `out/linux-debug`.
`--source-root`, `--build-root`, `CHROMIUM_SOURCE_ROOT`, and `CHROMIUM_BUILD_ROOT`
support deliberate relocation. Output must stay inside the upstream source.
Local depot_tools is found automatically; no global PATH edits are needed.

Release means `is_debug=false`, non-component, supported upstream optimization,
limited debug symbols, Chromium media branding, no proprietary codecs. It is
not yet an official signed/PGO release configuration. Existing different args
are not silently replaced; choose a separate output or `--overwrite-args`.

Ubuntu-derived AppArmor/user-namespace restrictions can prevent custom builds
from starting securely. Diagnose the denial and configure a reviewed package
policy; never use `--no-sandbox`, disable system security globally, or run as root.
Check `chrome://sandbox`, `chrome://gpu`, `chrome://version` after first launch.

### This host: “No usable sandbox!” after a successful build

On October 12, 2026, `kernel.apparmor_restrict_unprivileged_userns=1` and the
system Chrome AppArmor exception covered `/opt/google/chrome/chrome`, not this
workspace binary. The default run aborted before UI initialization. Rebuilding
Chromium does not resolve this host sandbox setup.

The pinned upstream `docs/security/apparmor-userns-restrictions.md` documents
using an existing installed Chrome SUID helper for development builds. This host
has `/opt/google/chrome/chrome-sandbox`, owned by root with mode 4755. Use a
command-scoped environment variable (no shell startup or OS policy changes):

```sh
CHROME_DEVEL_SANDBOX=/opt/google/chrome/chrome-sandbox python3 -B scripts/build.py run -- --ozone-platform=wayland
```

This uses the installed sandbox helper, not Chrome profiles or credentials.
Chromium validates the helper and fails if its setup/API is unsuitable. Do not
set the variable to an empty string; upstream treats that as disabling the SUID
sandbox. If the helper is absent on another host, review the pinned upstream
sandbox installation instructions or an exact-path AppArmor policy with the
administrator before making a system change.

Validation: the browser remained running with Wayland, renderer namespace
separation, renderer `NoNewPrivs=1` / `Seccomp=2`, and GPU `Seccomp=2` observed in
`/proc`. This is limited process-level evidence, not a full sandbox audit or
visual UI test. The initial headless `--dump-dom` smoke test timed out after 40
seconds without returning DOM; its test browser was stopped. Wayland logs include
an incompatibility warning for Vulkan and upstream GCM registration errors.
The Wayland launch later exited with code 0; GLib assertions were also logged.
Later DevTools-based runtime smoke checks successfully exercised actual pages
and recorded `chrome://sandbox` Seccomp-BPF status; the earlier dump-DOM timeout
is not the current browsing result.
Logs: `.local/logs/wayland-launch.log` and
`.local/logs/sandbox-launch-smoke.log`.

Packaging currently refuses with “Release not ready.” Do not distribute raw
unvalidated upstream packages. Required gates are listed in `TODO.md`.
