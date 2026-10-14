# Blynx — live handoff

Updated October 13, 2026. Native Chromium development distribution, not a
completed daily-driver release. User explicitly selected the company/product
name **Blynx**. Preserve Chromium/Views/C++23/GN/Clang/libc++, sandbox/isolation,
pins, OS policy, credentials and existing profiles. No Electron/WebView shell.

## Workspace and integration

KDE neon 24.04 x86-64, KDE Wayland, Ryzen 7 5700, 31 GiB RAM. Existing repository
commits are preserved; session changes are unstaged. Never stage `.local/`,
PROMPT.txt, secrets, browser data or crash dumps. No OS security change or package
installation was made. Owned code is `browser/`; four reviewed patches in
`patches/series`; pins in `config/`. Integration installs 24 owned files and records
hashes in `.local/chromium/src/.browser-final-overlay.json`.

Pinned Chromium 154.0.8037.97 / `b510e9d7cd3a2fbd78d0ddc42234103206c5f78d`,
depot_tools `ccecefb58648ac0a416756b0a4591ec8905a7f55`. Source/dependencies:
`.local/chromium/src`; tools `.local/tools/depot_tools`; GN output
`.local/chromium/src/out/linux-release`; native executable remains `chrome`.
Reintegrate with `scripts/integrate.py`; preserve local upstream overlay edits.
Never run two builds in this output concurrently.

## Product behavior and identity

- Blynx product strings, native customization labels, built-in new-tab header and
  application menu entry. New-tab shortcuts open upstream bookmarks/history/
  downloads/extensions/settings; upstream search and most-visited remain.
- Own B icon in selected native Linux/Views resources and the desktop entry.
- Root `./blynx` launcher; `browser-final` remains a compatibility alias.
  User-local `blynx-dev.desktop` installed. Old owned menu entry is hidden but
  redirects pinned legacy shortcuts. No default-browser change.
- Existing development profile `out/linux-release/development-profile`, legacy
  `BrowserFinal` disk roots, `browser_final.appearance_json`, GRIT/internal IDs and
  receipt filenames remain stable. No profile migration or daily-profile access.
- Strict <=4 KiB versioned appearance schema, system/light/dark presets,
  PrefService persistence, policy/private-profile/future-schema write guards.
- Native Views toolbar/dialog: accent, theme, bookmark bar, upstream vertical-tab
  preference. Controls update a dialog-local illustrative preview, not live
  browser chrome. Apply/Cancel use upstream services. Other schema fields remain
  data-only; no workspaces/privacy blocker/custom credential system implemented.

## Blank new-tab diagnosis and launch safeguards

New resources had been generated while the updated executable failed to compile:
`ToolbarButton::SetText` is protected. Replaced it with public `SetHighlight` and
successfully linked the product surface. The user confirmed new tabs now load.

Linux launch now requires a successful-build receipt matching integration, GN
arguments and runtime artifacts. A build invalidates its old receipt and publishes
one atomically after success. This prevents partial-resource/old-binary launch;
it is a consistency check, not package signing or a security audit.

On this host AppArmor restricts unprivileged namespaces for the workspace binary.
The launcher validates and selects the existing root-owned mode-4755
`/opt/google/chrome/chrome-sandbox` when no helper was explicitly configured.
An empty helper setting is refused. Wayland is selected when available; explicit
X11 flags remain supported. No unsafe flag or global policy/environment change.

## Validation

Passed before final rename:
- Product-surface Linux browser link: 305 steps / 5m44s; subsequent polish link
  and final no-op build passed. Logs `.local/logs/product-*-oct13.log`.
- Actual DevTools runtime smoke: 10 checks, no page errors. Branded new-tab header/
  links and repeated tabs, bookmarks route, local JavaScript/navigation, tab
  activation/close, local download contents, local MV3 service-worker/storage
  messaging and disable/re-enable persistence, `chrome://sandbox` Seccomp-BPF.
- Native Wayland/AT-SPI smoke: five checks passed for toolbar/dialog, native Apply
  writing accent, upstream vertical preference persistence, restart restoring
  accent, Cancel preserving saved configuration. A later extra visibility check
  failed because it expected the wrong accessible label (“Collapse vertical tabs”
  vs upstream “Collapse tabs”); corrected for the final rerun.
- 90 Python tooling tests, including launcher and partial-build readiness guards.
- Prior focused Chromium identity/appearance tests and nine PrefService tests,
  Release and ASan/UBSan standalone CMake tests, GN dependency checks and owned
  C++ formatting. Native browser-test objects compiled; full browser_tests not run.

Final Blynx results (October 13):
- Full browser build succeeded: 508 steps / 3m03s, log
  `.local/logs/blynx-brand-build.log`. No browser build remains active.
- All 90 Python tests passed after rename; desktop entries passed
  `desktop-file-validate`; KDE application cache refreshed.
- Chromium identity/appearance executable rebuilt and passed after rename.
- Runtime smoke passed all 10 checks with Blynx branding and no page errors;
  `.local/runtime-smoke/results.json`, screenshot `new-tab.png`.
- Native Wayland smoke passed all six checks, including the actual visible
  vertical strip after Apply, saved preferences, restart and Cancel;
  `.local/native-smoke/results.json`. The corrected upstream label resolves the
  earlier test assertion failure.
- Integration preflight and repository `git diff --check` passed.


Not executed/limitations: full native policy/incognito and inherited regression
suites; Windows build/runtime/COM identity audit; release packages; real Web Store
installation; broad media/GPU/privacy/network audit and performance comparisons.
AT-SPI smoke depends on this host's GI/Wayland accessibility support. Runtime
smoke uses temporary profiles/local fixtures and deletes only its own test data.
The old dump-DOM timeout is superseded by passing DevTools page tests. Earlier
Wayland/Vulkan/GCM/GLib warnings remain diagnostic follow-up, not evidence of a
full platform audit. Upstream dependency quick-check reports missing broad test/
packaging packages; sysroot-backed browser builds work without blanket installs.

Chrome-only benchmark baseline remains in PERFORMANCE.md; no Blynx advantage
has been measured. Packaging deliberately fails closed pending validation.

## Commands and next work

```sh
./blynx
python3 -B scripts/install_launcher.py
python3 -B scripts/integrate.py --check
python3 -B scripts/integrate.py
python3 -B scripts/build.py build -j 4
python3 -B -m unittest discover -s tests -q
python3 -B scripts/runtime_smoke.py
python3 -B scripts/native_smoke.py
python3 -B scripts/build.py test --test-target browser:browser_identity_appearance_unittests -j 4
python3 -B scripts/build.py test --test-target browser:appearance_prefs_unittests -j 4
```

Full browser_tests filters: `NativeAppearanceBrowserTest.*`,
`NativeAppearancePolicyBrowserTest.*`, `BrowserFinalExtensionTest.*`. Building
that executable is substantial. Next implement/test genuine native design-token
consumers, live preview/import-export and tabs/workspaces/sidebar slices, while
expanding browsing/security/extension coverage. See TODO.md for remaining work.
