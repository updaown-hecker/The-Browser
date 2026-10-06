#ifndef BROWSER_UI_NATIVE_APPEARANCE_CONTROLLER_H_
#define BROWSER_UI_NATIVE_APPEARANCE_CONTROLLER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "browser/appearance/appearance_pref_store.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "third_party/skia/include/core/SkColor.h"

class Profile;

namespace browser {

enum class NativeAppearanceAccess {
  kAllowed,
  kReadOnlyProfile,
  kManaged,
  kUnsupportedStoredVersion,
  kUnavailable,
};

// This conversion is only for the implemented native accent setting. The
// general preset's background/radius/density/etc. are intentionally not used.
std::optional<SkColor> AppearanceAccentColor(const AppearancePreset& preset);

// Staging/preview is local to the dialog; only Apply writes either JSON or
// Chromium preferences. In particular, Cancel never reinstalls a theme (the
// upstream reinstaller can delete a user's NTP background image).
class NativeAppearanceController : public ProfileObserver {
 public:
  explicit NativeAppearanceController(Profile& profile);
  ~NativeAppearanceController() override;

  AppearancePrefReadResult Load() const;
  NativeAppearanceAccess GetAccess() const;
  bool Apply(const AppearancePreset& preset);

 private:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  raw_ptr<Profile> profile_;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
};

}  // namespace browser

#endif  // BROWSER_UI_NATIVE_APPEARANCE_CONTROLLER_H_
