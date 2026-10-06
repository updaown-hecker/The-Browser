# Native Windows build

Same overlay and patches, native Chromium GN/clang-cl; **no Linux-to-Windows
cross-build is claimed**. No Windows host is available in this session, so all
Windows build/runtime/installer checks remain unverified.

Pinned upstream requirements:
https://chromium.googlesource.com/chromium/src/+/154.0.8037.97/docs/windows_build_instructions.md

Windows 10 or newer, x86-64, NTFS, >=100 GiB free (prefer >=150), >=16 GiB RAM.
The pinned guide requires Visual Studio 2026 >=18.0.0, Desktop development with
C++, MFC/ATL, Windows 11 SDK 10.0.28000.2270, and debugging tools >=10.0.26100.3323.
Recheck that guide whenever the Chromium pin changes. Chromium uses bundled
clang-cl, but VS headers/libraries/tools and the SDK are still required.

Use a short path with no spaces, e.g. `C:\src\browser-final`. Do not share a
depot_tools/source checkout with WSL. Initial depot_tools bootstrap should be
run from `cmd.exe` per upstream's instructions; do not set global credentials or
change users' installed toolchains automatically.

```bat
python scripts\bootstrap.py tools --yes
python scripts\bootstrap.py bootstrap --yes
python scripts\bootstrap.py hooks --yes
python scripts\bootstrap.py --check
python scripts\integrate.py --check
python scripts\integrate.py
python scripts\build.py configure --platform windows
python scripts\build.py build --platform windows -j 4
python scripts\build.py run --platform windows
```

Child processes set `DEPOT_TOOLS_WIN_TOOLCHAIN=0` to use the local VS installation;
the wrappers do not change global environment variables. `vs2026_install` can
explicitly identify an installation if upstream requires it. Output is
`.local\chromium\src\out\windows-release\chrome.exe`; the launch wrapper uses
its own development profile, not installed Chrome's data.

```bat
python -B -m unittest discover -s tests -v
python scripts\build.py test --platform windows --test-target browser:browser_identity_appearance_unittests -j 4
python scripts\build.py test --platform windows --test-target browser:appearance_prefs_unittests -j 4
python scripts\build.py test --platform windows --test-target unit_tests -j 4 -- --gtest_filter=AppearancePrefStoreTest.*
python scripts\build.py test --platform windows --test-target browser_tests -j 4 -- --gtest_filter=NativeAppearanceBrowserTest.*:NativeAppearancePolicyBrowserTest.*
```

## Installer release gates

Packaging is intentionally disabled. Basic app/ProgID names and user-data roots
are distinct in source, but platform binary naming, COM interfaces/CLSIDs,
Active Setup, notification activation, icon resources, Start Menu/uninstall and
default-browser integration still need a complete identity audit and real tests.
Never reuse Chromium's service/COM identity in a released package. Use the
upstream installer architecture once that is resolved; produce a signed-ready
artifact and Authenticode/signing verification before calling it a release.

Retain upstream Windows sandbox, OS credential protection, high-DPI/native window
controls, snapping and file/dialog APIs. Test these on Windows rather than
assuming a successful Linux build proves them.
