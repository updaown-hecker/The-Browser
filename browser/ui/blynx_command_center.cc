#include "browser/ui/blynx_command_center.h"

#include <algorithm>
#include <array>
#include <cmath>
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
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
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
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/infinite_animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/text_utils.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
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

bool UseSearchColors(const views::View& view) {
  return view.GetNativeTheme()->forced_colors() ==
             ui::ColorProviderKey::ForcedColors::kNone &&
         view.GetNativeTheme()->preferred_contrast() !=
             ui::NativeTheme::PreferredContrast::kMore;
}
bool AnimateSearch(const views::View& view) {
  return UseSearchColors(view) && gfx::Animation::ShouldRenderRichAnimation() &&
         !gfx::Animation::PrefersReducedMotion();
}

sk_sp<cc::PaintShader> SearchGradient(const gfx::RectF& bounds, float phase) {
  const std::array<SkColor4f, 5> colors = {
      SkColor4f::FromColor(SkColorSetRGB(0x42, 0x85, 0xf4)),
      SkColor4f::FromColor(SkColorSetRGB(0xea, 0x43, 0x35)),
      SkColor4f::FromColor(SkColorSetRGB(0xfb, 0xbc, 0x05)),
      SkColor4f::FromColor(SkColorSetRGB(0x34, 0xa8, 0x53)),
      SkColor4f::FromColor(SkColorSetRGB(0x42, 0x85, 0xf4)),
  };
  SkMatrix rotation;
  rotation.setRotate(phase, bounds.CenterPoint().x(), bounds.CenterPoint().y());
  return cc::PaintShader::MakeSweepGradient(
      bounds.CenterPoint().x(), bounds.CenterPoint().y(), colors.data(),
      nullptr, colors.size(), SkTileMode::kClamp, 0, 360,
      SkGradient::Interpolation(), 0, &rotation);
}

class SearchResultButton : public views::MdTextButton {
  METADATA_HEADER(SearchResultButton, views::MdTextButton)

 public:
  explicit SearchResultButton(PressedCallback callback)
      : views::MdTextButton(std::move(callback), u"") {
    color_hover_.SetSlideDuration(base::Milliseconds(180));
  }
  void AdvancePhase(float phase) {
    phase_ = phase;
    if (GetVisible() && color_hover_.GetCurrentValue() > 0) {
      SchedulePaint();
    }
  }

  void StateChanged(ButtonState old_state) override {
    views::MdTextButton::StateChanged(old_state);
    const bool active =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (!AnimateSearch(*this)) {
      color_hover_.Reset(active ? 1 : 0);
      SchedulePaint();
    } else if (active) {
      color_hover_.Show();
    } else {
      color_hover_.Hide();
    }
  }

 private:
  void AnimationProgressed(const gfx::Animation* animation) override {
    views::MdTextButton::AnimationProgressed(animation);
    SchedulePaint();
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    views::MdTextButton::PaintButtonContents(canvas);
    const float amount = color_hover_.GetCurrentValue();
    if (!UseSearchColors(*this) || amount <= 0) {
      return;
    }
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(1.5f);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setShader(SearchGradient(bounds, phase_));
    flags.setAlphaf(0.12f * amount);
    canvas->DrawRoundRect(bounds, 11, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.4f);
    flags.setAlphaf(0.8f * amount);
    canvas->DrawRoundRect(bounds, 11, flags);
  }
  float phase_ = 0;
  gfx::SlideAnimation color_hover_{this};
};

BEGIN_METADATA(SearchResultButton)
END_METADATA

// Paint inside reserved transparent frame space so native bounds include the
// halo.
class SearchGlowBorder : public views::BubbleBorder {
 public:
  SearchGlowBorder() : views::BubbleBorder(FLOAT, DIALOG_SHADOW) {
    set_insets(gfx::Insets(16));
    set_rounded_corners(gfx::RoundedCornersF(22));
  }

  void SetPhase(float phase) { phase_ = phase; }

