# Changelog

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
