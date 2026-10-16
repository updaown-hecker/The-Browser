#include "browser/ui/blynx_command_center.h"

#include <algorithm>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_types.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace browser {
namespace {
constexpr size_t kRows = 7;
std::u16string String(int id) {
  return l10n_util::GetStringUTF16(id);
}

bool CommandEnabled(BrowserWindowInterface& browser, int command) {
  if (!chrome::IsCommandEnabled(&browser, command)) {
    return false;
  }
  if (command == IDC_TOGGLE_VERTICAL_TABS) {
    auto* profile = browser.GetProfile();
    const auto* pref =
        profile->GetPrefs()->FindPreference(prefs::kVerticalTabsEnabled);
    return profile->IsRegularProfile() && !profile->IsOffTheRecord() &&
           !profile->IsGuestSession() && pref && pref->IsUserModifiable();
  }
  return true;
}

struct Command {
  int command;
  int label;
};
constexpr Command kCommands[] = {
    {IDC_FOCUS_LOCATION, IDS_BLYNX_COMMAND_RESULTS},
    {IDC_NEW_TAB, IDS_BLYNX_COMMAND_NEW_TAB},
    {IDC_SHOW_HISTORY, IDS_BLYNX_COMMAND_HISTORY},
    {IDC_SHOW_DOWNLOADS, IDS_BLYNX_COMMAND_DOWNLOADS},
    {IDC_SHOW_BOOKMARK_MANAGER, IDS_BLYNX_COMMAND_BOOKMARKS},
    {IDC_OPTIONS, IDS_BLYNX_COMMAND_SETTINGS},
    {IDC_TOGGLE_VERTICAL_TABS, IDS_BLYNX_COMMAND_TABS},
    {IDC_DEV_TOOLS, IDS_DEV_TOOLS},
    {IDC_TASK_MANAGER, IDS_TASK_MANAGER},
    {IDC_MANAGE_EXTENSIONS, IDS_EXTENSIONS_SUBMENU_MANAGE_EXTENSIONS_ITEM},
    {IDC_VIEW_PASSWORDS, IDS_PASSWORD_MANAGER_MANAGE_PASSWORDS_BUTTON},
};
}  // namespace

