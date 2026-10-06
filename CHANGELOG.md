# Changelog

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
