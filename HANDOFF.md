# Blynx — live handoff

Updated October 17, 2026. Native Chromium development distribution, not a
completed daily-driver release. User explicitly selected the company/product
name **Blynx**. Preserve Chromium/Views/C++23/GN/Clang/libc++, sandbox/isolation,
pins, OS policy, credentials and existing profiles. No Electron/WebView shell.

## Workspace and integration

KDE neon 24.04 x86-64, KDE Wayland, Ryzen 7 5700, 31 GiB RAM. Existing repository
commits are preserved; session changes are unstaged. Never stage `.local/`,
PROMPT.txt, secrets, browser data or crash dumps. No OS security change or package
installation was made. Owned code is `browser/`; seven reviewed patches in
`patches/series`; pins in `config/`. Integration installs 75 owned files and records
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
- Settings-local live illustration and strict preset file import/export now work;
  import stages until Apply, export reads saved data, and Cancel invalidates late reads.
  Direct private Settings loads safely and refuses Blynx preset writes/export.
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
then implement/test genuine native design-token consumers and tabs/workspaces/
sidebar slices while expanding browsing/security/extension coverage. Settings-local
preview and preset file import/export are now implemented and tested below. See `TODO.md` for remaining work.


## October 14 continuation — Settings preview and functional coverage

Read all repository Markdown documentation; no project `.mb` files were found.
Before implementation, reran 90 Python tests, the existing 19-check DevTools
smoke and 22-check native Wayland smoke: all passed. Independently launched an
isolated browser and verified four additional scenarios: Back/Forward, bookmark
create/search/reload/delete, the branded history route and regular/private-context
site-storage isolation. Evidence: `.local/manual-oct14/results.json` and
`.local/logs/session-oct14-manual.log`. These are disposable fixture tests, not
personal-profile interaction or a full incognito policy audit.

Implemented a Settings-local illustrative preview in the existing fifth patch.
It reflects staged theme, RGB accent, vertical tabs, bookmark visibility, canvas
and shortcut visibility. No preferences or ThemeService state change while
editing; Apply remains the write operation and Cancel restores saved choices.
Invalid accent text uses a fixed safe preview color. The preview is an
illustration, not an exact native ColorProvider palette or whole-window preview.
Reused existing GRIT strings and corrected the Settings emblem to the upstream
`chrome://theme/current-channel-logo` resource route. No pin, toolchain, OS
policy, credentials or user profile changes. Five reviewed patches remain;
all integration was recorded using `scripts/integrate.py`.

The first build failed only on Chromium CSS formatting checks; corrected with
upstream stylelint. The second build succeeded in 13 actions / 53.64 seconds,
including TypeScript/CSS lint, Settings resources, C++ localization provider and
browser link. A subsequent no-op build succeeded after patch whitespace cleanup.
The final logo-resource and dark-mode caption-contrast build succeeded in
11 actions / 19.08 seconds (`.local/logs/session-oct14-preview-contrast-build.log`).
The successful-build receipt was checked against the current overlay, GN args and
runtime artifacts after that build.

Final verification passed: 25 DevTools checks, 28 combined native Wayland/AT-SPI
checks, no JavaScript page errors, and Seccomp-BPF reported enabled. This includes
preview logo loading, staged edits leaving saved state unchanged, safe invalid-
color fallback, Cancel, Apply, the existing browsing/download/MV3 checks and the
four added browsing/isolation scenarios. Screenshots were visually inspected in
both modes. A later source review found the attempted caption-color replacement
had missed the formatted CSS line; the follow-on pass below corrects it and adds
a computed-color assertion.
Reports: `.local/runtime-smoke/results.json`, `.local/native-smoke/results.json`.
Logs: `.local/logs/session-oct14-runtime-verified.log` and
`.local/logs/session-oct14-native-verified.log`. All 90 tooling tests passed again
(`.local/logs/session-oct14-tooling-final.log`); JavaScript syntax, integration
preflight and repository whitespace checks passed. AT-SPI logged a denied cache
request to an unrelated sandboxed desktop application; the Blynx checks passed.

Not executed this session: full `unit_tests`/`browser_tests`, Windows, Web Store
installation, restart/session recovery, broad security/privacy/media/performance
suites or packaging. Earlier broad-unit build timeout remains unresolved. The
new private-context check establishes local-storage separation only. All logs,
profiles and screenshots remain ignored; no release/performance claim is made.


## October 14 follow-on — preset files and restart smoke

Continued from HANDOFF.md / TODO.md after the completed preview slice. Added
Settings Import preset / Export saved preset. Import invokes Chromium's file
chooser, checks 1–4096 bytes before reading, decodes UTF-8 without replacement,
then calls the existing native strict schema parser for canonical data. It stages
theme/accent/bookmark choices until Apply, preserving separately staged tab-layout
and canvas choices. Cancel or a later edit invalidates an asynchronous file read.
Export uses saved native canonical data and Chromium's normal download service;
it refuses invalid/unsupported storage or unavailable/read-only/managed access.
No WebUI-supplied file path reaches native code. Schema v1 and upstream pins are
unchanged; unimplemented token fields remain compatibility data.

Both owned GRIT source and the required adjacent-GRD catalog in patch 0002 were
updated. Integration now checks that these copies match. First build failed on
two unnecessary TypeScript assertions; the second found the stale patched GRIT
copy. Both were corrected without overwriting unrelated upstream changes.
The full shared-string-consumer build succeeded: 1060 actions / 26m18.42s,
`.local/logs/session-oct14-presets-build3.log`. A final About/menu identity and
caption resource pass succeeded: 241 actions / 29.29s,
`.local/logs/session-oct14-presets-about-build.log`. About now says Blynx while
retaining upstream source/license/copyright attribution. The preview caption's
primary foreground was verified by computed-style assertion and visual inspection.

Tooling checks: 93 tests passed, including catalog consistency and smoke refusal
of an incomplete build / stale prior report. The existing focused Chromium
identity/appearance executable and all nine PrefService tests were run and passed;
these binaries' library sources are unchanged. The smoke runner now checks the
current integration/build receipt, clears stale results, and adds an actual clean
process restart to check saved appearance/canvas prefs and extension storage.
File tests cover chooser dispatch (intercepted to select owned fixtures), canonical
saved-preset export, valid staging/Apply/Cancel/round-trip, eight invalid files,
a delayed-read cancellation race and private Settings guards. The first expanded runtime run found a real upstream Settings crash when opened
in a private DevTools browser context: `isClearPrimaryAccountAllowed` dereferenced
an unavailable ChromeSigninClient. Added an off-the-record short-circuit so that
private profiles do not obtain or query this service. Rebuild and browser link
passed: 2 actions / 29.21s (`.local/logs/session-oct14-presets-private-fix-build.log`).
Crash evidence is preserved under `.local/logs/session-oct14-private-settings-crash.log`
and `session-oct14-private-settings-failure.json`. DevTools disconnects now reject
pending commands promptly; context cleanup preserves the original error.

