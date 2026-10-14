#include <string>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "browser/ui/customize_browser_dialog.h"
#include "browser/ui/native_appearance_controller.h"
#include "chrome/browser/policy/policy_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/bookmarks/common/bookmark_bar_visibility_state.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace browser {

using NativeAppearanceBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       ToolbarHasNoCustomizationPopup) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_FALSE(
      browser_view->toolbar()->GetViewByID(kCustomizeBrowserToolbarButtonId));
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       PRE_SavedAppearanceSurvivesRestart) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  ASSERT_TRUE(controller.Apply(AppearancePreset::Dark()));
  base::RunLoop committed;
  profile->GetPrefs()->CommitPendingWrite(committed.QuitClosure());
  committed.Run();
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       SavedAppearanceSurvivesRestart) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  EXPECT_EQ(AppearancePreset::Dark(), controller.Load().preset);
  auto* theme = ThemeServiceFactory::GetForProfile(profile);
  EXPECT_EQ(ThemeService::BrowserColorScheme::kDark,
            theme->GetBrowserColorScheme());
  EXPECT_EQ(AppearanceAccentColor(AppearancePreset::Dark()),
            theme->GetUserColor());
  EXPECT_EQ(ui::mojom::BrowserColorVariant::kTonalSpot,
            theme->GetBrowserColorVariant());
  EXPECT_TRUE(
      profile->GetPrefs()->GetBoolean(bookmarks::prefs::kShowBookmarkBar));
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       ApplyUsesBothBookmarkVisibilityPreferences) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  ASSERT_TRUE(controller.Apply(AppearancePreset::Compact()));
  EXPECT_FALSE(
      profile->GetPrefs()->GetBoolean(bookmarks::prefs::kShowBookmarkBar));
  EXPECT_EQ(
      static_cast<int>(bookmarks::BookmarkBarVisibilityState::kOnlyShowOnNtp),
      profile->GetPrefs()->GetInteger(
          bookmarks::prefs::kBookmarkBarVisibilityState));
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       TabLayoutUsesUpstreamProfilePreference) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  ASSERT_TRUE(controller.Apply(AppearancePreset::Dark(), true));
  EXPECT_TRUE(controller.VerticalTabsEnabled());
  EXPECT_TRUE(profile->GetPrefs()->GetBoolean(prefs::kVerticalTabsEnabled));
  // Selecting a preset through a non-dialog caller preserves the tab layout.
  ASSERT_TRUE(controller.Apply(AppearancePreset::Default()));
  EXPECT_TRUE(controller.VerticalTabsEnabled());
  ASSERT_TRUE(controller.Apply(AppearancePreset::Default(), false));
  EXPECT_FALSE(controller.VerticalTabsEnabled());
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       InvalidAndFutureDataDoNotChangeNativeAppearance) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  auto* theme = ThemeServiceFactory::GetForProfile(profile);
  const auto original_scheme = theme->GetBrowserColorScheme();
  const auto original_accent = theme->GetUserColor();
  auto invalid = AppearancePreset::Default();
  invalid.accent_color = "red";
  EXPECT_FALSE(controller.Apply(invalid));
  const std::string future = "{\"future_field\":true,\"schema_version\":2}";
  profile->GetPrefs()->SetString(kAppearanceJsonPref, future);
  EXPECT_EQ(NativeAppearanceAccess::kUnsupportedStoredVersion,
            controller.GetAccess());
  EXPECT_FALSE(controller.Apply(AppearancePreset::Dark()));
  EXPECT_EQ(future, profile->GetPrefs()->GetString(kAppearanceJsonPref));
  EXPECT_EQ(original_scheme, theme->GetBrowserColorScheme());
  EXPECT_EQ(original_accent, theme->GetUserColor());
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       IncognitoRefusesOriginalProfileChanges) {
  auto* regular = browser()->GetProfile();
  auto* incognito_browser = CreateIncognitoBrowser(regular);
  auto* incognito = incognito_browser->GetProfile();
  auto* theme = ThemeServiceFactory::GetForProfile(regular);
  const auto before = theme->GetBrowserColorScheme();
  const auto color = theme->GetUserColor();
  const std::string saved = regular->GetPrefs()->GetString(kAppearanceJsonPref);
  NativeAppearanceController controller(*incognito);
  EXPECT_EQ(NativeAppearanceAccess::kReadOnlyProfile, controller.GetAccess());
  auto* parent =
      BrowserView::GetBrowserViewForBrowser(incognito_browser)->GetWidget();
  CustomizeBrowserDialog dialog(*incognito, parent->GetNativeWindow(),
                                parent->GetNativeView());
  auto* delegate =
      dialog.widget_for_testing()->widget_delegate()->AsDialogDelegate();
  EXPECT_FALSE(delegate->IsDialogButtonEnabled(ui::mojom::DialogButton::kOk));
  EXPECT_FALSE(controller.Apply(AppearancePreset::Dark()));
  EXPECT_EQ(saved, regular->GetPrefs()->GetString(kAppearanceJsonPref));
  EXPECT_EQ(before, theme->GetBrowserColorScheme());
  EXPECT_EQ(color, theme->GetUserColor());
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       DialogLoadsSavedValuesAndPreviewCancelDoesNotWrite) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  ASSERT_TRUE(controller.Apply(AppearancePreset::Dark()));
  const std::string saved = profile->GetPrefs()->GetString(kAppearanceJsonPref);
  auto* theme = ThemeServiceFactory::GetForProfile(profile);
  const auto native_accent = theme->GetUserColor();
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  CustomizeBrowserDialog dialog(*profile, parent->GetNativeWindow(),
                                parent->GetNativeView());
  dialog.Show();
  auto* widget = dialog.widget_for_testing();
  auto* root = widget->GetContentsView();
  auto* mode = static_cast<views::Combobox*>(
      root->GetViewByID(CustomizeBrowserDialog::kThemeComboboxId));
  auto* accent = static_cast<views::Textfield*>(
      root->GetViewByID(CustomizeBrowserDialog::kAccentTextfieldId));
  auto* bookmarks = static_cast<views::Checkbox*>(
      root->GetViewByID(CustomizeBrowserDialog::kBookmarksCheckboxId));
  ASSERT_TRUE(mode);
  ASSERT_TRUE(accent);
  ASSERT_TRUE(bookmarks);
  EXPECT_EQ(static_cast<size_t>(AppearanceTheme::kDark),
            mode->GetSelectedIndex());
  EXPECT_EQ(base::UTF8ToUTF16(AppearancePreset::Dark().accent_color),
            accent->GetText());
  EXPECT_TRUE(bookmarks->GetChecked());
  accent->SetText(u"#00AA33");
  bookmarks->SetChecked(false);
  auto* preview = static_cast<views::Button*>(
      root->GetViewByID(CustomizeBrowserDialog::kPreviewButtonId));
  ASSERT_TRUE(preview);
  views::test::ButtonTestApi(preview).NotifyDefaultMouseClick();
  EXPECT_FALSE(root->GetViewByID(CustomizeBrowserDialog::kPreviewBookmarksId)
                   ->GetVisible());
  EXPECT_EQ(saved, profile->GetPrefs()->GetString(kAppearanceJsonPref));
  EXPECT_EQ(native_accent, theme->GetUserColor());
  widget->widget_delegate()->AsDialogDelegate()->CancelDialog();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(saved, profile->GetPrefs()->GetString(kAppearanceJsonPref));
  EXPECT_EQ(native_accent, theme->GetUserColor());
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest, DialogPresetApplyPersists) {
  auto* profile = browser()->GetProfile();
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  CustomizeBrowserDialog dialog(*profile, parent->GetNativeWindow(),
                                parent->GetNativeView());
  dialog.Show();
  auto* widget = dialog.widget_for_testing();
  auto* presets =
      static_cast<views::Combobox*>(widget->GetContentsView()->GetViewByID(
          CustomizeBrowserDialog::kPresetComboboxId));
  ASSERT_TRUE(presets);
  presets->MenuSelectionAt(2);  // Saved, Default, Dark, Compact.
  widget->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  base::RunLoop().RunUntilIdle();
  NativeAppearanceController controller(*profile);
  EXPECT_EQ(AppearancePreset::Dark(), controller.Load().preset);
  auto* theme = ThemeServiceFactory::GetForProfile(profile);
  EXPECT_EQ(ThemeService::BrowserColorScheme::kDark,
            theme->GetBrowserColorScheme());
}

