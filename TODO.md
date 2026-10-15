# Remaining work (not implemented claims)

## Immediate
- Completed: Linux link, branded new-tab rendering, local navigation/tabs/download,
  local MV3 worker/storage/disable-enable and sandbox-status smoke checks.
- Complete broader browsing/profile/bookmark/session, F11, DevTools, GPU/media,
  renderer-crash and accessibility tests; investigate Wayland/Vulkan warnings.
  Focused smoke now also covers Back/Forward, bookmark create/search/reload/delete,
  the history route and regular/private-context site-storage separation. These
  Clean process restart now also verifies saved appearance/canvas/tab prefs, site
  storage, bookmarks and MV3 storage in both smoke modes. These checks do not
  replace inherited suites or full tab/session/crash-recovery coverage.
- Execute full native appearance policy/incognito browser tests. Smoke covers
  Apply/restart/Cancel/tab layout and private Settings validate/export/apply refusal.
  Fixed the observed private Settings null sign-in-service crash; full policy and
  in-process incognito suites remain unexecuted.
- Validate direct Web Store behavior and permissions/profile separation without
  impersonation or bypass; the full MV3 gtest remains unexecuted.
- Retry the full `unit_tests` build/filter from the existing GN output; the first
  attempt timed out after 10 minutes while compiling unrelated upstream tests.

## Daily browsing and customization
- Audit inherited behavior first. Polish owned settings/about/new-tab branding
  and cover remaining platform icon resources; selected Linux/Views icons are owned.
  About/menu Blynx labels now match product identity; Chromium attribution remains.
- Completed: Settings-local illustrative preview of staged theme/accent/tab/
  bookmark/canvas/shortcut choices; Apply alone saves, Cancel discards edits.
  Completed: Settings preset file import/export, strict file rejection, staging/
  Apply/Cancel/round-trip, delayed-read cancellation and private-profile refusal.
  Platform-native dialog/portal interaction remains to be tested. Actual centralized native token consumers for density, scaling,
  radius, spacing, motion and colors remain pending.
- Native vertical tabs positions, DnD/groups/pinning/audio/keyboard/collapsed mode;
  evaluate upstream vertical/split tab infrastructure instead of duplicating it.
- Per-profile persistent workspaces; modular isolated side panels; command palette,
  tab search, shortcut conflict editing and optional gestures.
- Completed initial local new-tab canvases/shortcut controls and eye branding.
  Add custom wallpaper file selection, widget visibility and further search controls.
- Test existing secure passwords/autofill, import/export safeguards, media/codecs,
  accessibility/localization and platform privacy permissions.

## Privacy/performance
- Upstream service/network/telemetry audit and transparent feature availability.
- Standard/strict/custom privacy controls and measured, isolated native blocking,
  rule/list licensing, allowlists, cosmetics and update safety.
- Balanced/Performance/Memory Saver mapping to upstream lifecycle facilities;
  exclusions, discarded state and task-manager metrics.
- Release desktop/PSS/CPU/Speedometer/load/tab-switch benchmarks and profiling.

## Distribution
- Own executable/package IDs and all desktop/icon/protocol paths.
- Unique Windows COM/Active Setup/service/AppUserModel identities and IDL consistency;
  native build/DPI/snapping/credential/notification/default-app tests.
- Linux .deb dependency/xdg/KDE Wayland/X11/AppArmor/sandbox checks.
- License/media/service audits, reproducible optimized/signature-ready artifacts,
  signed update infrastructure and ongoing upstream security releases.
- Enable packaging only after these gates and runtime regressions pass.

## First-run and branding follow-on
- Implemented: per-profile setup, optional upstream importer entry, appearance
  personalization, completion/reopen, weak-window automatic presentation for new
  profiles and resume for incomplete setup; private/guest/policy/future-state guards.
- Implemented: browser-owned GRIT identity pass and theme-colored shared WebUI /
  native eye product glyphs. Actual provider/store identities remain truthful.
- Validated: Linux browser build, 46 runtime checks, 51 combined native checks,
  nine PrefService tests, 93 tooling tests and independent existing/completed/future
  profile checks. Setup opens the native browser-data importer, password CSV
  importer and extension manager; actual data transfer remains a separate gate.
- Remaining: end-to-end browser-data import using the OS file chooser and supported
  installed-browser sources; Windows icons/runtime/installer identity; localization
  review and full inherited policy/incognito/security regression suites.

## Developer documentation
- Completed: organized `DEBUG_AND_TEST_COMMANDS.md` covering quick checks, builds,
  smoke/gtest commands, disposable-profile onboarding, logs, DevTools, extension
  inspection, GN/lint, native debugging, sanitizers, benchmarks and Windows.