Final actual-browser results: **38 runtime checks and 41 combined native checks
passed**, no JavaScript page errors, Seccomp-BPF enabled. Both modes perform a
clean SIGINT shutdown and a second browser process using the same disposable
profile. Saved appearance/tab/canvas settings, rendered canvas, site storage,
bookmarks and MV3 storage survived that restart. Private Settings now loads,
reports non-editable and refuses validate/export/apply through native guards.
All preset file tests above passed. Chooser dispatch is intercepted to supply
owned fixtures; OS-native portal interaction remains untested.

Final logs: `.local/logs/session-oct14-presets-runtime2.log` and
`.local/logs/session-oct14-presets-native2.log`. Reports/screenshots are under
`.local/runtime-smoke/` and `.local/native-smoke/`, including `settings-import.png`
and `about.png`; native screenshots were visually inspected. All **93 Python
checks** passed again (`.local/logs/session-oct14-presets-tooling-verified.log`).
Owned C++ clang-format, JavaScript syntax, integration preflight, repository
whitespace and the current completed-build receipt also passed verification.

An independent fresh-profile HTTPS check loaded `https://example.com/` with
normal certificate checks and a secure context, reporting QUIC and no page errors.
The initial test's overly specific body-text assertion was corrected; this was
not a certificate/network failure. Evidence: `.local/manual-https-oct14/results.json`
and `.local/logs/session-oct14-https-verified.log`. This is one real HTTPS page,
not a TLS/network audit or benchmark.

No personal profiles, credentials, OS policy or toolchain/pin changes. Logs and
fixtures stay under ignored `.local/`. Full inherited browser/security suites,
platform-native file-dialog/portal behavior, Windows, Web Store installation,
release packaging, workspaces, native token consumers and performance/privacy
work remain outstanding. Focused restart checks do not establish full tab/session
recovery or renderer-crash recovery.


## October 14 — first-run setup and broader identity pass

Current user scope adds browser-owned Google/Chrome names, first-run data-transfer
onboarding, then theme-appropriate Blynx icon replacements. Continued after passing
93 tooling tests and the baseline runtime smoke. Read the user-requested local
modern-web-guidance skill; searched onboarding/accessibility/icon guidance and
retrieved `accessibility`. Existing Chromium buttons, explicit control labels,
programmatically focusable headings and decorative-logo semantics are retained.

Implemented in the same five patches / 36-file owned overlay:
- Profile-local `blynx.onboarding_step`, bounded actions and native first-window
  presentation. New regular profiles start setup; old default-state profiles do
  not. Incomplete setup resumes once per profile/process; completion suppresses
  it. Private/guest/system, managed and unknown future states cannot write.
- Existing trusted Settings Appearance hosts Welcome → optional transfer →
  personalization. Transfer navigates to Chromium's `/importData`, preserving its
  source detection, data-type selection and explicit import confirmation. No
  migration file parser or custom password handling. Apply still controls saving
  appearance; setup completion only stores progress and opens a new tab.
- Browser-owned GRIT product references use Blynx; shared WebUI and native product
  glyphs use matching monochrome eye geometry with caller-supplied theme colors.
  The legacy owned SVG also uses the eye. Existing Linux taskbar PNGs are reused.
- Actual Google providers, external Chrome Web Store, legal/source attribution,
  source browser names, OS names and compatibility identifiers remain truthful.
  No external DOM injection, store install bypass, separate Blynx store/backend,
  toolchain/pin/OS-policy change or personal-profile migration.

The initial build found CSS formatting issues; corrected with upstream stylelint.
The next build found Chromium click-handler naming requirements; corrected the
five bindings. All 93 Python tests, JS syntax, GRIT XML parse and the GN UI header
check pass. The completed broad build passed in 1455 actions / 12m54.75s at -j6
(`.local/logs/blynx-onboarding-branding-build4.log`). The earlier -j4 build was
intentionally interrupted after 753 successful actions to consolidate the final
sources and resume at -j6; it reported zero failed compilation steps. Final
payment-footer/transfer-link polish passed in 244 actions / 44.18s
(`.local/logs/blynx-onboarding-branding-build5.log`). The browser build receipt is
current; integration reports no pending overlay writes.

Final expanded isolated smoke passed: 46 runtime checks and 51 combined native
Wayland/AT-SPI checks, with no JavaScript page errors and Seccomp-BPF enabled.
Coverage includes setup via keyboard/heading focus, real importer open/cancel,
password CSV importer / extension-manager links, Blynx labels and theme-colored
glyph, action bounds/private write refusal, automatic native first-run/resume,
clean restart progress and completion with the wizard hidden. The existing
browsing, bookmark, download, appearance/preset and MV3 storage checks also pass.
Logs: `.local/logs/blynx-onboarding-runtime-final.log` and
`.local/logs/blynx-onboarding-native-final.log`; reports/screenshots remain in
`.local/runtime-smoke/` and `.local/native-smoke/`. Setup and importer screenshots
were visually inspected.

The first transfer-link assertion expected a chrome:// DevTools target URL;
actual target metadata correctly reports the branded blynx:// virtual URL.
The test now uses real pointer input and normalizes only that display alias,
while still checking the original trusted document origin. Native restart can
restore an Appearance tab through upstream session behavior; the completion
check now verifies saved step 4 and a hidden wizard, rather than forbidding a
restored Settings tab. A separate cold native profile at step 4 verifies no
automatic setup. Native accessibility snapshots now retry stale GLib objects;
required address/tab-layout assertions still time out on real failures.

Independent native fixtures passed for existing step 0, completed step 4 and
future step 999, with no automatic setup and no preference changes after clean
shutdown (`.local/manual-onboarding/results.json`). All nine real PrefService
tests passed (`.local/logs/blynx-onboarding-prefs-tests.log`), and all 93 Python
tooling tests passed again. GN UI and payment-target header checks, JS syntax,
GRIT XML parsing, integration preflight and repository whitespace checks pass.

