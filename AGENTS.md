# Development constraints

This is a native Chromium distribution, not Electron or a WebView shell.
Read HANDOFF.md before continuing. Do not replace the selected stack or silently
change the Chromium pin, toolchain, OS policy, credentials or user's profiles.

- Own code in browser/, reviewed integration in patches/series, pins in config/.
- `.local/` contains ignored source/dependencies/output/profiles/logs/raw results.
  Never stage it, PROMPT.txt, secrets, crash dumps or browser data.
- GN/Chromium is the browser build. CMake tests only the isolated config library.
- Reintegrate owned source changes using scripts/integrate.py; preserve local
  overlay edits. It is not a command to overwrite upstream conflicts.
- Keep Chromium C++23/Clang/libc++, safety diagnostics, sandbox and isolation.
  No insecure flags, arbitrary website-to-native APIs, custom password encryption
  or unsigned background updater.
- Use Views for native chrome, GRIT for UI strings, PrefService for per-profile
  configuration and upstream browser/extension/credential services.
- Run the smallest real checks and state exactly what passed, failed or was not
  executed. Compiled UI or inherited source is not proof of working behavior.
- Do not run two builds in the same GN output concurrently. Capture large logs
  under .local/logs and read only relevant diagnostics, not generated trees.
- At session end update HANDOFF.md, required docs and TODO.md truthfully. Do not
  advertise a daily-driver release or performance/extension advantage without
  actual validation evidence.
