# Security policy and release gates

This source is **not security-audited or approved as a daily browser**. A complete
Chromium link, runtime test and timely upstream update process are prerequisites
to distribution. Vulnerabilities must be handled privately by the maintainers;
no public security-report address is established yet.

## Invariants

- Preserve Chromium sandbox, process/site/extension isolation, safe origins,
  certificate validation and permissions. Do not introduce `--no-sandbox`,
  insecure-content/certificate bypass or privileged APIs to ordinary pages.
- Owned UI is native Views. Existing `chrome://` internal boundaries remain;
  there is no localhost control server or website-to-shell execution bridge.
- Theme import is data-only, <=4096 bytes, strict schema/types/ranges/UTF-8,
  no executable code/paths/remote requests/secrets. Unsupported versions are
  preserved and refuse overwrite. Apply rechecks managed/extension policy.
- Incognito/guest/system profiles cannot use our controller to mutate regular
  appearance prefs. Profile destruction invalidates the borrowed profile pointer.
- Credentials/passwords/autofill must use Chromium and OS secure storage. No
  custom crypto or plaintext secret store is implemented.
- No own telemetry or crash upload endpoint. Tooling opts out of depot_tools
  metrics in child processes. Upstream networking/services still require a
  release privacy audit; **this is not a promise of zero Chromium network traffic**.
- No borrowed Google API keys, Google branding or proprietary media/CDM bundles.

Development logs may contain build paths or local test details. Browser profiles,
crash dumps and raw logs remain under ignored `.local/`; never commit browsing
data, credentials or user instructions. Never put administrative passwords into
scripts, shell arguments, diagnostics or documentation.

## Updates and distribution

Source/toolchain bootstrap fetches reviewed HTTPS upstream pins and runs upstream
hooks only after an explicit action. Integration refuses a different source HEAD
and conflicting/local overlay edits. Re-check pins and patches on each security
release (`UPSTREAM.md`). The pinned version is not proof of ongoing patch currency.

No self-updater exists. An eventual updater needs signed artifacts and metadata,
trusted key distribution, expiry/anti-rollback rules and verified platform
installers. Until then, release packaging fails closed. No unsigned background
download-and-execute mechanism is acceptable.

Before release: unique OS identities; dependency/license audit; profile separation;
TLS/sandbox/renderer-crash tests; extension/permission regression tests; privacy
network capture; safe-download service availability disclosures; safe key storage
tests; distribution signatures; a sustainable upstream security cadence.

## Internal-page alias boundary

`blynx://` is accepted only as a browser navigation alias. It is not registered
as an additional secure, CORS-enabled or privileged WebUI renderer scheme.
Navigation is canonicalized to upstream `chrome://` before policy, extensions
and WebUI dispatch. Successfully committed internal pages receive a branded
virtual address; their actual origins and bindings remain unchanged. HTTP(S),
extension, chrome-untrusted and error documents are not assigned that display.

Appearance messages are registered only on trusted Settings. The owned handler
validates bounded schema JSON and field types, known new-tab styles and profile/
policy write access before changing preferences. No website-to-native API,
custom encryption, install bypass or updater is introduced. The Web Store route
opens the official external service and adds no verification exception.

Recorded October 13 evidence includes the isolated CMake and focused Chromium
identity/appearance and PrefService tests. The October 14 continuation reran the
Python tooling tests (now 93), integration/patch preflight, `git diff --check`, and
expanded DevTools/native Wayland smoke after the Settings preview build. The
preview never writes preferences while staging edits; its CSS accent accepts only
six-digit RGB hex. A private-context fixture test verifies local-storage
separation. This evidence is not a security audit; broad policy/incognito,
renderer-crash, network/privacy, Windows and package checks remain release gates.


Preset file imports are bounded before reading and use fatal UTF-8 decoding plus
the native strict schema parser. They stage data only; Apply rechecks write access.
Export reads canonical saved data only and refuses invalid/unsupported storage.
User-selected files and exported JSON use Chromium's standard file-input/download
facilities; no WebUI-supplied path reaches native file I/O. The import read is
invalidated by Cancel or later edits. Runtime file tests use only owned fixtures
and isolated regular/private browser contexts. Platform file-dialog/portal and
broader policy/security suites remain separate validation gates.

The expanded smoke found and fixed a null sign-in-service dereference in upstream
Settings localization for a private browser context. Off-the-record contexts now
skip this account-service query. Real private Settings tests refuse preset
validate/export/apply, and clean-restart tests preserve ordinary profile data.
These checks do not replace the full in-process policy/incognito/security suites.

First-run setup persists only a bounded profile-local progress integer. Native
handlers recheck regular-profile and preference-policy access on every action;
future/invalid progress states are not overwritten. Automatic presentation does
not import data or touch existing browser profiles. Transfer delegates to
Chromium's standard importer and its explicit source/data confirmation, using
upstream credential services. No password file parser, custom encryption,
external-site branding injection, service impersonation or install bypass is added.


Native Blynx search (Ctrl+Space) enumerates only the invoking window's tabs,
using weak handles and live index lookup. Tab results display title/host rather than
raw URL paths/queries/fragments. Typed text is not persisted by the popup. Tool
actions come from a fixed native command table and recheck upstream availability;
tab-layout writes also require a regular profile and user-modifiable preference.

The explicit address/search result delegates to the existing omnibox
CanPasteAndGo/PasteAndGo classifier and configured provider. It does not add a
URL parser, credentials handling, service endpoint or website-to-native bridge.
Navigation retains upstream policy, certificate, permissions and sandbox checks.
The actual top omnibox remains available through its normal focus command.

Accepted actions close the popup before posting work, with weak browser/tab
references and a fresh access/index check. Escape queues no action. A stale-tab
selection refreshes rather than silently navigating or activating a different tab.
The accelerator is local to normal browser windows; OS input settings are untouched.