The transfer step also links to the existing password CSV importer and extension
manager in new tabs. On Linux the pinned direct profile importer detects Firefox;
other browsers can provide bookmarks HTML and password CSV. Extensions require
reinstallation, and unsupported history/session data is not automatically moved.
The payment footer now uses the Blynx eye with native icon theme colors, replacing
its inherited Chromium wordmark; no payment-provider artwork is changed.

Not executed: end-to-end browser-data/password import through OS file selection
or real source-profile migration; payment-request flow; Windows build/icons/
installer audit; localization review; full inherited policy/incognito/security/
media/privacy suites; real Web Store installation or packaging. No release or
performance claim is made.


## October 14 — organized debug/test command guide

Added `DEBUG_AND_TEST_COMMANDS.md` at the user's request and linked it from README.
It consolidates supported CLI commands, their scope and prerequisites, disposable
profile workflows, result paths, debugging and troubleshooting. Historical pass
counts are clearly separate from commands merely documented. No browser sources,
pins, build arguments, user profiles or extension manifests changed in this slice.
Consulted the local Chrome extensions skill plus service-worker/DevTools references
for the extension inspection notes; no extension publication was performed.
Documentation verification: Bash block syntax, Python block syntax, CLI help/dry
runs and repository whitespace were checked. Browser build/runtime suites were
not rerun for this documentation-only change.


## October 15 — Ctrl+Space native Blynx search

Read all 14 repository Markdown files. Continued the pending command-palette /
tab-search work. The user then explicitly replaced the initial toolbar-button
concept with a centered, clean Ctrl+Space search popup. The final toolbar has no
Blynx search button. The original button/form prototype and its failing expanded
smoke runs are superseded by the final implementation and results below.

Current behavior:
- Browser-local Ctrl+Space registration/handling in BrowserView; normal windows
  only, including private windows. Renderer and top-omnibox focus are supported.
  No desktop/global hotkey, OS input setting, toolchain or pin change.
- Centered native Views popup with large search field, up to seven visible result
  rows, tonal selection, native theme colors, adaptive height, arrow keys, Enter,
  Escape, Ctrl+Space toggle and row clicks. No Go/Cancel form or results combobox.
- Tool search: top address-bar focus, new tab, history, downloads, bookmarks,
  settings, tab layout, Developer Tools, Task Manager, extensions, saved passwords.
  Uses upstream command availability and strips native menu mnemonic markup.
- Current-window title/host tab search with weak handles and live index lookup.
  A stale tab refreshes without activating a different tab or silently searching.
- Explicit address/web-query result delegates to the actual LocationBar's
  OmniboxEditModel CanPasteAndGo/PasteAndGo classifier and configured provider.
  No separate parser, search backend or full omnibox autocomplete clone. The
  Focus address bar tool hands control to the real omnibox.
- Accepted actions close the native popup synchronously before posting work with
  weak references and fresh access/index checks. Private/guest and controlled
  tab-layout writes are refused. Escape queues no action; IME composition is left
  to the native textfield instead of submitting incomplete composition.

Owned source: `browser/ui/blynx_command_center.{h,cc}`. Patch 0002 owns the toolbar
method/header and GRIT wiring; patch 0005 adds the BrowserView shortcut. Same five
patches, 38-file overlay and Chromium/depot_tools pins. Reintegrated through
`scripts/integrate.py`; no personal profile, credentials, OS policy or toolchain
changes. Consulted both current local development skills; this is native C++
Views and Python/Node testing, so no client-side web guide or extension-publishing
workflow applied. No new extension code or public/native renderer bridge.

Build history and fixes:
- Initial command-center build deliberately stopped after 805 completed actions,
  zero compile failures, to consolidate sources; resumed browser link passed.
- Final requested shortcut/popup build passed in 276 actions / 3m11.02s
  (`.local/logs/blynx-search-build1.log`). Tool/size polish passed in 3 actions /
  21.09s (`blynx-search-build2.log`).
- Actual second-monitor geometry check found incorrect screen coordinates passed
  to a child-widget positioning API. Replaced this with Views CenterWindow, which
  understands the native parent/display transforms. Fix passed in 3 actions /
  23.79s (`blynx-search-build3.log`). The final native IME guard rebuild passed in
  3 actions / 21.61s (`blynx-search-build4.log`). Current build receipt is valid.
- Earlier prototype checks exposed unsupported AT-SPI text setters, ambiguous
  global control names, async dismissal and the upstream Downloads bubble/page
  distinction. Final tests use scoped X11 keyboard events and popup-local control
  lookup, retaining actual upstream Downloads behavior. Accessibility traversal
  is breadth-first so large page trees do not hide native controls at its cap.
  An uncached-accessibility experiment was stopped after hanging; only its own
  test process/fixture was stopped, without OS/accessibility policy changes.

Validation on the final slice:
- 96 Python tooling tests passed, including three keyboard-driver refusal tests:
  unrelated PID, ambiguous owned windows and unsupported non-ASCII fixture input.
  Log `.local/logs/blynx-search-tooling-final.log`.
- GN UI header/dependency check passed (`.local/logs/blynx-search-gn.log`).
- Headless runtime smoke passed 46 checks with no page errors; log
  `.local/logs/blynx-search-runtime-final.log`, report `.local/runtime-smoke/results.json`.
- Expanded X11/XWayland smoke passed 59 checks with no page errors, including the
  real shortcut from page and address-bar focus, URL classification, tool Enter,
  tab activation, Escape/reopen, tab-layout round-trip and stale-tab refusal.
  Log `.local/logs/blynx-search-native-x11-final.log`; report
  `.local/native-smoke-x11/results.json`.
- Wayland native smoke passed 52 checks with no page errors, including existing
  browsing/Settings/MV3/restart/sandbox coverage and removal of the toolbar entry.
  Detailed popup keyboard synthesis is X11-only on this host; do not call it a
  tested Wayland shortcut. Log `.local/logs/blynx-search-native-wayland-final.log`;
  report `.local/native-smoke/results.json`.
- Eight isolated X11 visual/manual checks passed on the final build. Popup
  geometry on monitor two is `[2251,425,622,546]` inside browser
  `[1930,10,1265,1376]`, within one pixel horizontally and exact vertically at the
  window center. Down+Enter selected the second tool. Plain text generated the
  configured provider's search URL; provider page content is not asserted. The
  popup toggled closed with Ctrl+Space. Private search excluded regular tabs,
  found its own private tab and omitted layout writes. Clean shutdown passed.
  Evidence `.local/blynx-search-visual/results.json`, log
  `.local/logs/blynx-search-visual2.log`. No personal provider account was used.
