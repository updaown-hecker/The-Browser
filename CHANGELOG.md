# Changelog

## October 16, 2026 — configurable search appearance

- Added per-profile search customization under Settings → Appearance: five beam
  styles independently for popup/hover/input, named/custom palettes, intensity,
  pause/speed, light/dark/system and custom UI/beam colors.
- Added color previews and staged Apply/Cancel/Reset, with RGB/schema/range
  validation, policy/future-version preservation and private/guest write refusal.

## October 16, 2026 — search beam treatment

- Replaced the full rainbow rim with softer moving beams and dark gaps, following
  the supplied Libraries.dev reference: outside pulse on the card, inside pulse
  on hovered rows, and a thin beam on the native search field.
- Preserved native contrast and reduced-motion fallbacks, centering and dismissal.

## October 15, 2026 — search motion and AI roadmap

- Added slow motion to the search halo and fading multicolor hover accents on
  result rows. Motion stops on close and respects reduced-motion settings.
- Added the planned optional Qwen 2.5 0.5B Instruct assistant to TODO.md: chat,
  browser tasks, WebLLM evaluation, and disable/unload controls to save memory.

## October 15, 2026 — luminous search

- Added a blue/red/yellow/green rim and soft halo around native Blynx search,
  with a larger input and refined spacing. The glow is static; forced colors
  and increased contrast use the native border.

## October 15, 2026 — search popover fixes

- Replaced the modal search dialog with a browser-anchored native bubble.
  Clicking outside dismisses it; actions dispose it before dispatch. No grey
  modal backdrop is created or left behind.
- Tightened spacing, typography and row heights, added rounded framing, and
  centered the bubble within the browser bounds on initial show and resize.
- Added pointer regressions for outside dismissal and input after Enter/Escape.

## October 15, 2026 — centered keyboard search

- Ctrl+Space opens native Blynx search; the initial toolbar button is removed.
- Added a large search field, visible tool/tab rows, arrow-key selection,
  Enter/Escape, centered adaptive sizing and native theme colors.
- Address/web queries use Chromium's omnibox; a tool focuses the real top bar.
  Added Developer Tools, Task Manager, extension and password-manager commands.
- Preserved private/policy guards and weak tab identity; actions run after popup
  dismissal. Build/runtime outcomes and platform limits are in HANDOFF.md.

## October 14, 2026 — appearance preset file controls

- Added Settings preset import through Chromium file selection, strict size/UTF-8/
  native-schema validation and staging until Apply. Cancel invalidates late reads.
- Added export of saved canonical presets through the upstream download service,
  with profile/policy/unsupported-data guards and no native file-path bridge.
- Added an integration guard against mismatched owned and patched GRIT catalogs.
- Fixed an observed private Settings crash caused by querying an unavailable
  upstream sign-in service. Corrected About/menu Blynx labels, preserving attribution.
- Passed 93 tooling tests, 38 runtime checks, 41 combined native checks and an
  independent HTTPS load. Added clean-process persistence coverage for appearance,
  canvas, site storage, bookmarks and MV3 storage. Full results are in HANDOFF.md.

## October 14, 2026 — Settings preview and browsing checks

- Added a Settings-local illustrative browser preview for staged theme, accent,
  tab layout, bookmarks, canvas and shortcut choices. Only Apply saves preferences;
  Cancel restores the saved choices. Invalid RGB input uses a safe preview fallback.
- Corrected the Settings emblem to use the upstream current-channel logo route.
- Expanded actual-browser smoke coverage for preview transaction behavior, logo
  loading, Back/Forward, bookmark operations, history routing and private-context
  site-storage separation. Exact rebuild/runtime results are in HANDOFF.md.

## October 13, 2026 — redesign follow-up validation

- Completed the redesign Chromium rebuild after preserving the hidden upstream
  NTP logo compatibility element required by inherited TypeScript tests.
- Re-ran integration preflight, 90 Python tests, CMake, targeted Chromium tests,
  DevTools runtime smoke and native Wayland/AT-SPI smoke successfully.
