#include "browser/ui/blynx_settings_handler.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "browser/appearance/appearance_pref_store.h"
#include "browser/appearance/search_appearance.h"
#include "browser/ui/native_appearance_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"

namespace browser {
namespace {
bool CanEditNewTab(PrefService* prefs) {
  for (const char* name : {kNewTabStylePref, kNewTabLinksPref}) {
    const auto* pref = prefs->FindPreference(name);
    if (!pref || !pref->IsUserModifiable()) {
      return false;
    }
  }
  return true;
}
}  // namespace

void BlynxSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getBlynxSearchAppearance",
      base::BindRepeating(&BlynxSettingsHandler::GetSearchAppearance,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "applyBlynxSearchAppearance",
      base::BindRepeating(&BlynxSettingsHandler::ApplySearchAppearance,
                          base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "blynxOnboarding", base::BindRepeating(&BlynxSettingsHandler::Onboarding,
                                             base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getBlynxAppearance",
      base::BindRepeating(&BlynxSettingsHandler::Get, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "applyBlynxAppearance", base::BindRepeating(&BlynxSettingsHandler::Apply,
                                                  base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "validateBlynxAppearancePreset",
      base::BindRepeating(&BlynxSettingsHandler::ValidatePreset,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "exportBlynxAppearancePreset",
      base::BindRepeating(&BlynxSettingsHandler::ExportSavedPreset,
                          base::Unretained(this)));
}

base::DictValue BlynxSettingsHandler::SearchState(bool saved) {
  auto* profile = Profile::FromWebUI(web_ui());
  auto* prefs = profile->GetPrefs();
  const bool editable =
      profile->IsRegularProfile() && !profile->IsOffTheRecord() &&
      !profile->IsGuestSession() && CanWriteSearchAppearance(*prefs);
  return base::DictValue()
      .Set("config", LoadSearchAppearance(*prefs))
      .Set("defaults", DefaultSearchAppearance())
      .Set("editable", editable)
      .Set("saved", saved);
}
void BlynxSettingsHandler::GetSearchAppearance(const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  ResolveJavascriptCallback(args[0], SearchState(false));
}
void BlynxSettingsHandler::ApplySearchAppearance(const base::ListValue& args) {
  if (args.size() != 2 || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  auto state = SearchState(false);
  const bool saved =
      args[1].is_dict() && state.FindBool("editable").value_or(false) &&
      WriteSearchAppearance(*Profile::FromWebUI(web_ui())->GetPrefs(),
                            args[1].GetDict());
  ResolveJavascriptCallback(args[0], SearchState(saved));
}

void BlynxSettingsHandler::Onboarding(const base::ListValue& args) {
  if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  AllowJavascript();
  auto* profile = Profile::FromWebUI(web_ui());
  auto* prefs = profile->GetPrefs();
  int step = prefs->GetInteger(kOnboardingStepPref);
  const bool editable = CanEditBlynxOnboarding(*profile);
  const auto& action = args[1].GetString();
  bool saved = false;
  if (editable) {
    int next = step;
    if (action == "start") {
      next = 1;
    }
    if (action == "next" && step >= 1 && step < 3) {
      next = step + 1;
    }
    if (action == "back" && step > 1 && step <= 3) {
      next = step - 1;
    }
    if (action == "finish" && step >= 1 && step <= 3) {
      next = 4;
    }
    if (next != step) {
      prefs->SetInteger(kOnboardingStepPref, next);
      step = next;
      saved = true;
    }
  }
  ResolveJavascriptCallback(args[0], base::DictValue()
                                         .Set("step", step)
                                         .Set("editable", editable)
                                         .Set("saved", saved));
}

base::DictValue BlynxSettingsHandler::State(bool saved) {
  auto* profile = Profile::FromWebUI(web_ui());
  NativeAppearanceController controller(*profile);
  const auto value = controller.Load();
  auto json = ExportAppearancePreset(value.preset);
  auto* prefs = profile->GetPrefs();
  return base::DictValue()
      .Set("preset", json.value_or(""))
      .Set("vertical", controller.VerticalTabsEnabled())
      .Set("style", prefs->GetString(kNewTabStylePref))
      .Set("links", prefs->GetBoolean(kNewTabLinksPref))
      .Set("editable",
           controller.GetAccess() == NativeAppearanceAccess::kAllowed &&
               CanEditNewTab(prefs))
      .Set("saved", saved);
}

void BlynxSettingsHandler::Get(const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  ResolveJavascriptCallback(args[0], State(false));
}

void BlynxSettingsHandler::Apply(const base::ListValue& args) {
  if (args.size() != 5 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_bool() || !args[3].is_string() || !args[4].is_bool()) {
    return;
  }
  AllowJavascript();
  const auto parsed = ImportAppearancePreset(args[1].GetString());
  const auto& style = args[3].GetString();
  bool saved = false;
  auto* profile = Profile::FromWebUI(web_ui());
  auto* prefs = profile->GetPrefs();
  NativeAppearanceController controller(*profile);
  if (parsed.preset &&
      (style == "aurora" || style == "midnight" || style == "paper") &&
      CanEditNewTab(prefs) &&
      controller.Apply(*parsed.preset, args[2].GetBool())) {
    prefs->SetString(kNewTabStylePref, style);
    prefs->SetBoolean(kNewTabLinksPref, args[4].GetBool());
    saved = true;
  }
  ResolveJavascriptCallback(args[0], State(saved));
}

// Validation is read-only. Imported data is staged in Settings; the existing
// Apply endpoint alone writes preferences after checking access again.
void BlynxSettingsHandler::ValidatePreset(const base::ListValue& args) {
  if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  AllowJavascript();
  auto* profile = Profile::FromWebUI(web_ui());
  NativeAppearanceController controller(*profile);
  const auto parsed = ImportAppearancePreset(args[1].GetString());
  std::optional<std::string> canonical;
  if (parsed.preset &&
      controller.GetAccess() == NativeAppearanceAccess::kAllowed &&
      CanEditNewTab(profile->GetPrefs())) {
    canonical = ExportAppearancePreset(*parsed.preset);
  }
  ResolveJavascriptCallback(args[0],
                            base::DictValue()
                                .Set("valid", canonical.has_value())
                                .Set("preset", canonical.value_or("")));
}

void BlynxSettingsHandler::ExportSavedPreset(const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return;
  }
  AllowJavascript();
  auto* profile = Profile::FromWebUI(web_ui());
  NativeAppearanceController controller(*profile);
  const auto stored = controller.Load();
  std::optional<std::string> canonical;
  if (controller.GetAccess() == NativeAppearanceAccess::kAllowed &&
      CanEditNewTab(profile->GetPrefs()) &&
      (stored.state == AppearancePrefState::kSaved ||
       stored.state == AppearancePrefState::kUnconfigured)) {
    canonical = ExportAppearancePreset(stored.preset);
  }
  ResolveJavascriptCallback(args[0],
                            base::DictValue()
                                .Set("valid", canonical.has_value())
                                .Set("preset", canonical.value_or("")));
}
}  // namespace browser