- Default/filtered/keyboard-selected screenshots were captured and visually
  inspected. Capture initially preceded native paint; a repaint delay fixes that.
  One capture briefly overlapped a keyboard smoke; a final sequential capture
  avoids shared-focus interference. Images stay under `.local/blynx-search-visual/`.

Still not executed: full inherited unit/browser/policy/security suites, Windows,
OS-native importer/portal, real Web Store installation, separate pointer-click
coverage of the new rows, broad IME/accessibility /
localization, GPU/media/privacy/network/performance audits and release packaging.
No release or speed/privacy advantage claim. Existing Wayland/Mesa/GCM/GLib logs
and an AT-SPI cache denial for an unrelated sandboxed desktop app are not resolved
platform audits. Full omnibox suggestions, cross-window search, workspaces and
native appearance token consumers remain TODOs. Restart the development browser
to load the new binary, then press Ctrl+Space.

### October 15 follow-up — search dismissal and placement regression

User reported off-center/unfinished presentation, inability to click outside to
close, and a grey scrim persisting after Enter. The previous tests observed popup
visibility but did not test pointer input after dismissal. Those checks were
insufficient to establish correct modal teardown on the user's session.

- Replaced window-modal DialogDelegate with the upstream BubbleDialogDelegate,
  anchored to the browser root view. FLOAT framing has no arrow; overridden bubble
  bounds center within the actual browser window and inherit anchor move/resize
  notifications. Native bubble placement replaces CenterWindow on a modal child.
- No window modality or dimming scrim is installed. Upstream close-on-deactivate
  dismisses on outside clicks/window switching. Preferred client-owned CreateBubble
  plus a synchronous close callback resets the Widget before an accepted action
  posts. Escape, Ctrl+Space and native row activation share disposal. Search
  callbacks/controllers/raw view references are detached before Widget disposal.
- Compact 48px input, 36px rows, smaller gaps/captions, 20px rounded frame, and width
  selected from browser size. Native theme colors/contrast remain upstream-owned.
- Added real pointer coverage to X11 smoke: click the top omnibox outside the open
  popup; assert dismissal AND real omnibox focus. Click again after Enter URL
  navigation, then after reopening/Escape. The scoped X11 driver refuses points
  outside its verified fixture client; no desktop keymap/settings changes.

Validation for this fix:
- Native Chromium build passed, 4 steps / 33.93s;
  `.local/logs/search-popover-build.log`. Integration succeeded with 38 owned files.
- 96 tooling tests passed (`.local/logs/search-popover-tooling.log`).
- GN UI header/dependency check passed (`.local/logs/search-popover-gn.log`).
- X11 native smoke passed 61 checks, including new pointer regressions, upstream
  tools, navigation, tabs, restart persistence, MV3 fixture and sandbox checks;
  `.local/logs/search-popover-x11.log`, `.local/native-smoke-x11/results.json`.
- Wayland baseline native smoke passed 52 checks, including restart/MV3/sandbox;
  `.local/logs/search-popover-wayland.log`, `.local/native-smoke/results.json`.
  Actual native Wayland popup shortcut/pointer synthesis remains unverified.
- Sequential X11 capture passed two visual checks. Browser bounds
  `[1930,10,1265,1376]`, popup `[2255,479,614,437]`: within one pixel of its center.
  Default, filtered and keyboard-selected screenshots were inspected; page remains
  white behind the popup (no dimming). `.local/blynx-search-visual/capture.json`
  and `.local/logs/search-popover-capture.log`.
- An additional disposable Wayland experiment attempted to open via DevTools
  renderer keyboard injection. It did not produce a focused native popup and
  failed its timeout; this is not Wayland popup validation. Fixture was closed.
  `.local/logs/search-popover-wayland-popup.log`,
  `.local/blynx-search-wayland/capture.json` record the failed experiment.

Previous broader release/IME/accessibility/platform limitations remain. No release
claim. Restart the development browser to load the rebuilt executable; existing
user windows/profiles were not stopped or edited by these disposable tests.

### October 15 follow-up — luminous search styling

User requested lighting around search inspired by Google's multicolor treatment.
Implemented in owned native Views code, without a web surface or new dependency:

- `SearchGlowBorder` derives from upstream BubbleBorder. A blue/red/yellow/green
  sweep shader paints a crisp 1.8 DIP rim with a soft layered halo. Reserved 16 DIP
  transparent frame insets contain the effect inside native bounds, preserving
  bubble anchoring/hit testing and avoiding clipped paint. Corners are 22 DIP.
- The effect is static: no animation loop/timer or offscreen blur surface. Forced
  colors/increased contrast skip the decorative paint and retain native framing.
- Search input is 54 DIP with a larger font; title uses native body emphasis;
  card spacing is refined. Text, selection and result colors retain native tokens.
  No navigation, shortcut, ownership, profile, sandbox or dismissal changes.

Validation:
- Final native build passed, 3 steps / 24.46s;
  `.local/logs/search-glow-build-final.log`. Prior initial glow build also passed.
- GN UI header/dependency check passed (`.local/logs/search-glow-gn.log`).
- X11 native smoke passed 61 checks on the final build, including real outside
  click, input after Enter/reopen/Escape, tab/tools/navigation and runtime restart
  checks. `.local/logs/search-glow-native-x11.log`,
  `.local/native-smoke-x11/results.json`.
- Final normal and forced-high-contrast captures passed centered/focused field,
  native control/shortcut and clean shutdown checks. Default, filtered and arrow
  selection renders were inspected. Normal render shows a smooth colored halo;
  forced-high-contrast render shows the native neutral outline and no colored
  halo. This verifies the decorative fallback, not a broad accessibility audit.
  Logs `.local/logs/search-glow-capture-final.log` and
  `.local/logs/search-glow-contrast.log`; reports under
  `.local/blynx-search-visual/` and `.local/blynx-search-glow-contrast/`.
  Browser `[1930,10,1265,1376]`, popup `[2244,458,636,480]` centered within a pixel.
  Cropped preview: `.local/blynx-search-visual/search-popup-detail.png`.
