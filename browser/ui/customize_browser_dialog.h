#ifndef BROWSER_UI_CUSTOMIZE_BROWSER_DIALOG_H_
#define BROWSER_UI_CUSTOMIZE_BROWSER_DIALOG_H_

#include <memory>

#include "ui/gfx/native_ui_types.h"

class Profile;

namespace views {
class Widget;
}  // namespace views

namespace browser {

inline constexpr int kCustomizeBrowserToolbarButtonId = 64001;

// Owned by ToolbarView, not self-deleting. Its client-owned Widget is destroyed
// before its delegate/controller. Reopening a closed dialog reloads prefs.
class CustomizeBrowserDialog {
 public:
  CustomizeBrowserDialog(Profile& profile,
                         gfx::NativeWindow context,
                         gfx::NativeView parent);
  CustomizeBrowserDialog(const CustomizeBrowserDialog&) = delete;
  CustomizeBrowserDialog& operator=(const CustomizeBrowserDialog&) = delete;
  ~CustomizeBrowserDialog();

  bool IsClosed() const;
  void Show();

  // Stable view IDs for in-process UI assertions; no Chrome command IDs.
  static constexpr int kPresetComboboxId = 64002;
  static constexpr int kThemeComboboxId = 64003;
  static constexpr int kAccentTextfieldId = 64004;
  static constexpr int kBookmarksCheckboxId = 64005;
  static constexpr int kPreviewButtonId = 64006;
  static constexpr int kPreviewBookmarksId = 64007;
  static constexpr int kVerticalTabsCheckboxId = 64008;
  views::Widget* widget_for_testing() { return widget_.get(); }

 private:
  class Delegate;
  std::unique_ptr<Delegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
};

}  // namespace browser

#endif  // BROWSER_UI_CUSTOMIZE_BROWSER_DIALOG_H_