IN_PROC_BROWSER_TEST_F(NativeAppearanceBrowserTest,
                       InvalidDialogInputCannotSave) {
  auto* profile = browser()->GetProfile();
  const std::string before =
      profile->GetPrefs()->GetString(kAppearanceJsonPref);
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  CustomizeBrowserDialog dialog(*profile, parent->GetNativeWindow(),
                                parent->GetNativeView());
  dialog.Show();
  auto* widget = dialog.widget_for_testing();
  auto* accent =
      static_cast<views::Textfield*>(widget->GetContentsView()->GetViewByID(
          CustomizeBrowserDialog::kAccentTextfieldId));
  ASSERT_TRUE(accent);
  accent->SetText(u"red");
  widget->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  EXPECT_FALSE(dialog.IsClosed());
  EXPECT_EQ(before, profile->GetPrefs()->GetString(kAppearanceJsonPref));
}

class NativeAppearancePolicyBrowserTest : public policy::PolicyTest {};

IN_PROC_BROWSER_TEST_F(NativeAppearancePolicyBrowserTest,
                       PolicyArrivingWhileDialogIsOpenRefusesWholeApply) {
  auto* profile = browser()->GetProfile();
  NativeAppearanceController controller(*profile);
  ASSERT_EQ(NativeAppearanceAccess::kAllowed, controller.GetAccess());
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  CustomizeBrowserDialog dialog(*profile, parent->GetNativeWindow(),
                                parent->GetNativeView());
  dialog.Show();
  const auto color =
      ThemeServiceFactory::GetForProfile(profile)->GetUserColor();
  const std::string saved = profile->GetPrefs()->GetString(kAppearanceJsonPref);
  policy::PolicyMap policies;
  SetPolicy(&policies, policy::key::kBookmarkBarEnabled, base::Value(false));
  UpdateProviderPolicy(policies);
  ASSERT_TRUE(profile->GetPrefs()->IsManagedPreference(
      bookmarks::prefs::kShowBookmarkBar));
  EXPECT_EQ(NativeAppearanceAccess::kManaged, controller.GetAccess());
  dialog.widget_for_testing()
      ->widget_delegate()
      ->AsDialogDelegate()
      ->AcceptDialog();
  EXPECT_FALSE(dialog.IsClosed());
  EXPECT_FALSE(controller.Apply(AppearancePreset::Dark()));
  EXPECT_EQ(saved, profile->GetPrefs()->GetString(kAppearanceJsonPref));
  EXPECT_EQ(color, ThemeServiceFactory::GetForProfile(profile)->GetUserColor());
  EXPECT_FALSE(
      profile->GetPrefs()->GetBoolean(bookmarks::prefs::kShowBookmarkBar));
}

}  // namespace browser