- Read-only integration check, clang-format dry run and `git diff --check` passed.
  No Python logic changed this slice; the previous 96-test tooling result remains
  historical rather than a new run. Wayland popup interaction/visual validation
  and other broad release/platform limitations remain as documented above.

Restart Blynx to load the rebuilt binary. Existing user windows/profiles were not
closed or edited; all native checks used disposable fixture profiles.

### October 15 follow-up — moving glow, hover accents, optional AI backlog

User requested motion similar to Google's colorful search treatment and an AI
roadmap entry; the AI request was explicitly for TODO.md, not implementation.

- Search halo now rotates one full turn per ten seconds. Native InfiniteAnimation
  requests repaint at 33ms intervals only while the popup is visible; close stops
  it before releasing frame/view references. Bounds and disposal are unchanged.
- Native result buttons use a 180ms SlideAnimation for a gradient wash and thin
  colorful outline on hover/press. Hovered rows share the halo's rotating phase.
  Pointer exit fades the decoration; text/selection and commands remain native.
- Reduced-motion settings keep the halo static and use instant color feedback.
  Forced colors/increased contrast keep native border/hover presentation.
- TODO.md now records optional Qwen 2.5 (0.5B Instruct), WebLLM as a candidate to
  evaluate, local chat/user-directed browser tasks/settings updates, explicit
  enable/disable, on-demand loading and releasing weights/workers/GPU/RAM when
  disabled. Compatibility/quality/memory/action-isolation work remains planned.
  No model/runtime was installed/downloaded and no AI controls were exposed.
- Scoped X11 test driver now supports move-only pointer input through the same
  verified client/bounds checks. Added a negative-coordinate refusal check.

Validation:
- First compile rejected missing Views metadata and a private shader helper;
  corrected metadata registration, public interpolation construction and raw_ptr
  iteration. Final build passed, 3 steps / 23.60s;
  `.local/logs/search-motion-build2.log` (initial failure `search-motion-build.log`).
- 97 tooling tests passed (`.local/logs/search-motion-tooling.log`), including the
  new move/click refusal check. GN UI dependency check passed
  (`.local/logs/search-motion-gn.log`).
- Final X11 native smoke passed 61 checks, including outside-click dismissal,
  input after Enter/reopen/Escape, tools/tabs/navigation and restart/MV3/sandbox;
  `.local/logs/search-motion-native-x11.log` and
  `.local/native-smoke-x11/results.json`.
- Three sequential disposable visual runs passed (normal, forced reduced motion,
  forced high contrast), with real scoped pointer motion, frame comparisons,
  Escape disposal and clean shutdown. Normal halo strip changed by mean ~8.26
  RGB units over one second; reduced-motion and contrast strips changed by 0.
  Result-region captures demonstrate hover appearance, changing color phase and
  exit feedback. Normal and contrast renders were visually inspected.
  Reports `.local/blynx-search-motion/capture.json`,
  `.local/blynx-search-motion-reduced/capture.json`,
  `.local/blynx-search-motion-contrast/capture.json`; logs
  `.local/logs/search-motion-visual-final.log`, `search-motion-reduced-final.log`,
  `search-motion-contrast-final.log`.
- Initial hover capture comparisons sampled a single row while pointer feedback
  was on a different row, producing zero-difference assertion failures. Final
  script uses native client coordinates and compares the result-list interior
  separately from halo/caret. Final captures show the actual hovered row; those
  failures were test-coordinate/sampling failures, not passing behavior evidence.
- Read-only integration, clang-format dry run and diff whitespace checks passed.

Wayland popup/motion synthesis remains unverified on this host; earlier baseline
Wayland checks are historical, not a new animation validation. Broad platform,
accessibility, performance and release limitations remain. Existing user profiles
and OS motion settings were not changed. Restart Blynx to load the new binary.

### October 16 — segmented beam treatment from supplied reference

User supplied `/home/blynx/Pictures/Screenshots/Screenshot_20261016_161826.png`
and https://libraries.dev/beam.html as visual references: pulse-outside on the
popup, pulse-inner on hovered results, pulse-inner or line on the input. Read the
public documentation and viewed the screenshot. Implemented the appearance in
native Views/Skia; no React/npm package, web shell or stack change was introduced.

- Replaced the continuous rainbow with localized cyan/lime/gold/violet highlights
  separated by transparent/dark gaps. The existing ten-second phase now drives
  travelling beams plus subtle brightness pulses.
- Outside: rounded clipping excludes the card interior; 24 DIP transparent frame
  insets contain a soft 7 DIP blurred stroke and narrow light edge.
- Inside: hovered/pressed results retain the 180ms fade but clip the beam and a
  3 DIP blur inside the row. Removed the previous full-row colored wash.
- Input: native FocusableBorder plus a thin moving beam / 1 DIP soft edge. The
  native focus ring is restored after Textfield::SetBorder (which removes it),
  then used for contrast mode; color mode uses the beam. Input text padding and
  native text/selection colors remain owned by Views. Input/frame beam pointers
  are detached after stopping animation and before Widget disposal.
- Reduced motion freezes beams with immediate hover feedback; forced colors and
  increased contrast retain native focus/border/hover styling. No command,
  profile, search-routing or dismissal changes. AI remains only a TODO entry.

Validation and repairs:
- Initial build passed, but opening the popup crashed in the first real test:
  custom Textfield::SetBorder removed the FocusRing before it was configured.
  Fixed by reinstalling the native ring before setting its focus predicate.
  Failed fixture/report preserved under `.local/logs/search-beam-visual.log`.
- Final build passed, 3 steps / 22.66s;
  `.local/logs/search-beam-build3.log`. Final GN UI dependency check passed
  (`.local/logs/search-beam-gn-final.log`).
- Final native X11 smoke passed 61 checks, including Ctrl+Space, tool/tab/URL
  actions, click-away dismissal, Enter/reopen/Escape, restart/MV3/sandbox checks;
  `.local/logs/search-beam-native-x11.log`, `.local/native-smoke-x11/results.json`.
- Final normal visual run passed moving outside/input beams, result hover color
  and motion, exit feedback, Escape disposal and clean shutdown. Outer-strip
  mean RGB change ~4.53 over one second, input-line change ~21.59; hovered result
  region changes ~6.28 and its continuing beam phase changes ~0.32.
  `.local/logs/search-beam-visual-final.log`,
  `.local/blynx-search-beam/capture.json`; actual hover preview
  `.local/blynx-search-beam/hover-moving-popup.png` visually inspected.
