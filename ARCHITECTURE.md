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
5. `0005-blynx-experience.patch`: internal aliases, Settings appearance controls,
   new-tab canvases and a Settings-local illustrative preview. Staged edits update
   the miniature browser without theme-service or preference writes; accent CSS
   accepts only a six-digit RGB hex color. Apply remains the only write action.
   Settings also exposes guarded, non-writing preset validation/export. An
   off-the-record guard avoids querying an unavailable sign-in service while
   constructing upstream Settings localization data.

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
Cancel restores the saved state. A miniature browser preview reflects staged
theme/accent/tab/bookmark/canvas/shortcut choices without modifying browser prefs.
It illustrates layout and color choices; Chromium derives the actual native palette. Invalid input does not partially mutate settings.
The old dialog implementation remains for isolated tests but is not exposed by
the toolbar or new-tab Customize action. Canvas artwork is local; uploaded logo
and generated taskbar asset live in `browser/resources/icons/`.

The final redesign build keeps an upstream `ntp-logo` element hidden in the NTP
DOM for inherited TypeScript/test compatibility while the visible Blynx eye mark
is rendered separately. This preserves upstream theme/test object shape without
restoring the old visible logo or toolbar popup.


## Preset file UI

Settings imports use the browser's native HTML file chooser. File size is checked
before reading (1–4096 bytes), UTF-8 decoding is fatal, and the trusted native
Settings handler invokes the existing strict C++ schema parser and canonical
exporter. Import only stages the validated preset in the local preview; Apply
rechecks profile/policy/future-schema access before writing through PrefService.
Cancel or a later edit invalidates an outstanding file read so it cannot restore
stale edits. Tab layout and canvas prefs are outside preset schema v1 and retain
their staged values. Other schema fields remain compatibility data until native
token consumers are implemented.

Export reads the saved native preset, refuses invalid/unsupported storage or
read-only/managed access, and hands canonical JSON to Chromium's normal Blob
URL download service with a fixed `.json` filename. It does not serialize
credentials, take filesystem paths from WebUI, or save staged edits. The owned
GRIT catalog and its adjacent-GRD patch copy must match; integration now refuses
mismatches before changing the source checkout.

## First-run setup and identity pass

The native first visible normal window asks the owned appearance controller
whether setup should open. A new regular profile starts at step 1; existing
profiles at the default step 0 are left alone. Incomplete steps 1–3 resume once
per profile/process, and completed step 4 suppresses automatic setup. Headless,
private, guest, system, policy-controlled and unknown future states are excluded.
The deferred Settings navigation uses the window's weak pointer.

Trusted Settings exposes bounded setup actions through `BlynxSettingsHandler`.
The local, non-syncable `blynx.onboarding_step` preference stores progress only.
Welcome, transfer and personalization use the existing Lit Appearance page;
step changes focus the visible heading. Transfer opens upstream `/importData`,
which owns source detection, supported data types, user confirmation and secure
credential import. Cancel returns to setup. Setup never reads another browser's
files itself and never marks an import successful. Appearance still saves only
through Apply; setup completion is an independent preference.

Browser-owned English GRIT messages now use Blynx, with source-browser names,
OS names, attribution and actual external-provider/store identities preserved.
The Google-services category is titled “Services and connections”; its provider
disclosures still identify Google. Existing translations may fall back to English
for changed messages. Compatibility identifiers, URLs and on-disk profile roots
are deliberately stable. Shared WebUI and native product glyphs now use the
owned monochrome eye, inheriting their caller's theme/contrast color. Linux
application PNGs continue to use the existing Blynx artwork.

The transfer step links to the existing password CSV importer and extension
manager in separate tabs, preserving setup progress. The pinned Linux source
profile detector supports Firefox; exported bookmarks HTML and password CSV
provide upstream import paths for other browsers. Extensions and unsupported
history/sessions are not silently copied. Native payment footer branding uses the
same product eye vector with `ui::kColorIcon`, replacing its inherited wordmark.


## Native Blynx search

Ctrl+Space is registered with the normal browser window's Views FocusManager and
handled by BrowserView. Its existing unknown-command accelerator path also handles
renderer focus, while the native focus manager handles the top omnibox. App/kiosk
windows are excluded. This is a browser-local accelerator, with no OS-wide hotkey
or input-policy change. The toolbar owns the popup lifetime but adds no button.

Blynx search is a centered, nonmodal native Views bubble with a search field and up to
seven visible result rows. A tonal selected row and native theme colors preserve
contrast. Up/Down changes the selected result, Enter runs it, Escape or Ctrl+Space
closes it. Clicking outside or activating another window also dismisses it.
Result-row buttons use the same action handler. The bubble anchors to the browser
root view and centers its bounds within that window, following moves/resizes. It
sizes to results, with compact rows and rounded native framing. A custom native
BubbleBorder paints a blue/red/yellow/green sweep rim and a soft halo inside
16 DIPs of reserved transparent frame space. Layered translucent strokes provide
the falloff without an offscreen blur surface. A native InfiniteAnimation rotates
the gradient once per ten seconds at a 33ms interval only while the popup is
visible. Disposal stops it before releasing frame/view references. Native button
hover transitions fade a gradient wash/rim in and out over 180ms; hovered rows
share the rotating color phase. Reduced-motion settings keep the colors static;
forced colors and increased contrast retain the standard native border instead. Search text,
selection and result colors remain upstream theme tokens. No Go/Cancel form.

Commands are filtered from a fixed table using Chromium's enabled state, including
address-bar focus, new tab, history, downloads, bookmarks, settings, tab layout,
Developer Tools, Task Manager, extensions and Password Manager. Menu accelerator
markup is removed using the upstream gfx utility. Tab title/host search inspects
only the invoking window. Weak WebContents handles resolve their live tab index;
closing/moving a tab cannot select another tab by an obsolete index. Navigation
is a separate result, so a failed stale-tab action refreshes without web search.

Nonempty text also offers an address/search result. On acceptance it calls the
current LocationBar's OmniboxEditModel CanPasteAndGo/PasteAndGo path. Chromium owns
URL classification, configured search-provider routing and navigation safeguards;
Blynx adds no independent URL parser or search backend. This initial popup does
not clone full omnibox autocomplete/history/bookmark suggestions. Focus address
bar uses the upstream native command and leaves the real omnibox in control.

Before execution a synchronous close callback disposes the client-owned Widget,
then posts an action with weak browser/tab handles. Availability and live tab
identity are checked again. The bubble never installs window modality or a dimming
backdrop, and cannot leave a modal scrim behind after an action.
Cancel/Escape queues nothing. No search history, preference store or renderer API
is added. The layout action additionally requires a regular profile and a
user-modifiable preference, consistent with the owned Settings write guards.