  void Paint(const views::View& view, gfx::Canvas* canvas) override {
    views::BubbleBorder::Paint(view, canvas);
    if (!UseSearchColors(view)) {
      return;
    }
    gfx::RectF rim(view.GetLocalBounds());
    rim.Inset(gfx::InsetsF(GetInsets()));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setShader(SearchGradient(rim, phase_));
    // Nested translucent strokes give a smooth falloff without an offscreen
    // blur surface; the final narrow stroke keeps the luminous edge crisp.
    for (int spread = 14; spread >= 1; --spread) {
      flags.setStrokeWidth(2.0f * spread);
      flags.setAlphaf(0.012f + 0.026f * (14 - spread) / 13.0f);
      canvas->DrawRoundRect(rim, 22, flags);
    }
    flags.setStrokeWidth(1.8f);
    flags.setAlphaf(0.92f);
    canvas->DrawRoundRect(rim, 22, flags);
  }

 private:
  float phase_ = 0;
};

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
                                     public views::TextfieldController,
                                     public gfx::AnimationDelegate {
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
    set_corner_radius(22);
    const int width = std::clamp(
        anchor->GetWidget()->GetWindowBoundsInScreen().width() - 96, 240, 560);
    auto root = std::make_unique<views::View>();
    root->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(22), 7));
    auto* heading = root->AddChildView(
        std::make_unique<views::Label>(String(IDS_BLYNX_COMMAND_TITLE)));
    heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading->SetTextStyle(views::style::STYLE_BODY_3_EMPHASIS);
    search_ = root->AddChildView(std::make_unique<views::Textfield>());
    search_->SetPreferredSize(gfx::Size(width, 54));
    search_->SetFontList(search_->GetFontList().DeriveWithSizeDelta(4));
    search_->SetPlaceholderText(String(IDS_BLYNX_COMMAND_SEARCH));
    search_->GetViewAccessibility().SetName(String(IDS_BLYNX_COMMAND_SEARCH));
    search_->SetController(this);
    for (size_t i = 0; i < kRows; ++i) {
      auto* row = root->AddChildView(std::make_unique<SearchResultButton>(
          base::BindRepeating(&Delegate::Choose, base::Unretained(this), i)));
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
  std::unique_ptr<views::FrameView> CreateFrameView(
      views::Widget* widget) override {
    auto frame = views::BubbleDialogDelegate::CreateFrameView(widget);
    auto border = std::make_unique<SearchGlowBorder>();
    border->SetColor(background_color());
    glow_ = border.get();
    static_cast<views::BubbleFrameView*>(frame.get())
        ->SetBubbleBorder(std::move(border));
    return frame;
  }

  void StartAnimations() {
    if (AnimateSearch(*GetBubbleFrameView())) {
      animation_start_ = base::TimeTicks::Now();
      glow_animation_.Start();
    }
  }

  // Close callbacks dispose of the Widget before this window-owned delegate.
  void PrepareToClose() {
    glow_animation_.Stop();
    glow_ = nullptr;
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
  void AnimationProgressed(const gfx::Animation*) override {
    auto* frame = GetBubbleFrameView();
    if (!glow_ || !GetWidget()->IsVisible() || !AnimateSearch(*frame)) {
      glow_animation_.Stop();
      return;
    }
    const float phase = std::fmod(
        (base::TimeTicks::Now() - animation_start_).InSecondsF() * 36.0, 360.0);
    glow_->SetPhase(phase);
    frame->SchedulePaint();
    for (auto& row : rows_) {
      row->AdvancePhase(phase);
    }
  }
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
  std::vector<raw_ptr<SearchResultButton>> rows_;
  raw_ptr<SearchGlowBorder> glow_ = nullptr;
  base::TimeTicks animation_start_;
  gfx::InfiniteAnimation glow_animation_{this, base::Milliseconds(33)};
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
  delegate_->StartAnimations();
}
}  // namespace browser