- Final forced reduced-motion and forced high-contrast visual runs passed: outer
  and input strip changes are exactly 0, with working hover feedback and clean
  disposal. Contrast render was inspected: native ring/hover, no colored beams.
  `.local/logs/search-beam-reduced2.log`, `search-beam-contrast2.log`; reports under
  `.local/blynx-search-beam-reduced/` and `.local/blynx-search-beam-contrast/`.
- Capture attempts before final runs were invalidated by desktop input changing
  the fixture query, changing popup geometry/hover, or by an initial window-paint
  transition. Final captures clear/check the fixture query and wait for settled
  paint. A temporary XWarpPointer test-driver experiment was reverted; the tracked
  input driver has no change. No separate Xvfb/Xephyr server was installed.
- Read-only integration check, clang-format dry run and diff whitespace check
  passed. No tooling test rerun was needed for this C++ styling slice; prior 97
  tooling result is historical. No user profile or OS setting was changed.

Wayland popup/beam interaction remains unverified, as do the previously documented
broad performance/accessibility/platform/release audits. No performance claim is
made for native blur painting. Restart Blynx to load this build.


## October 16 — configurable search appearance

Settings → Appearance → Search appearance exposes every Beam option natively:
md/sm/line/pulse-inner/pulse-outside independently for popup, input and result
hover; colorful/mono/ocean/sunset/custom palettes; strength 0–1, animation on/off,
2–30 second revolutions and system/light/dark themes. Fifteen optional UI colors
cover surfaces, text/caret, placeholder, selection, results and outline; four
custom beam stops are independent. Apply saves one profile preference, Cancel
restores the saved draft and Reset stages defaults. Reopen Ctrl+Space to apply.
No React package or renderer-to-native control API was introduced.

Owned `appearance/search_appearance.{h,cc}` registers and validates the versioned
`blynx.search_appearance` dictionary. Unknown keys/types/enums/ranges/colors fail
atomically; managed/future preferences remain preserved. Trusted Settings writes
refuse private/guest profiles. Native views load one immutable snapshot per popup.
Zero strength restores the native focus ring; reduced-motion/high-contrast guards
remain. The Settings color preview is illustrative, not a live beam simulation.

Patch 0005 includes Settings controls/localized labels and a Views bubble fix:
explicit color-mode overrides use the bubble's color provider instead of silently
inheriting its anchor's palette. Default bubbles keep upstream palette inheritance.
An actual native surface pixel check caught and repaired BubbleDialogDelegate's
frame-color overwrite; SetBackgroundColor now synchronizes frame/client surfaces.
The render matrix also caught the inherited light-theme override issue. GTK
OS-only background/label tokens are replaced with upstream scheme-aware surface/
on-surface tokens only for an explicit light/dark search theme. Native contrast
still uses OS tokens. Custom outlines paint underneath the beam so thin line
styles remain visible.

Validation this slice:
- Browser builds passed, including the final surface fix (3 steps / 35.77s);
  `.local/logs/search-options-build-surface.log`. Later theme fix evidence below.
- All 12 PrefService tests passed, including three new configuration/guard tests;
  `.local/logs/search-options-prefs-tests-final.log`.
- All 97 Python tooling tests passed; `search-options-tooling-final.log`.
- Headless runtime passed 53 checks with no page errors: staged edit/cancel/reset,
  atomic save/rejection, all option lists, private refusal, persisted selections
  after restart. `.local/logs/search-options-runtime.log`.
- Native X11 passed 69 checks, including actual saved surface RGB, navigation,
  click-away/Enter/reopen/Escape and clean shutdown;
  `.local/logs/search-options-native-x11-final.log`.
- GN UI dependency check passed (`search-options-gn.log`). Settings TypeScript,
  CSS lint and native compilation failures during development were repaired before
  these successful runs. Initial 4-job build was interrupted cleanly and resumed
  serially with 8 jobs. No two GN builds ran concurrently.

Wayland popup interaction, Windows runtime and broad performance/accessibility/
release audits remain unverified. No user profile, OS policy, pin or toolchain was
changed. Restart Blynx to load this build.

Final render/build follow-up:
- Final browser build passed, 3 steps / 23.32s;
  `.local/logs/search-options-build-release-final.log`.
- Six native X11 render cases passed across all five styles, all four named
  palettes plus custom, light/dark/system modes, paused/active animation and zero
  strength. Actual background RGB and input-beam motion/pause were checked, plus
  hover rendering, Escape and clean disposal. Light/default and custom/dark
  captures were inspected. `.local/logs/search-options-matrix-release.log` and
  `.local/search-options-case-{0..5}/capture.json`.
- An earlier matrix sampled a band too far outside the thin outer beam; corrected
  to the input border. Another launch correctly refused an incomplete build
  receipt while the final build was underway; all final cases ran after success.
- Both UI and Views GN dependency checks passed (`search-options-gn-release.log`,
  `search-options-gn-views.log`). Final 97-tooling rerun, overlay read-only check,
  clang-format dry run and diff whitespace check passed. Initial light-mode render
  failures were repaired, not accepted as successful validation.
- Final-build X11 interaction rerun passed all 69 checks with no page errors,
  including custom surface pixels, search/navigation, dismissal and restart;
  `.local/logs/search-options-native-release.log` and
  `.local/native-smoke-x11/results.json`.

## October 16 — assistant integration and launch-readiness repair

The new native assistant slice was present in the workspace but had not been
reintegrated into the ignored Chromium checkout. The launcher readiness guard
therefore refused the desktop entry with a generic “development build may be
incomplete” notification. The specific stale-overlay error named
`browser/ai/assistant_protocol.cc`; the current assistant protocol/panel and one
updated test copy were newer than the installed overlay. The build receipt was
internally valid for the older overlay, so it was not edited or bypassed.

Read-only integration preflight passed, then `python3 -B scripts/integrate.py`
installed the four changed overlay files and reported no upstream conflicts. The
repair build succeeded in five steps / 23.16 seconds using the existing Linux
release output (`.local/logs/assistant-repair-build.log`). The post-build readiness
guard passed, `scripts/integrate.py --check` reported zero pending writes, and
`git diff --check` passed. The launcher was exercised with a fresh disposable
profile and reached a local DevTools endpoint reporting Chrome/154.0.8037.97;
evidence is `.local/logs/assistant-launch-smoke.log`. The launch log retains the
known host/upstream Wayland Vulkan, GCM and GLib diagnostics; no sandbox bypass or
OS policy change was made. Existing user windows and profiles were not edited.