- Recorded the broad `unit_tests` timeout as an unfinished validation task rather
  than treating partial compilation as a pass.

## October 13, 2026 — Blynx interface redesign

- Adopted the supplied eye logo and generated a matching taskbar icon.
- Designed Aurora/Midnight/Paper new-tab canvases with optional browser shortcuts.
- Moved appearance customization into Settings with guarded Apply/Cancel;
  removed the native toolbar popup and routed new-tab Customize to Settings.
- Added public `blynx://` aliases and branded internal address-bar display while
  retaining the upstream trusted origins and legacy `chrome://` navigation.
- Added the `webstore` route to the official external Chrome Web Store.
- Runtime validation results are recorded in HANDOFF.md after the rebuilt browser
  passes the new Settings/routing checks.

## October 13, 2026 — Blynx development browser

- Renamed public product branding, new-tab heading, native customization and
  application-menu entry to Blynx; added `./blynx` while preserving legacy profile
  paths and launcher compatibility.
- Fixed the toolbar API compilation failure that left new resources paired with
  an old executable. Added successful-build readiness checks before launching.
- Added branded new-tab shortcuts, selected native icons, dialog preview and
  the upstream vertical-tab preference control.
- Added actual-browser navigation/tab/download/MV3/sandbox smoke checks and native
  Apply/restart/Cancel checks. Exact outcomes are recorded in HANDOFF.md.
- Release packaging and full cross-platform/security validation remain pending.

## October 12, 2026 — development launch diagnosis

- Recorded successful full Linux browser build from the user's terminal output.
- Diagnosed AppArmor namespace restriction; verified Wayland process startup with
  the existing installed Chrome sandbox helper without OS policy changes.
- Observed renderer namespace isolation and renderer/GPU seccomp; visual UI and
  browsing remain unverified. Recorded headless DOM timeout and Wayland warning.
- Documented the command-scoped sandbox-helper launch in Linux build instructions.

## 0.1.0-dev — first development session

- Inspected empty repository and KDE neon toolchain/resources; preserved prompt.
- Established pinned workspace-local Chromium/depot_tools bootstrap and overlay.
- Synced Chromium 154 source/dependencies/hooks; Linux release GN generation.
- Added product identity/profile paths, strict appearance preset schema/library,
  native Views dialog/controller, PrefService adapter, policy/private-mode guards
  and GRIT strings with upstream registration/toolbar/test patches.
- Added Python tooling/integration/benchmark tests and Chromium unit/browser tests.
- Built and executed isolated C++ tests with GCC/CMake and Chromium Clang/libc++.
- Compiled native dialog/controller, toolbar/pref hooks and native/extension
  browser-test sources. Executed all nine focused PrefService unit tests,
  including disk persistence and managed/read-only guards, successfully.
- Added local headless benchmark runner and recorded a Chrome-only baseline.
- Release packaging blocked pending identity/security/runtime audits.

Full browser launch and shipping feature completeness are not implied; current
targeted compiler and runtime status is recorded in `HANDOFF.md`.

### First-run setup and identity follow-on (October 14)

- Added resumable, profile-local Blynx welcome, optional browser-data import entry,
  and personalization setup; existing profiles can start it from Appearance.
- Rebranded browser-owned Settings/install/extension messages and monochrome
  WebUI/native product glyphs; provider/store identities and upstream attribution
  remain identifiable. Native and WebUI eye glyphs follow interface theme colors.
- Runtime coverage extended for setup navigation/focus, importer open/cancel,
  private writes, clean restart persistence and completion suppression.

- Transfer setup links to the upstream password CSV importer and extension manager
  without completing setup. Replaced the native payment footer wordmark with the
  theme-colored product eye; provider logos remain unchanged.
- Validated the Linux link, 46 runtime / 51 combined native checks, nine PrefService
  tests and 93 tooling tests; independent existing/completed/future-state native
  fixtures pass. Data import through OS selection and other release gates remain.
