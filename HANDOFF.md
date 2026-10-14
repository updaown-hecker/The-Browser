# Blynx — live handoff

Updated October 13, 2026. Native Chromium development distribution, not a
completed daily-driver release. User explicitly selected the company/product
name **Blynx**. Preserve Chromium/Views/C++23/GN/Clang/libc++, sandbox/isolation,
pins, OS policy, credentials and existing profiles. No Electron/WebView shell.

## Workspace and integration

KDE neon 24.04 x86-64, KDE Wayland, Ryzen 7 5700, 31 GiB RAM. Existing repository
commits are preserved; session changes are unstaged. Never stage `.local/`,
PROMPT.txt, secrets, browser data or crash dumps. No OS security change or package
installation was made. Owned code is `browser/`; five reviewed patches in
`patches/series`; pins in `config/`. Integration installs 34 owned files and records
hashes in `.local/chromium/src/.browser-final-overlay.json`.

Pinned Chromium 154.0.8037.97 / `b510e9d7cd3a2fbd78d0ddc42234103206c5f78d`,
depot_tools `ccecefb58648ac0a416756b0a4591ec8905a7f55`. Source/dependencies:
`.local/chromium/src`; tools `.local/tools/depot_tools`; GN output
`.local/chromium/src/out/linux-release`; native executable remains `chrome`.
Reintegrate with `scripts/integrate.py`; preserve local upstream overlay edits.
Never run two builds in this output concurrently.

## Product behavior and identity

The October 13 redesign follows the user's supplied eye logo and request for a
more distinct browser. Public product name remains Blynx.

- Designed local new-tab canvas: original eye logo, Aurora/Midnight/Paper
  backgrounds, optional browser shortcuts and upstream search/most-visited.
- Generated transparent taskbar icon plus 16/32/64 native PNG derivatives.
  Source/reference/prompt are documented in `browser/resources/icons/README.md`.
- Settings → Appearance now contains Blynx theme/accent/bookmarks/vertical-tab/
  canvas controls. Apply uses the existing guarded native controller; Cancel
  discards staged changes. The old toolbar popup is removed; new-tab Customize
  opens Settings. Old dialog source remains for isolated tests, not the user flow.
- `blynx://` is a standard navigation alias, canonicalized to `chrome://` before
  upstream policy/extension/WebUI handlers. Successful internal documents display
  a branded virtual URL; actual trusted origins and permissions remain upstream.
  HTTP(S), chrome-extension, chrome-untrusted and error pages are not branded.
- `blynx://webstore` and `chrome://webstore` open the official external Chrome Web
  Store. No external-store identity substitution or install-verifier change.
- Root `./blynx` launcher; `browser-final` remains a compatibility alias. Menu
  entry `blynx-dev.desktop` uses the generated icon; old owned pinned shortcut
  redirects. No default-browser change or profile migration.
- Development profile `out/linux-release/development-profile`, legacy
  `BrowserFinal` disk roots, `browser_final.appearance_json`, GRIT/internal IDs and
  receipt filenames stay stable. New canvas prefs are `blynx.new_tab_style` and
  `blynx.new_tab_links`, per-profile and not syncable.
- Strict <=4 KiB versioned appearance schema and policy/private-profile/future-
  schema guards remain. Only implemented controls are exposed in Settings;
  density/scaling/etc. remain data-only. No workspaces, privacy blocker, sync
  service, password encryption or updater is newly implemented.

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
  `.local/logs/blynx-brand-build.log`. This was the completed rename build, preceding the current redesign.
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

## Redesign build and validation — completed for this slice

The full redesign browser build succeeded in
`.local/logs/blynx-redesign-build5.log` (1076 steps). A final compatibility fix
keeps the upstream `ntp-logo` element hidden in the NTP DOM so inherited
TypeScript tests retain their typed `app.$.logo` surface; the visible Blynx eye
mark remains custom. The final integration was applied and an incremental
12-step browser rebuild succeeded. The current readiness receipt is
`.local/chromium/src/out/linux-release/.browser-final-build.json`; launching
`./blynx` is now guarded by that receipt and is allowed on this host.

The updated runtime and native smoke tests were rerun after that final rebuild:
both passed with 19 DevTools checks, 22 combined native checks, no page errors,
and Seccomp-BPF reported enabled. The prior renamed-build results above remain
historical, but the current redesign results are in the ignored
`.local/runtime-smoke/results.json` and `.local/native-smoke/results.json`.

The broad `unit_tests` target was attempted twice. The focused nine-test
PrefService executable passed; the full target timed out after 10 minutes while
compiling unrelated upstream tests (937 actions completed, no test failure was
reported). Full `browser_tests`, Windows validation, packaging and inherited
security/regression suites remain unexecuted.

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
that executable is substantial. The next session should first retry the full
`unit_tests` target from the existing GN output if broad coverage is desired,
then implement/test genuine native design-token consumers, live preview/import-
export and tabs/workspaces/sidebar slices while expanding browsing/security/
extension coverage. See `TODO.md` for remaining work.
