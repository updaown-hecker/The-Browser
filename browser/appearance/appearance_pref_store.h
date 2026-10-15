#ifndef BROWSER_APPEARANCE_APPEARANCE_PREF_STORE_H_
#define BROWSER_APPEARANCE_APPEARANCE_PREF_STORE_H_

#include "base/memory/raw_ref.h"
#include "browser/appearance/appearance_preset.h"

class PrefRegistrySimple;
class PrefService;

namespace browser {

// Profile-local, deliberately not syncable. Empty means never configured.
inline constexpr char kAppearanceJsonPref[] = "browser_final.appearance_json";

inline constexpr char kOnboardingStepPref[] = "blynx.onboarding_step";

inline constexpr char kNewTabStylePref[] = "blynx.new_tab_style";
inline constexpr char kNewTabLinksPref[] = "blynx.new_tab_links";

void RegisterAppearanceProfilePrefs(PrefRegistrySimple* registry);

enum class AppearancePrefAccess { kWritableProfile, kReadOnlyProfile };
enum class AppearancePrefState {
  kUnconfigured,
  kSaved,
  kInvalid,
  kUnsupported,
};
enum class AppearancePrefWriteResult {
  kSaved,
  kReadOnlyProfile,
  kUnavailable,
  kManaged,
  kInvalidPreset,
  kUnsupportedStoredVersion,
};

struct AppearancePrefReadResult {
  AppearancePreset preset = AppearancePreset::Default();
  AppearancePrefState state = AppearancePrefState::kUnconfigured;
  AppearancePresetError error = AppearancePresetError::kNone;
};

// The caller must explicitly provide profile access, never an OTR profile's
// original PrefService. Reads are non-mutating, including malformed/oversized
// input. Unknown schema versions are preserved and cannot be overwritten by
// this version of the application.
class AppearancePrefStore {
 public:
  AppearancePrefStore(PrefService& prefs, AppearancePrefAccess access);

  AppearancePrefReadResult Read() const;
  AppearancePrefWriteResult Write(const AppearancePreset& preset);

 private:
  const raw_ref<PrefService> prefs_;
  const AppearancePrefAccess access_;
};

}  // namespace browser

#endif  // BROWSER_APPEARANCE_APPEARANCE_PREF_STORE_H_
