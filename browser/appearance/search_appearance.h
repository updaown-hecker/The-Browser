#ifndef BROWSER_APPEARANCE_SEARCH_APPEARANCE_H_
#define BROWSER_APPEARANCE_SEARCH_APPEARANCE_H_

#include "base/values.h"

class PrefRegistrySimple;
class PrefService;
namespace browser {
inline constexpr char kSearchAppearancePref[] = "blynx.search_appearance";
base::DictValue DefaultSearchAppearance();
bool ValidateSearchAppearance(const base::DictValue& value);
void RegisterSearchAppearancePrefs(PrefRegistrySimple* registry);
base::DictValue LoadSearchAppearance(const PrefService& prefs);
// Profile access is checked by the caller. Policy/future versions are checked
// here.
bool CanWriteSearchAppearance(const PrefService& prefs);
bool WriteSearchAppearance(PrefService& prefs, const base::DictValue& value);
}  // namespace browser
#endif
