# Remaining work (not implemented claims)

## Immediate
- Link and launch a minimally branded Linux browser; verify real navigation,
  multi-tab operations, F11, DevTools, sandbox/GPU, downloads and renderer crashes.
- Execute native appearance browser tests, including restart, policy and incognito.
  The focused nine preference unit tests already pass; native sources compile.
- Add/run ordinary browsing/profile/bookmark/download/session integration tests;
  execute the now-compiled MV3 worker/storage/disable-enable browser test and
  tests; validate direct Web Store behavior without impersonation/bypass.

## Daily browsing and customization
- Audit inherited behavior first. Polish owned settings/about/new-tab branding
  and replace upstream icon resources with the owned icon artwork.
- Native immediate live preview and safe import/export file UI; consume actual
  centralized tokens for density, scaling, radius, spacing, motion and colors.
- Native vertical tabs positions, DnD/groups/pinning/audio/keyboard/collapsed mode;
  evaluate upstream vertical/split tab infrastructure instead of duplicating it.
- Per-profile persistent workspaces; modular isolated side panels; command palette,
  tab search, shortcut conflict editing and optional gestures.
- Search/new-tab widgets/wallpaper controls without network/advertising dependencies.
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
