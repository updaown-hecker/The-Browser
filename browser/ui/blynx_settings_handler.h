#ifndef BROWSER_UI_BLYNX_SETTINGS_HANDLER_H_
#define BROWSER_UI_BLYNX_SETTINGS_HANDLER_H_

#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

namespace browser {
// Bound exclusively to the upstream trusted Settings WebUI. All mutations use
// the same profile/policy/schema guards as the native appearance controller.
class BlynxSettingsHandler : public settings::SettingsPageUIHandler {
 public:
  BlynxSettingsHandler() = default;
  ~BlynxSettingsHandler() override = default;
  void RegisterMessages() override;
  void OnJavascriptAllowed() override {}
  void OnJavascriptDisallowed() override {}

 private:
  void Get(const base::ListValue& args);
  void Apply(const base::ListValue& args);
  base::DictValue State(bool saved);
};
}  // namespace browser
#endif
