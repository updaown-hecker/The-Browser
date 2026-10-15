#include "browser/appearance/appearance_pref_store.h"

#include <utility>

#include "base/json/json_reader.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace browser {

void RegisterAppearanceProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(kOnboardingStepPref, 0);
  registry->RegisterStringPref(kAppearanceJsonPref, "");
  registry->RegisterStringPref(kNewTabStylePref, "aurora");
  registry->RegisterBooleanPref(kNewTabLinksPref, true);
}

AppearancePrefStore::AppearancePrefStore(PrefService& prefs,
                                         AppearancePrefAccess access)
    : prefs_(prefs), access_(access) {}

AppearancePrefReadResult AppearancePrefStore::Read() const {
  AppearancePrefReadResult result;
  const auto* pref = prefs_->FindPreference(kAppearanceJsonPref);
  if (!pref || !pref->GetValue()->is_string()) {
    result.state = AppearancePrefState::kInvalid;
    result.error = AppearancePresetError::kWrongType;
    return result;
  }
  const std::string& json = pref->GetValue()->GetString();
  if (json.empty()) {
    return result;
  }
  if (json.size() > AppearancePreset::kMaximumJsonBytes) {
    result.state = AppearancePrefState::kInvalid;
    result.error = AppearancePresetError::kInputTooLarge;
    return result;
  }
  // A future version may introduce fields before schema_version. Identify its
  // version before the strict v1 parser rejects those fields. Size and depth
  // remain bounded, and no parsed object is imported as a partial preset.
  auto document = base::JSONReader::Read(json, base::JSON_PARSE_RFC, 16);
  if (document && document->is_dict()) {
    const auto version = document->GetDict().FindInt("schema_version");
    if (version &&
        *version != static_cast<int>(AppearancePreset::kCurrentSchemaVersion)) {
      result.state = AppearancePrefState::kUnsupported;
      result.error = AppearancePresetError::kUnsupportedVersion;
      return result;
    }
  }
  // The existing flat, bounded parser checks size before inspecting input and
  // returns no partial data. Do not normalize or repair the on-disk value here.
  auto parsed = ImportAppearancePreset(json);
  if (parsed.ok()) {
    result.preset = std::move(*parsed.preset);
    result.state = AppearancePrefState::kSaved;
  } else {
    result.error = parsed.error;
    result.state = parsed.error == AppearancePresetError::kUnsupportedVersion
                       ? AppearancePrefState::kUnsupported
                       : AppearancePrefState::kInvalid;
  }
  return result;
}

AppearancePrefWriteResult AppearancePrefStore::Write(
    const AppearancePreset& preset) {
  if (access_ != AppearancePrefAccess::kWritableProfile) {
    return AppearancePrefWriteResult::kReadOnlyProfile;
  }
  const auto* pref = prefs_->FindPreference(kAppearanceJsonPref);
  if (!pref || !pref->GetValue()->is_string()) {
    return AppearancePrefWriteResult::kUnavailable;
  }
  if (!pref->IsUserModifiable()) {
    return AppearancePrefWriteResult::kManaged;
  }
  if (Read().state == AppearancePrefState::kUnsupported) {
    return AppearancePrefWriteResult::kUnsupportedStoredVersion;
  }
  auto json = ExportAppearancePreset(preset);
  if (!json || json->size() > AppearancePreset::kMaximumJsonBytes) {
    return AppearancePrefWriteResult::kInvalidPreset;
  }
  prefs_->SetString(kAppearanceJsonPref, *json);
  return AppearancePrefWriteResult::kSaved;
}

}  // namespace browser
