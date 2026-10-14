# Upstream maintenance

`config/upstream.json` pins Chromium 154.0.8037.97 and an exact depot_tools
revision. The tag/hash were resolved from official googlesource and match the
installed Chrome engine version used for the baseline. Pinning is reproducibility,
not a claim that this will remain the newest secure release.

Keep the small Blynx repository separate from the ignored full upstream
Git checkout. No massive source tree, generated output, profile or dependency
cache belongs in Blynx commits. Track owned components and ordered patch
files; keep unmodified upstream license/source attribution intact.

## Security-release workflow

1. Monitor official Chromium security/stable announcements frequently. Evaluate
   patch urgency; do not distribute a browser whose security base is stale.
2. Preserve current work and create a **new** upstream checkout/output for the
   reviewed release (or a deliberate worktree with synced dependencies).
3. Resolve official tag and full hash, update config pin on an owned feature
   branch, and refresh depot_tools deliberately. Scripts never update it silently.
4. Rebase/regenerate patches against exact upstream. Integration refuses a
   different HEAD or partial/conflicting patch installation. Do not use force,
   reset or clean to hide conflicts or discard another developer's work.
5. Run dependency sync/hooks; use integration read-only preflight, then install
   the overlay. Configure GN, run GN include checks, build custom units and full
   browser, run custom and upstream regressions on Linux and Windows.
6. Validate extension APIs, sandbox, site permissions, profiles/sessions, GPU,
   media, secure storage and packaging identities. Re-run release benchmarks.
7. Review licenses/diffs and sign/release only when release gates pass. Record
   exact revisions, conflicts, evidence and rollback/handoff state.

`bootstrap.py sync --yes` is for a clean unmanaged checkout and defaults to the
config pin with no hooks. It refuses dirty patched sources; use a fresh checkout
rather than deleting the fork overlay. `hooks` is explicit. `integrate.py` does
not commit or reset either repository and preserves locally edited owned files.

## Expected conflict points

Pref-registration splits, browser UI/toolbar target decomposition, Views dialog
APIs/ownership, ThemeService/policy names, bookmark visibility migration, GRIT
part/resource-ID bookkeeping and test targets. Branding paths and Windows install
metadata also need review. The native rendering/network/security engine should
normally remain untouched; do not scatter fork-specific changes through it.
