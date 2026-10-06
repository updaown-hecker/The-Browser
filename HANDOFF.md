# Browser Final — live handoff

## Goal and architecture

Serious native Chromium-derived daily-browser project; **not a completed browser
release**. Native Chromium/Views/C++23/GN/Clang/libc++, Linux first and native Windows
architecture from the same source. No Electron, generic WebView shell, localhost
app server, insecure flags, proprietary codecs/CDM or borrowed credentials.
Owned overlay `browser/`, reviewed `patches/series`, exact `config/upstream.json`.

## Current environment / preserved work

Original directory contained only `PROMPT.txt`, left intact and ignored. Root Git
repo initialized, no commits/staging. KDE neon 24.04 x86-64, KDE Wayland, Ryzen 7
5700/16 threads, 31 GiB RAM, initially 269 GiB free. Installed Chrome
154.0.8037.97 and Brave 154.1.96.61. Missing initial Chromium/GN/Clang/depot_tools.
Do not store or reuse supplied administrative secrets in code/logs/docs. No OS
package installation or global environment/security setting change was performed.

## Completed source/bootstrap

- Chromium 154.0.8037.97, hash `b510e9d7cd3a2fbd78d0ddc42234103206c5f78d`.
- depot_tools `ccecefb58648ac0a416756b0a4591ec8905a7f55`.
- Shallow source clone, unmanaged `.gclient` Linux dependency sync and upstream
  hooks completed in `.local/chromium/src`; pinned tools `.local/tools/depot_tools`.
- Linux release GN generation succeeded: 34,285 targets / 5,055 files after the
  focused preference-test executable was added.
- Both patches applied and 18 owned overlay files installed. Integration receipt
  `.local/chromium/src/.browser-final-overlay.json` records hashes and source pin.
- Product strings/profile roots/Windows basic app/ProgID names patched; icon SVG
  exists but is not substituted into native platform resources. Native development
  binary naming remains upstream `chrome`/`chrome.exe`. Packaging deliberately
  fails closed; Windows COM/Active Setup identity audit remains mandatory.

## Implemented custom code

- `browser/identity`: product identifiers, safe portable profile path components.
- `browser/appearance/appearance_preset`: strict <=4 KiB flat versioned JSON,
  enums/types/colors/numeric/UTF-8 checks, duplicate/unknown-key rejection,
  canonical export/import and three built-in presets. No secrets/remote content.
- `appearance_pref_store`: registered per-profile PrefService JSON, nonmutating
  invalid fallback, unsupported-version preservation and write refusal.
- Native Views toolbar/dialog/controller: saved preset, theme, RGB accent,
  bookmark bar, illustrative local preview, Apply/Cancel and policy/private-mode
  guards. Unsupported appearance fields are explicitly data-only, not fake working
  UI controls. GRIT labels. **Runtime not yet verified.**
- Python pinned bootstrap/build/integrate with dry runs, local tool discovery,
  source/overlay/policy safeguards and sandbox-switch rejection.
- Local Linux headless benchmark runner with scoped subprocess cleanup, raw
  samples, repetition, signal cancellation and incremental atomic checkpoints.
- MV3 unpacked service-worker/storage/disable-enable browser test source added
  and compiled. This is not yet executed extension-functionality evidence.

## Actual checks so far

Passed:
- `python3 -B -m unittest discover -s tests -q`: **83 tests passed** (tooling,
  malformed receipt guards, real tiny Git fixture integration and benchmark
  parsing/process/checkpoint behavior).
- Standalone Release CMake/CTest identity/appearance test executable, 1/1.
- Standalone ASan/UBSan + warnings-as-errors/no-exceptions/no-RTTI CMake/CTest, 1/1.
- Chromium release GN build and execution of
  `browser:browser_identity_appearance_unittests` using upstream Clang/libc++.
  Initial unsafe-buffer errors were fixed using bounded containers/iterators and
  C++23 `std::to_address`, **not warning suppressions**; rerun passed.
