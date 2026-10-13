#include "browser/ui/customize_browser_dialog.h"

#include <array>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "browser/ui/native_appearance_controller.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_delegate.h"

namespace browser {
namespace {

std::u16string String(int id) {
  return l10n_util::GetStringUTF16(id);
}

std::unique_ptr<ui::SimpleComboboxModel> Model(
    std::initializer_list<int> messages) {
  std::vector<ui::SimpleComboboxModel::Item> items;
  for (int id : messages) {
    items.emplace_back(String(id));
  }
  return std::make_unique<ui::SimpleComboboxModel>(std::move(items));
}

views::Label* AddLabel(views::View* parent, int id) {
  auto* label =
      parent->AddChildView(std::make_unique<views::Label>(String(id)));
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SizeToFit(420);
  return label;
}

int AccessMessage(NativeAppearanceAccess access) {
  switch (access) {
    case NativeAppearanceAccess::kAllowed:
      return IDS_BROWSER_FINAL_CUSTOMIZE_READY;
    case NativeAppearanceAccess::kReadOnlyProfile:
      return IDS_BROWSER_FINAL_CUSTOMIZE_READ_ONLY;
    case NativeAppearanceAccess::kManaged:
      return IDS_BROWSER_FINAL_CUSTOMIZE_MANAGED;
    case NativeAppearanceAccess::kUnsupportedStoredVersion:
      return IDS_BROWSER_FINAL_CUSTOMIZE_UNSUPPORTED;
    case NativeAppearanceAccess::kUnavailable:
      return IDS_BROWSER_FINAL_CUSTOMIZE_UNAVAILABLE;
  }
  return IDS_BROWSER_FINAL_CUSTOMIZE_UNAVAILABLE;
}

}  // namespace

class CustomizeBrowserDialog::Delegate : public views::DialogDelegate,
                                         public views::WidgetObserver,
                                         public views::TextfieldController {
 public:
  explicit Delegate(Profile& profile) : controller_(profile) {
    const auto saved = controller_.Load();
    saved_ = saved.preset;
    staged_ = saved_;

    SetTitle(IDS_BROWSER_FINAL_CUSTOMIZE_TITLE);
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(true);
    SetButtonLabel(ui::mojom::DialogButton::kOk,
                   String(IDS_BROWSER_FINAL_CUSTOMIZE_APPLY));
    SetButtonLabel(ui::mojom::DialogButton::kCancel,
                   String(IDS_BROWSER_FINAL_CUSTOMIZE_CANCEL));
    SetAcceptCallbackWithClose(
        base::BindRepeating(&Delegate::Apply, base::Unretained(this)));

    auto contents = std::make_unique<views::View>();
    auto* root = contents.get();
    root->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 8));
    AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_DESCRIPTION);
    AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_PRESET_LABEL);
    presets_ = root->AddChildView(std::make_unique<views::Combobox>(Model({
        IDS_BROWSER_FINAL_CUSTOMIZE_SAVED,
        IDS_BROWSER_FINAL_CUSTOMIZE_PRESET_DEFAULT,
        IDS_BROWSER_FINAL_CUSTOMIZE_PRESET_DARK,
        IDS_BROWSER_FINAL_CUSTOMIZE_PRESET_COMPACT,
    })));
    presets_->SetID(kPresetComboboxId);
    presets_->GetViewAccessibility().SetName(
        String(IDS_BROWSER_FINAL_CUSTOMIZE_PRESET_LABEL));
    presets_->SetCallback(
        base::BindRepeating(&Delegate::SelectPreset, base::Unretained(this)));

    AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_THEME_LABEL);
    theme_ = root->AddChildView(std::make_unique<views::Combobox>(Model({
        IDS_BROWSER_FINAL_CUSTOMIZE_THEME_SYSTEM,
        IDS_BROWSER_FINAL_CUSTOMIZE_THEME_LIGHT,
        IDS_BROWSER_FINAL_CUSTOMIZE_THEME_DARK,
    })));
    theme_->SetID(kThemeComboboxId);
    theme_->GetViewAccessibility().SetName(
        String(IDS_BROWSER_FINAL_CUSTOMIZE_THEME_LABEL));
    theme_->SetCallback(
        base::BindRepeating(&Delegate::Preview, base::Unretained(this)));
    AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_ACCENT_LABEL);
    accent_ = root->AddChildView(std::make_unique<views::Textfield>());
    accent_->SetID(kAccentTextfieldId);
    accent_->SetDefaultWidthInChars(7);
    accent_->GetViewAccessibility().SetName(
        String(IDS_BROWSER_FINAL_CUSTOMIZE_ACCENT_LABEL));
    accent_->SetController(this);

    bookmarks_ = root->AddChildView(std::make_unique<views::Checkbox>(
        String(IDS_BROWSER_FINAL_CUSTOMIZE_BOOKMARKS)));
    bookmarks_->SetID(kBookmarksCheckboxId);
    bookmarks_->SetCallback(
        base::BindRepeating(&Delegate::Preview, base::Unretained(this)));
    vertical_tabs_ = root->AddChildView(
        std::make_unique<views::Checkbox>(String(IDS_SWITCH_TO_VERTICAL_TAB)));
    vertical_tabs_->SetID(kVerticalTabsCheckboxId);
    vertical_tabs_->SetChecked(controller_.VerticalTabsEnabled());
    auto* preview_button =
        root->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&Delegate::Preview, base::Unretained(this)),
            String(IDS_BROWSER_FINAL_CUSTOMIZE_PREVIEW)));
    preview_button->SetID(kPreviewButtonId);

    AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_PREVIEW_NOTICE);
    preview_ = root->AddChildView(std::make_unique<views::View>());
    preview_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));
    preview_mode_ =
        AddLabel(preview_, IDS_BROWSER_FINAL_CUSTOMIZE_THEME_SYSTEM);
    preview_accent_ =
        AddLabel(preview_, IDS_BROWSER_FINAL_CUSTOMIZE_PREVIEW_ACCENT);
    preview_bookmarks_ =
        AddLabel(preview_, IDS_BROWSER_FINAL_CUSTOMIZE_PREVIEW_BOOKMARKS);
    preview_bookmarks_->SetID(kPreviewBookmarksId);
    AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_UNIMPLEMENTED);

    status_ = AddLabel(root, IDS_BROWSER_FINAL_CUSTOMIZE_READY);
    const auto access = controller_.GetAccess();
    status_->SetText(String(AccessMessage(access)));
    if (access == NativeAppearanceAccess::kAllowed &&
        saved.state == AppearancePrefState::kInvalid) {
      status_->SetText(String(IDS_BROWSER_FINAL_CUSTOMIZE_INVALID_SAVED));
    }
    const bool writable = access == NativeAppearanceAccess::kAllowed;
    presets_->SetEnabled(writable);
    theme_->SetEnabled(writable);
    accent_->SetEnabled(writable);
    bookmarks_->SetEnabled(writable);
    vertical_tabs_->SetEnabled(writable);
    preview_button->SetEnabled(writable);
    SetButtonEnabled(ui::mojom::DialogButton::kOk, writable);
    PopulateControls();
    RenderPreview(staged_);
    // Keep Apply/Cancel reachable even on smaller desktop displays.
    auto scroll = std::make_unique<views::ScrollView>();
    scroll->ClipHeightTo(300, 500);
    scroll->SetContents(std::move(contents));
    SetContentsView(std::move(scroll));
  }

  void ObserveWidget(views::Widget* widget) { observation_.Observe(widget); }

 private:
  void ContentsChanged(views::Textfield*, const std::u16string&) override {
    Preview();
  }

  void OnWidgetDestroying(views::Widget*) override {
    // The Widget owns the contents after Init; release all borrowed View
    // pointers before it destroys that tree, even when closed by its parent.
    observation_.Reset();
    presets_ = nullptr;
    theme_ = nullptr;
    accent_->SetController(nullptr);
    accent_ = nullptr;
    bookmarks_ = nullptr;
    vertical_tabs_ = nullptr;
    preview_ = nullptr;
    preview_mode_ = nullptr;
    preview_accent_ = nullptr;
    preview_bookmarks_ = nullptr;
    status_ = nullptr;
  }

  void PopulateControls() {
    theme_->SetSelectedIndex(static_cast<size_t>(staged_.theme));
    accent_->SetText(base::UTF8ToUTF16(staged_.accent_color));
    bookmarks_->SetChecked(staged_.show_bookmarks_bar);
  }

  void SelectPreset() {
    const size_t index = presets_->GetSelectedIndex().value_or(0);
    staged_ = index == 0 ? saved_ : BuiltInAppearancePresets().at(index - 1);
    PopulateControls();
    Preview();
  }

  AppearancePreset Draft() const {
    auto preset = staged_;
    preset.theme =
        static_cast<AppearanceTheme>(theme_->GetSelectedIndex().value_or(0));
    preset.accent_color = base::UTF16ToUTF8(accent_->GetText());
    preset.show_bookmarks_bar = bookmarks_->GetChecked();
    if (preset != staged_) {
      // Wire metadata, not a displayed label. All UI preset names are GRIT IDs.
      preset.preset_id = "custom";
      preset.display_name = "Custom";
    }
    return preset;
  }

  void RenderPreview(const AppearancePreset& preset) {
    const bool dark =
        preset.theme == AppearanceTheme::kDark ||
        (preset.theme == AppearanceTheme::kSystem &&
         ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
             ui::NativeTheme::PreferredColorScheme::kDark);
    const SkColor background = dark ? SkColorSetRGB(32, 33, 36) : SK_ColorWHITE;
    const SkColor foreground = color_utils::GetColorWithMaxContrast(background);
    preview_->SetBackground(views::CreateSolidBackground(background));
    constexpr std::array<int, 3> kModeMessages = {
        IDS_BROWSER_FINAL_CUSTOMIZE_THEME_SYSTEM,
        IDS_BROWSER_FINAL_CUSTOMIZE_THEME_LIGHT,
        IDS_BROWSER_FINAL_CUSTOMIZE_THEME_DARK,
    };
    preview_mode_->SetText(
        String(kModeMessages[static_cast<size_t>(preset.theme)]));
    for (auto* label : {preview_mode_.get(), preview_bookmarks_.get()}) {
      label->SetAutoColorReadabilityEnabled(false);
      label->SetEnabledColor(foreground);
      label->SetBackground(views::CreateSolidBackground(background));
    }
    const SkColor accent = *AppearanceAccentColor(preset);
    preview_accent_->SetAutoColorReadabilityEnabled(false);
    preview_accent_->SetEnabledColor(
        color_utils::GetColorWithMaxContrast(accent));
    preview_accent_->SetBackground(views::CreateSolidBackground(accent));
    preview_bookmarks_->SetVisible(preset.show_bookmarks_bar);
  }

  void Preview() {
    const auto draft = Draft();
    if (!AppearanceAccentColor(draft)) {
      status_->SetText(String(IDS_BROWSER_FINAL_CUSTOMIZE_INVALID_INPUT));
      return;
    }
    RenderPreview(draft);
    status_->SetText(String(IDS_BROWSER_FINAL_CUSTOMIZE_PREVIEW_READY));
  }

  bool Apply() {
    const auto draft = Draft();
    if (!AppearanceAccentColor(draft)) {
      status_->SetText(String(IDS_BROWSER_FINAL_CUSTOMIZE_INVALID_INPUT));
      return false;
    }
    // Recheck access at click time, including policy changes while open.
    if (!controller_.Apply(draft, vertical_tabs_->GetChecked())) {
      status_->SetText(String(AccessMessage(controller_.GetAccess())));
      return false;
    }
    return true;
  }

  NativeAppearanceController controller_;
  AppearancePreset saved_;
  AppearancePreset staged_;
  raw_ptr<views::Combobox> presets_ = nullptr;
  raw_ptr<views::Combobox> theme_ = nullptr;
  raw_ptr<views::Textfield> accent_ = nullptr;
  raw_ptr<views::Checkbox> bookmarks_ = nullptr;
  raw_ptr<views::Checkbox> vertical_tabs_ = nullptr;
  raw_ptr<views::View> preview_ = nullptr;
  raw_ptr<views::Label> preview_mode_ = nullptr;
  raw_ptr<views::Label> preview_accent_ = nullptr;
  raw_ptr<views::Label> preview_bookmarks_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

CustomizeBrowserDialog::CustomizeBrowserDialog(Profile& profile,
                                               gfx::NativeWindow context,
                                               gfx::NativeView parent)
    : delegate_(std::make_unique<Delegate>(profile)),
      widget_(std::make_unique<views::Widget>()) {
  auto params = views::DialogDelegate::GetDialogWidgetInitParams(
      delegate_.get(), context, parent, gfx::Rect());
  params.ownership = views::Widget::InitParams::CLIENT_OWNS_WIDGET;
  widget_->Init(std::move(params));
  delegate_->ObserveWidget(widget_.get());
}

CustomizeBrowserDialog::~CustomizeBrowserDialog() = default;

bool CustomizeBrowserDialog::IsClosed() const {
  return widget_->IsClosed();
}

void CustomizeBrowserDialog::Show() {
  widget_->Show();
  widget_->Activate();
}

}  // namespace browser