class BlynxCommandCenter::Delegate : public views::BubbleDialogDelegate,
                                     public views::TextfieldController {
 public:
  Delegate(BrowserWindowInterface& browser, views::View* anchor)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::FLOAT),
        browser_(browser.GetWeakPtr()) {
    SetTitle(IDS_BLYNX_COMMAND_TITLE);
    SetShowTitle(false);
    SetShowCloseButton(false);
    SetButtons(0);
    set_close_on_deactivate(true);
    set_highlight_button_when_shown(false);
    set_margins(gfx::Insets());
    set_corner_radius(20);
    const int width = std::clamp(
        anchor->GetWidget()->GetWindowBoundsInScreen().width() - 96, 240, 560);
    auto root = std::make_unique<views::View>();
    root->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(20), 6));
    auto* heading = root->AddChildView(
        std::make_unique<views::Label>(String(IDS_BLYNX_COMMAND_TITLE)));
    heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading->SetTextStyle(views::style::STYLE_CAPTION);
    search_ = root->AddChildView(std::make_unique<views::Textfield>());
    search_->SetPreferredSize(gfx::Size(width, 48));
    search_->SetFontList(search_->GetFontList().DeriveWithSizeDelta(3));
    search_->SetPlaceholderText(String(IDS_BLYNX_COMMAND_SEARCH));
    search_->GetViewAccessibility().SetName(String(IDS_BLYNX_COMMAND_SEARCH));
    search_->SetController(this);
    for (size_t i = 0; i < kRows; ++i) {
      auto* row = root->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&Delegate::Choose, base::Unretained(this), i),
          u""));
      row->SetPreferredSize(gfx::Size(width, 36));
      row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      row->SetCornerRadius(12);
      row->SetCustomPadding(gfx::Insets::VH(8, 12));
      rows_.push_back(row);
    }
    auto* hint = root->AddChildView(
        std::make_unique<views::Label>(String(IDS_BLYNX_COMMAND_HINT)));
    hint->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    hint->SetTextStyle(views::style::STYLE_CAPTION);
    SetContentsView(std::move(root));
    search_subscription_ = search_->AddTextChangedCallback(
        base::BindRepeating(&Delegate::Refresh, base::Unretained(this)));
    Refresh();
  }
  // Close callbacks dispose of the Widget before this window-owned delegate.
  void PrepareToClose() {
    search_subscription_ = {};
    if (search_) {
      search_->SetController(nullptr);
      search_ = nullptr;
    }
    rows_.clear();
  }
  gfx::Rect GetBubbleBounds() override {
    const auto size = views::BubbleDialogDelegate::GetBubbleBounds().size();
    auto bounds = anchor_widget()->GetWindowBoundsInScreen();
    bounds.ClampToCenteredSize(size);
    return bounds;
  }
  views::View* GetInitiallyFocusedView() override { return search_; }

 private:
  struct Result {
    std::u16string label;
    int command = 0;
    base::WeakPtr<content::WebContents> tab;
    std::u16string input;
  };

  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed || event.is_char() ||
        search_->IsIMEComposing()) {
      return false;
    }
    if (event.key_code() == ui::VKEY_ESCAPE ||
        (event.key_code() == ui::VKEY_SPACE && event.IsControlDown())) {
      GetWidget()->Close();
      return true;
    }
    if (event.key_code() == ui::VKEY_RETURN) {
      Choose(selected_);
      return true;
    }
    if (!matches_.empty() && (event.key_code() == ui::VKEY_DOWN ||
                              event.key_code() == ui::VKEY_UP)) {
      selected_ = event.key_code() == ui::VKEY_DOWN
                      ? (selected_ + 1) % matches_.size()
                      : (selected_ + matches_.size() - 1) % matches_.size();
      Render();
      return true;
    }
    return false;
  }

  void Refresh() {
    matches_.clear();
    std::u16string input;
    base::TrimWhitespace(search_->GetText(), base::TRIM_ALL, &input);
    const auto query = base::i18n::ToLower(input);
    auto add = [&](Result result) {
      if (matches_.size() < (query.empty() ? kRows : kRows - 1) &&
          base::i18n::ToLower(result.label).find(query) !=
              std::u16string::npos) {
        matches_.push_back(std::move(result));
      }
    };
    if (browser_) {
      for (const auto& command : kCommands) {
        if (CommandEnabled(*browser_, command.command)) {
          add({gfx::RemoveAccelerator(String(command.label)),
               command.command,
               {},
               {}});
        }
      }
      if (!query.empty()) {
        auto* tabs = browser_->GetTabStripModel();
        for (int i = 0; i < tabs->count(); ++i) {
          auto* tab = tabs->GetWebContentsAt(i);
          const auto host =
              base::UTF8ToUTF16(tab->GetLastCommittedURL().host());
          add({String(IDS_BLYNX_COMMAND_TAB) + u" · " + tab->GetTitle() +
                   (host.empty() ? u"" : u" · " + host),
               0,
               tab->GetWeakPtr(),
               {}});
        }
        matches_.push_back(
            {String(IDS_BLYNX_COMMAND_GO) + u" · " + input, 0, {}, input});
      }
    }
    selected_ = 0;
    Render();
  }

  void Render() {
    for (size_t i = 0; i < rows_.size(); ++i) {
      const bool shown = i < matches_.size();
      rows_[i]->SetVisible(shown);
      if (shown) {
        rows_[i]->SetText(matches_[i].label);
        rows_[i]->SetStyle(i == selected_ ? ui::ButtonStyle::kTonal
                                          : ui::ButtonStyle::kText);
      }
    }
    if (GetWidget()) {
      SizeToContents();
    }
  }

  void Choose(size_t index) {
    if (!browser_ || index >= matches_.size()) {
      return;
    }
    const auto result = matches_[index];
    if (result.command && !CommandEnabled(*browser_, result.command)) {
      Refresh();
      return;
    }
    if (!result.command && result.input.empty() &&
        (!result.tab || browser_->GetTabStripModel()->GetIndexOfWebContents(
                            result.tab.get()) < 0)) {
      Refresh();
      return;
    }
    auto action = base::BindOnce(
        [](base::WeakPtr<BrowserWindowInterface> browser, Result result) {
          if (!browser) {
            return;
          }
          if (result.command) {
            if (CommandEnabled(*browser, result.command)) {
              chrome::ExecuteCommand(browser.get(), result.command);
            }
          } else if (!result.input.empty()) {
            auto* window = BrowserWindow::FromBrowser(browser.get());
            auto* bar = window ? window->GetLocationBar() : nullptr;
            auto* controller = bar ? bar->GetOmniboxController() : nullptr;
            auto* model = controller ? controller->edit_model() : nullptr;
            if (model && model->CanPasteAndGo(result.input)) {
              model->PasteAndGo(result.input);
            }
          } else if (result.tab) {
            auto* tabs = browser->GetTabStripModel();
            const int index = tabs->GetIndexOfWebContents(result.tab.get());
            if (index >= 0) {
              tabs->ActivateTabAt(index);
            }
          }
        },
        browser_, result);
    auto runner = base::SingleThreadTaskRunner::GetCurrentDefault();
    // Destroy the popover before navigating or opening another native UI.
    GetWidget()->Close();
    runner->PostTask(FROM_HERE, std::move(action));
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::CallbackListSubscription search_subscription_;
  std::vector<Result> matches_;
  size_t selected_ = 0;
  raw_ptr<views::Textfield> search_ = nullptr;
  std::vector<raw_ptr<views::MdTextButton>> rows_;
};

BlynxCommandCenter::BlynxCommandCenter(BrowserWindowInterface& browser,
                                       gfx::NativeWindow context,
                                       gfx::NativeView parent)
    : delegate_(std::make_unique<Delegate>(
          browser,
          views::Widget::GetWidgetForNativeView(parent)->GetRootView())) {
  widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate_.get(),
      base::BindOnce([](BlynxCommandCenter* owner,
                        views::Widget::ClosedReason) { owner->Close(); },
                     base::Unretained(this)));
}
BlynxCommandCenter::~BlynxCommandCenter() {
  Close();
}
void BlynxCommandCenter::Close() {
  delegate_->PrepareToClose();
  widget_.reset();
}
bool BlynxCommandCenter::IsClosed() const {
  return !widget_ || widget_->IsClosed();
}
void BlynxCommandCenter::Show() {
  delegate_->SizeToContents();
  widget_->Show();
  widget_->Activate();
}
}  // namespace browser