- `gn check out/linux-release '//browser:*'`: header dependency check passed.
- Patch preflight/install/idempotence and upstream `git diff --check` passed.
- All native dialog/controller/pref adapter and custom browser-test objects plus
  upstream toolbar/pref-registration hook objects compiled under Chromium Clang.
  Fixed a second bounded-array warning, Widget raw_ptr iteration mismatch and
  icon namespace mismatch, then reran successfully. No suppression added.
- `browser:appearance_prefs_unittests`: **9/9 executed and passed**, including
  disk-backed preference recreation, future/malformed data and policy/read-only
  refusal. Build 663 steps / ~3 minutes; tests completed successfully.
- Pinned upstream clang-format / GN formatting checks passed after normalization;
  all current owned C++ files including the new extension source passed final
  formatting validation. Release and sanitizer CMake/CTest reruns passed too.

Not passed/not run:
- Upstream OS dependency quick-check exits 1, reports broad missing dev/test/
  packaging dependencies. Sysroot-backed GN/config tests still work. Review the
  required list in terminal instead of silently installing everything.
- Full browser link/launch, webpages, tabs, GPU/sandbox, extension runtime and
  native appearance/extension in-process browser tests are **not verified**.
- Windows build/runtime/installer and all release packaging are unverified.

## Active full release build (do not start another in this output)

Targeted builds have finished and are successful. A bounded full `chrome` build
is now running as background shell `sh_10f20732f001romsGsXtg1kYFI`:

```sh
timeout --signal=INT --kill-after=30s 600s python3 -B scripts/build.py build -j 4
```

Log `.local/logs/browser-release-build.log`. A timeout means incomplete validation,
not a successful browser link or a diagnosed compiler defect. The 10-minute bound
prevents an unattended unlimited build; artifacts are incremental and reusable.
Wait for completion notification, inspect only relevant errors, and update this
section before claiming a finished session. Other logs:
`native-customization-build.log`, `upstream-hooks-build.log`,
`appearance-prefs-tests.log`, `extension-tests-compile.log` under `.local/logs`.

## Benchmark evidence

Chrome-only 5 empty-profile + 5 warm-profile trials completed:
`.local/benchmarks/run-k4b5f136/results.json`. Median launch-to-observed-valid-local
DOM: 304 ms empty profile, 273 ms warm. See `PERFORMANCE.md` and raw caveats: this
is headless, not cold OS-cache/desktop startup, and RSS is not PSS/idle RAM.
No Browser Final comparison exists. Earlier Chrome+Brave run was externally
interrupted while Brave hung; only its isolated leftover processes were stopped.
Runner signal/checkpoint behavior was then hardened and tested. Do not reuse that
incomplete run as performance evidence.

## Exact normal commands / next actions

From repository root, after the active build finishes:

```sh
python3 -B scripts/bootstrap.py --check
python3 -B scripts/integrate.py --check
python3 -B scripts/integrate.py  # after owned source changes
python3 -B scripts/build.py configure
python3 -B scripts/build.py test --test-target browser:browser_identity_appearance_unittests -j 4
python3 -B scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4
python3 -B scripts/build.py build -j 4
python3 -B scripts/build.py run -- --ozone-platform=wayland
python3 -B scripts/build.py test --test-target unit_tests -j 4 -- --gtest_filter='AppearancePrefStoreTest.*'
python3 -B scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='NativeAppearanceBrowserTest.*:NativeAppearancePolicyBrowserTest.*'
python3 -B scripts/build.py test --test-target browser_tests -j 4 -- --gtest_filter='BrowserFinalExtensionTest.*'
```

Highest value: finish the full release link/launch first
branded native browser, execute custom persistence/policy tests and verify real
browsing/extension/sandbox/GPU behavior. Then audit inherited services, implement
actual design-token consumers/live preview/import-export, and proceed with native
tabs/workspaces/sidebar/privacy vertical slices. Full roadmap is `TODO.md`.

Do not claim inherited systems work just because upstream source is present.
All required docs exist; keep them synchronized with further results.