The complete Python tooling suite passed **98 tests**
(`.local/logs/assistant-repair-tooling.log`). The new X11 assistant smoke was
attempted with only a local deterministic model fixture, but stopped at its first
connection-field focus assertion:
`Atspi.Component.grab_focus("Chat endpoint URL")` returned false after the panel
opened. Its report is `.local/assistant-smoke/results.json` and log is
`.local/logs/assistant-smoke-final.log`; this is not counted as assistant feature
validation. The browser build and launcher readiness are repaired, but assistant
provider/action behavior, Wayland panel interaction and the AT-SPI focus issue
remain to be investigated. Restart Blynx (or launch `./blynx`) to use the rebuilt
binary.

## October 17 — conversational assistant and direct browser actions

Read the repository Markdown documentation and the current local Modern Web
Guidance/Libraries.dev skills and beam/orb references. Preserved the native
Views/Chromium stack; no React/npm runtime, model download, profile migration,
pin/toolchain/OS-policy change or credential persistence was introduced.

- Replaced fixed 340×64 message boxes and the fixed transcript canvas with
  content-sized selectable messages measured at the scroll viewport width.
  New messages scroll to the bottom after a successful compositor frame; a posted
  task alone ran before layer geometry was ready. The smoke checks reply position.
  User bubbles, plain assistant replies, a compact header (+ / Settings / close),
  empty-state introduction, model caption and a bottom composer form the chat.
  The composer has a segmented native Skia beam rim, moving only during requests;
  hidden/reduced-motion/contrast safeguards remain. Speech is opt-in by default.
- Enter sends and Shift+Enter inserts a newline. Chromium Textarea consumes
  Return before TextfieldController, so an owned native subclass intercepts at
  PreHandleKeyPressed and leaves IME composition to the native control.
- Supported requested browser actions now validate and dispatch directly, with
  no Run these actions/Dismiss approval surface. The system prompt reflects this.
  Existing bounded allowlist, HTTP(S), weak/live tab URL, last-tab, regular-profile
  and appearance-policy checks remain. Optional tab/page sharing stays explicit.
- Updated the owned GRIT strings and their patch-0005 added-file copy together.
  Reviewed/applied the exact three-line installed catalog delta before normal
  scripts/integrate.py; its conflict guard was preserved. No unrelated upstream
  edits were overwritten. All owned source updates used the integration script.

Validation:
- Final native browser build passed, 3 steps / 25.05s;
  `.local/logs/assistant-chat-build-scroll-final.log`. Final integration preflight reports
  zero pending writes; clang-format dry run and git diff --check passed.
- All 98 Python tooling tests passed (`assistant-chat-tooling-final.log`).
  All 17 focused Chromium PrefService/protocol tests passed
  (`assistant-chat-prefs.log`); UI GN header/dependency check passed
  (`assistant-chat-gn-final.log`).
- All 17 actual X11 assistant checks passed against local deterministic Ollama/API
  fixtures, including multi-turn history, Enter/Shift+Enter, 1,425-pixel long reply,
  direct opening/switching/closing tabs, stale-target rejection, appearance actions,
  invalid-action rejection, Stop/late response, API bearer header, redirect block,
  hide/key undo clearing, disable/New chat, clean restart and private exclusion.
  `.local/logs/assistant-chat-smoke-final.log`, `.local/assistant-smoke/results.json`.
  Conversation and long-reply screenshots were visually inspected.
- General headless browser smoke (before the final assistant-scroll-only rebuild)
  passed all 53 checks with no page errors,
  including restart, Settings, navigation, local MV3 fixtures and sandbox status.
  `.local/logs/assistant-chat-runtime.log`, `.local/runtime-smoke/results.json`.
- Earlier runs exposed a blank transcript (unbounded ScrollView contents were
  never sized), then Return interception in upstream Textarea. Both were fixed
  and retested. One run stopped safely on a transient fixture X11 focus refusal;
  bounded retries preserve the verified-client focus guard. AT-SPI warnings about
  an unrelated sandboxed application are retained in logs.

No real remote API/model quality, live Wayland assistant interaction, native IME
composition, Windows, full inherited suites, broad accessibility/performance or
release validation is claimed. Tests used disposable profiles and fixture keys.
Restart Blynx to load the rebuilt executable; Ctrl+Alt+A opens the panel. Existing
personal windows/profiles were not stopped or edited.

## October 17 — remaining product icons and Add to Blynx

User requested replacing remaining Chromium product icons and the extension
installation button label. The current integration now has **six** reviewed
patches and **65** owned overlay files. Patch 0006 carries the added resource and
renderer wiring; existing local changes, pins, GN arguments, credentials, OS
policy and profiles were preserved.

- Replaced 24 additional product resources: large/unscaled PNGs and Linux shortcut/
  XPM icons, product/fallback SVGs, light/dark wordmarks, tray mask, Windows app/
  document/PDF ICOs, macOS ICNS and the shared dark-mode WebUI logo. Existing
  scaled/native eye resources remain in patch 0004. The light eye has explicit
  24×24 intrinsic dimensions. Functional incognito/isolation glyphs and actual
  external provider/Chrome Web Store identities are preserved.
- `browser/resources/icons/chromium-replacements.json` maps upstream paths to
  owned derivatives. `scripts/generate_product_icons.py` regenerates owned files
  from existing Blynx artwork and the existing local graphics/font tools; it
  never writes the upstream checkout. Binary patch refresh/integration is separate.
- Native extension confirmation says Add to Blynx. The owned renderer helper in
  `browser/renderer/` changes exact English Add to Chrome / Google Chrome /
  Chromium button text and matching accessible labels on HTTPS Store detail
  pages. Main-frame, exact host, port and route guards restrict it. Mutation
  updates are coalesced once per animation frame. Nodes/listeners/disabled states,
  descriptions, Store branding and permission/verification flows are unchanged.
  It exposes no native binding and never clicks or installs anything. This is a
  user-authorized cosmetic exception to earlier documentation's no-frontend-
  modification statement, not a replacement Store or a service-identity change.

Validation:
- Browser build passed: initial 240 actions / 44.62s, final renderer polish four
  actions / 29.78s, final dark-logo build two actions / 7.81s. The completed final
  integration/XPM receipt refresh was a successful no-op in 6.93s.
  `.local/logs/product-branding-build{,-final,-complete}.log`.
