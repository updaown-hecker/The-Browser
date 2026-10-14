# Architecture

## Distribution, not an engine rewrite

Pinned Chromium 154.0.8037.97 (`config/upstream.json`) supplies the actual desktop
browser target `//chrome:chrome`, native Views chrome, content processes, Blink,
V8, extensions, sandbox, permission mediation, GPU/media/PDF and DevTools.
Keep supported GN/Clang/libc++/Siso conventions. C++23 is used by the isolated
library too. Windows builds natively with clang-cl and upstream-required VS/SDK.

Our source is an overlay at upstream `//browser`, installed by
`scripts/integrate.py`. Thin patches connect it to upstream services. The source
checkout lives inside ignored `.local/` to respect workspace confinement while
keeping millions of upstream files out of this repository's version control.

## Owned modules and dependency direction

- `identity/`: stable product identifiers and portable safe profile components;
  library path construction does not replace Chromium's ProfileManager.
- `appearance/appearance_preset`: schema v1, flat <=4 KiB JSON, full validation,
  canonical export, UTF-8 and finite/bounded numeric checks. Unknown and duplicate
  fields are rejected. No executable themes, remote URLs or secret fields.
- `appearance/appearance_pref_store`: leaf adapter to PrefRegistry/PrefService.
  Invalid data is read without repair. Unsupported schema is preserved and cannot
  be overwritten. Off-the-record/system/guest writes are disallowed by callers.
- `ui/`: Views dialog plus Profile-observing controller. Existing ThemeService
  setters apply accent/color scheme; both upstream bookmark-bar prefs are set.
  The tab-layout checkbox writes upstream `vertical_tabs.enabled`, separately
  from the appearance JSON schema. Chromium owns tab layout and session behavior.
  Recheck policy and extension/custodian control at Apply time. No preview rollback
  which could lose an extension theme. Preset/theme/accent/bookmark edits update
  the dialog preview automatically; Apply remains the only write operation.
- `resources/`: GRIT strings (English fallback), owned SVG icon source and
  librsvg-rendered 16/32/64 pixel PNG assets for native Linux/Views resources.

The UI implementations compile in existing `//chrome/browser/ui:ui`; toolbar
depends only on owned header targets. Pref registration uses a separate leaf
target. This avoids `toolbar -> custom UI -> browser UI -> toolbar` cycles.

## Patch boundaries

1. `0001-branding.patch`: BRANDING, prominent product strings, OS profile roots,
   Windows app/ProgID names. Copyright/license provenance remains in upstream.
2. `0002-customization.patch`: pref registry, native toolbar/dialog sources and
   dependencies, GRIT catalog and test linkage. Preserve internal `chrome://`
   routing; never expose a custom privileged scheme to untrusted sites.
3. `0003-product-surface.patch`: branded built-in new-tab header and browser
   shortcuts around the upstream search UI; separate Linux desktop identity.
   All links use existing trusted Chromium WebUI pages; no new message bridge.
4. `0004-native-icons.patch`: replaces selected Linux/Views 1x/2x product icons
   with renders of the owned icon. Windows and other platform assets remain
   pending the platform identity audit.

Binary output names remain upstream `chrome`/`chrome.exe` in this development
slice. The root `blynx` is an executable development launcher for the
built Chromium binary, not a renamed or packaged browser binary. It selects
the project development profile and a user-local desktop identity; it can use
the existing verified root-owned installed Chrome SUID sandbox helper. It does
not copy installed profiles or alter OS security policy. Windows COM/Active Setup identifiers are still upstream
and **must be made unique consistently before installer creation**. Packaging is
disabled rather than risking collisions with installed Chromium.

## Future slices (not shipped functionality)

Integrate design tokens with ColorProvider/LayoutProvider/native tab views before
exposing density, radius or tab-shape controls. Reuse upstream vertical tabs and
split-tab facilities where viable; the current dialog already exposes the
upstream vertical layout preference. More elaborate tab customization and split
view still need their own implementation and validation.
Workspace service should map stable session/tab/group IDs under a per-profile
keyed service, not parallel unsafe persistence. Isolated WebContents for web
panels must retain normal site permissions, no privileged WebUI bridge.

Use Chromium's existing secure credential facilities, never an independent vault.
Native network filtering requires a separately tested request-layer service and
explicit update/list licensing; no blocker exists in this slice. Signed update
metadata/trust roots, anti-rollback and platform package signatures are required
before a release updater. Development source updates are explicit manual steps.

## User-agent and service decisions

Retain Chromium's compatibility UA (including its historical Chrome token).
This is the upstream web-compatibility behavior, **not permission to impersonate
Google Chrome to services**. No branded-build flag, borrowed API credentials or
store allowlist bypass. Google sign-in/sync, Widevine and proprietary codecs are
not promised; distribution must audit each external service separately.

## Public internal addresses and Settings

Blynx registers a standard navigation alias, not another trusted WebUI origin.
The browser URL handler canonicalizes `blynx://` into `chrome://` before upstream
policy, extension/new-tab and WebUI handlers. The tab observer brands only the
virtual URL of successfully committed Chromium internal documents. Actual
origins, bindings, isolated processes and resource URLs remain upstream. Ordinary
HTTP(S), extension and chrome-untrusted origins are not branded as trusted pages.
The `webstore` alias opens the official HTTPS Chrome Web Store.

Owned `BlynxSettingsHandler` is attached only to trusted Settings and applies
bounded validated appearance JSON through the existing native controller. New-tab
style/shortcut preferences are per-profile PrefService values, constrained to
known canvases and guarded before writes. The Lit card stages changes until Apply;
Cancel reloads the saved state. Invalid input does not partially mutate settings.
The old dialog implementation remains for isolated tests but is not exposed by
the toolbar or new-tab Customize action. Canvas artwork is local; uploaded logo
and generated taskbar asset live in `browser/resources/icons/`.

The final redesign build keeps an upstream `ntp-logo` element hidden in the NTP
DOM for inherited TypeScript/test compatibility while the visible Blynx eye mark
is rendered separately. This preserves upstream theme/test object shape without
restoring the old visible logo or toolbar popup.
