# Chromium-compatible extensions

Use the upstream desktop extension architecture and `chrome://extensions`, not
an incompatible custom extension system. No browser-specific disabling of
Manifest V3, sandbox boundaries, permission prompts or CRX verification is added.
No “100% compatible” claim is made. The local MV3 smoke test passes service-worker messaging, storage and
disable/re-enable persistence. This is limited unpacked-extension evidence.

`browser/tests/extensions_browsertest.cc` now exercises a real unpacked MV3
service worker, local storage round-trip and disable/re-enable through Chromium's
test harness. It compiles with Chromium Clang but the full gtest has not yet been executed;
compilation is not a compatibility
result. Run `browser_tests --gtest_filter='BrowserFinalExtensionTest.*'` after
building the full test binary.

`python3 -B scripts/runtime_smoke.py` loads the owned local MV3 fixture in a
disposable profile and checks runtime message/storage round trips, then disables
and re-enables it through the upstream extensions page. Store installation,
other extensions and the broader API matrix remain unverified.

The latest redesign follow-up reran this smoke successfully with no page errors;
the result is recorded under ignored `.local/runtime-smoke/results.json`.

## Chrome Web Store investigation

The pinned open-source Chromium includes the Web Store installer and restricted
`webstorePrivate` API, now under `extensions/browser/` with Chrome delegates in
`chrome/browser/extensions/api/webstore_private/`. Their presence is not proof
that the current Store frontend accepts this independent distribution, nor a
distribution/service license grant. Direct installation **has not been verified**.

Official documentation distinguishes Store installs and alternative mechanisms:
- https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions
- https://developer.chrome.com/docs/extensions/how-to/distribute/host-on-linux

Those instructions describe Google Chrome. They document different Linux vs
Windows/macOS external-install restrictions; do not assume Linux CRX installation
behavior applies to Windows, or that developer-mode loading proves Store access.

We leave upstream install prompts/verifiers and restricted-origin rules intact.
There is no Store scraping/downloader, Chrome-brand switch, key borrowing,
frontend injection, allowlist bypass or package-signature workaround. The precise
current limitation is **unverified integration and service/distribution approval**,
not an invented blanket claim that all independent browsers are prohibited.

Until verified, the legitimate baseline is unpacked extensions from their author
via upstream Developer mode / Load unpacked, and author-distributed signed CRX or
update mechanisms only where upstream platform policy and license permit. Users
must review permissions and trust the author. Do not silently sideload extensions.

## Required real tests

After linking the browser, test Load unpacked, MV3 service worker/storage API,
host/content-script permissions, enable/disable/reload/remove, profile separation,
incognito opt-in, errors, permissions changes and `declarativeNetRequest` behavior.
Then evaluate an author-approved Store listing using the browser's actual identity
and install prompt. Record Store errors and platform details without bypassing them.

Do not advertise direct Store install, sync, Google OAuth, payment APIs or a
specific extension's compatibility before its tested result. Some APIs depend on
external services beyond Chromium's local engine.