- All six branding checks passed with no page errors:
  `.local/logs/product-branding-smoke-final.log`, `.local/branding-smoke/results.json`.
  Verified the actual native renderer hook, dynamic/SPA labels, accessible-name
  changes, unchanged listeners/disabled controls/descriptions, lookalike/other-site
  exclusion, the real dark Extensions toolbar and all 24 asset mappings. Parsed
  the compiled resource packs to compare the 64/128/256/SVG/dark logo bytes and
  native English install string. Store-fixture and Extensions screenshots were
  visually inspected; the wide dark-mode Extensions toolbar visibly uses the eye.
- General browser smoke passed 53 checks, no page errors, including ordinary
  navigation, Settings, restart, local MV3 behavior and sandbox status;
  `.local/logs/product-branding-runtime.log`, `.local/runtime-smoke/results.json`.
- All 98 tooling tests passed (`product-branding-tooling-final.log`); renderer GN
  dependency check, owned C++ formatting, script syntax, integration preflight
  (zero pending writes) and git diff --check passed.

The first fully synthetic Store response hit upstream GoogleURLLoaderThrottle's
CHECK for parsed response headers. No security check was relaxed. The final smoke
loads the real HTTPS Store robots.txt response, then builds controlled DOM nodes
without innerHTML, preserving CSP/Trusted Types and parsed security headers.
Other-site fixtures are intercepted; no extension is installed. Earlier harness
assumptions about gzip SVG packs, UTF-8 locale packs and chrome-resource fetches
were corrected. Actual image loading and resource-pack comparisons replace the
unsupported fetch test. Source assets for Windows/macOS are not runtime evidence.

Not executed: a real Store installation, external-provider compatibility, Windows/
macOS builds/runtime/installer identity, localization of non-English Store labels,
full inherited security/extension suites or packaging. An upstream Store layout/
wording change may require adjusting this cosmetic adapter. Restart Blynx to use
this build. Existing personal browser processes/profiles were not stopped/edited.

## October 17 — provider-neutral canvas, version page and Store recommendations

User requested removal of the two exact English Chrome recommendation prompts,
version-page Chromium branding and the empty non-Google new-tab search area.
Read repository Markdown/current local web guidance; kept the native Chromium
stack, routing, pins, sandbox, profiles and credentials unchanged.

- Both built-in new-tab surfaces share owned `blynx_new_tab.css`, with an eye
  header, Aurora/Midnight/Paper canvas, browser shortcut cards, upstream top sites,
  responsive sizing, visible focus and reduced-motion/forced-color fallbacks.
- The third-party built-in NTP has a labeled native form and trusted Mojo search
  handler. The owned C++ helper validates bounded UTF-8, resolves the current
  TemplateURLService default at submission, and navigates through Chromium's
  WebContents, supporting provider GET/POST templates. External provider/extension
  NTP routing is preserved; no website-native search bridge is introduced.
- Six separate components-resource version wordmarks/favicons now use Blynx;
  logo alt text says Blynx logo. Chromium Authors copyright is retained.
- The exact-origin/top-frame isolated-world Store helper hides only banners and
  dialogs matching the supplied English text/structure. It invokes the matching
  dialog's No thanks button for normal focus/scrim cleanup, never installation.
  Existing Add to Blynx labeling remains detail-route-only.
- Seven reviewed patches/75 owned files. Full NTP template/controller/style folded
  into existing patch 0003, full NTP resource dependency into patch 0005; patch
  0007 adds third-party UI/Mojo, shared resource registration and version assets.
  Normal integration conflict guards remained intact.

Validation:
- Browser build passed: 618 actions / 11m12.70s, followed by formatting cleanup
  (2 actions / 22.59s) and final crisp third-party logo resource polish
  (6 actions / 19.13s). Logs `.local/logs/newtab-store-build5.log`,
  `newtab-store-build-complete.log`, `newtab-store-build-release.log`.
- Final Store/branding smoke passed all eight checks with no page errors. Exact
  banner/dialog suppression, the site's No thanks callback, unrelated security/
  permission UI retention, dynamic/category/detail routes, listeners/disabled
  controls, other-origin exclusion and dark Extensions icon were exercised. All
  30 source asset mappings and compiled version wordmarks/product packs match.
  `.local/logs/newtab-store-branding-final.log`, `.local/branding-smoke/results.json`.
- Final new-tab smoke passed all six checks and exited cleanly, no page errors:
  version Blynx wordmark/alt text with Chromium copyright, shared Google canvas,
  selected Brave provider/form, correctly encoded native Brave destination URL,
  successful loopback-provider query navigation, 390px layout without horizontal
  overflow, compiled branding. `.local/logs/newtab-search-smoke-final.log`,
  `.local/newtab-smoke/results.json`. Desktop/narrow/version screenshots inspected.
- All 98 tooling tests passed (`newtab-store-tooling-release.log`); both new-tab
  GN dependency checks passed (`newtab-google-gn-verified.log`,
  `newtab-third-party-gn-release.log`). CSS stylelint, owned/new C++ formatting,
  script syntax, integration preflight (zero pending writes) and git diff
  whitespace checks passed.
- Final general browser smoke passed all 53 checks with no page errors,
  including Settings/restart, navigation, local MV3 fixtures and Seccomp-BPF
  sandbox status. `.local/logs/newtab-store-runtime-final.log`,
  `.local/runtime-smoke/results.json`.

Initial iterations corrected a ResourceRequestBody API call and a direct GRIT
resource dependency. An unnecessary new string ID was removed to avoid continued
resource-ID shifts. Own builds were interrupted cleanly before resuming; no
concurrent builds, security-policy changes or readiness bypass occurred.

The initial search harness failed to intercept native navigation and received
Brave's bot-check page for a fixed synthetic query; the native destination URL
was correct. The final harness blocks Brave DNS resolution and checks the native
destination URL, then tests successful submission against a loopback HTTP
provider. It does not claim Brave's live service accepted a query. A fixture
HTTP preconnection initially kept Node alive after assertions; explicit tracked
socket destruction fixed cleanup and the final process exited successfully.
The earlier hanging process was stopped, its run was not counted as passing.

Not executed: real Store installation, non-English/changed Store selectors,
provider POST end-to-end, external provider/extension-owned NTP compatibility,
Windows/macOS runtime, broad inherited security/accessibility/performance or
release suites. No personal profiles/preferences, keys, existing user processes,
pins, toolchain or OS settings were changed. Restart Blynx to load this build.
