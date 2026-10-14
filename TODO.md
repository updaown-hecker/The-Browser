# Remaining work (not implemented claims)

## Immediate
- Completed: Linux link, branded new-tab rendering, local navigation/tabs/download,
  local MV3 worker/storage/disable-enable and sandbox-status smoke checks.
- Complete broader browsing/profile/bookmark/session, F11, DevTools, GPU/media,
  renderer-crash and accessibility tests; investigate Wayland/Vulkan warnings.
- Execute full native appearance policy/incognito browser tests. Targeted native
  Apply/restart/Cancel and upstream tab-layout checks are covered by smoke tests.
- Validate direct Web Store behavior and permissions/profile separation without
  impersonation or bypass; the full MV3 gtest remains unexecuted.

## Daily browsing and customization
- Audit inherited behavior first. Polish owned settings/about/new-tab branding
  and cover remaining platform icon resources; selected Linux/Views icons are owned.
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
