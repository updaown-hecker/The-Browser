#include "browser/ui/blynx_settings_handler.h"

#include "base/functional/bind.h"
#include "browser/appearance/appearance_pref_store.h"
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
      "getBlynxAppearance",
      base::BindRepeating(&BlynxSettingsHandler::Get, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "applyBlynxAppearance", base::BindRepeating(&BlynxSettingsHandler::Apply,
                                                  base::Unretained(this)));
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
}  // namespace browser
