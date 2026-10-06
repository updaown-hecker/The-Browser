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
