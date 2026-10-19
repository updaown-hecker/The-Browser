#include "browser/ui/blynx_command_center.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "browser/appearance/search_appearance.h"
#include "browser/ui/blynx_assistant_panel.h"
#include "cc/paint/paint_filter.h"
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
#include "chrome/grit/branded_strings.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkPath.h"
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
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/text_utils.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/focusable_border.h"
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
using SearchOptions = std::shared_ptr<const base::DictValue>;
SkColor SearchColor(const base::DictValue& options, const char* key) {
  unsigned rgb = 0;
  base::HexStringToUInt(
      std::string_view(*options.FindDict("colors")->FindString(key)).substr(1),
      &rgb);
  return 0xff000000 | rgb;
}

bool UseSearchColors(const views::View& view) {
  return view.GetNativeTheme()->forced_colors() ==
             ui::ColorProviderKey::ForcedColors::kNone &&
         view.GetNativeTheme()->preferred_contrast() !=
             ui::NativeTheme::PreferredContrast::kMore;
}
ui::ColorId SearchSurface(const base::DictValue& options) {
  // GTK's bubble token follows the OS palette even with a widget override.
  return *options.FindString("theme") == "system" ? ui::kColorBubbleBackground
                                                  : ui::kColorSysSurface;
}
bool AnimateSearch(const views::View& view) {
  return UseSearchColors(view) && gfx::Animation::ShouldRenderRichAnimation() &&
         !gfx::Animation::PrefersReducedMotion();
}

// Localized translucent highlights leave quiet gaps between the moving beams.
sk_sp<cc::PaintShader> SearchGradient(const gfx::RectF& bounds,
                                      float phase,
                                      const base::DictValue& options) {
  std::array<SkColor, 4> palette = {0xff20bfdf, 0xff8cd938, 0xffe6cc40,
                                    0xffad80d9};
  const auto& variant = *options.FindString("palette");
  if (variant == "mono") {
    palette = {0xffe0e5ec, 0xff8795a8, 0xfff3f5f8, 0xffaeb7c5};
  }
  if (variant == "ocean") {
    palette = {0xff22d3ee, 0xff0891b2, 0xff3b82f6, 0xff67e8f9};
  }
  if (variant == "sunset") {
    palette = {0xfffb7185, 0xfffb923c, 0xfffbbf24, 0xffc084fc};
  }
  if (variant == "custom") {
    palette = {SearchColor(options, "beam_1"), SearchColor(options, "beam_2"),
               SearchColor(options, "beam_3"), SearchColor(options, "beam_4")};
  }
  auto tone = [&](int i, float alpha) {
    auto color = SkColor4f::FromColor(palette[i]);
    color.fA = alpha;
    return color;
  };
  const std::array<SkColor4f, 13> colors = {
      tone(0, 0),    tone(0, .8f), tone(0, 0),   tone(1, 0), tone(1, .9f),
      tone(1, 0),    tone(2, 0),   tone(2, .8f), tone(2, 0), tone(3, 0),
      tone(3, .65f), tone(3, 0),   tone(0, 0)};
  constexpr std::array<SkScalar, 13> stops = {
      0, .06f, .15f, .24f, .31f, .40f, .49f, .56f, .65f, .74f, .81f, .90f, 1};
  SkMatrix rotation;
  rotation.setRotate(phase, bounds.CenterPoint().x(), bounds.CenterPoint().y());
  return cc::PaintShader::MakeSweepGradient(
      bounds.CenterPoint().x(), bounds.CenterPoint().y(), colors.data(),
      stops.data(), colors.size(), SkTileMode::kClamp, 0, 360,
      SkGradient::Interpolation(), 0, &rotation);
}

enum class BeamStyle { kOutside, kInside, kLine, kMedium, kSmall };
BeamStyle SearchStyle(const base::DictValue& options, const char* key) {
  const auto& name = *options.FindString(key);
  if (name == "pulse-outside") {
    return BeamStyle::kOutside;
  }
  if (name == "pulse-inner") {
    return BeamStyle::kInside;
  }
  if (name == "line") {
    return BeamStyle::kLine;
  }
  return name == "sm" ? BeamStyle::kSmall : BeamStyle::kMedium;
}
bool OutsideSpace(BeamStyle style) {
  return style == BeamStyle::kOutside || style == BeamStyle::kMedium ||
         style == BeamStyle::kSmall;
}
bool CustomSearchColors(const views::View& view,
                        const base::DictValue& options) {
  return UseSearchColors(view) && options.FindBool("custom_colors").value();
}

void PaintBeam(gfx::Canvas* canvas,
               const gfx::RectF& bounds,
               float radius,
               float phase,
               float strength,
               BeamStyle style,
               const base::DictValue& options) {
  strength *= options.FindDouble("strength").value();
  if (strength <= 0) {
    return;
  }
  gfx::ScopedCanvas scoped(canvas);
  if (style != BeamStyle::kMedium && style != BeamStyle::kSmall) {
    canvas->ClipPath(SkPath::RRect(gfx::RectFToSkRect(bounds), radius, radius),
                     true,
                     style == BeamStyle::kOutside ? SkClipOp::kDifference
                                                  : SkClipOp::kIntersect);
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setShader(SearchGradient(bounds, phase, options));
  const float pulse =
      (style == BeamStyle::kOutside || style == BeamStyle::kInside)
          ? .78f + .22f * std::sin(phase * .034906585f)
          : 1;
  const float blur = radius < 15 && style == BeamStyle::kSmall ? 1.0f
                     : radius < 15 && OutsideSpace(style)      ? 2.0f
                     : style == BeamStyle::kOutside            ? 7.0f
                     : style == BeamStyle::kInside             ? 3.0f
                     : style == BeamStyle::kMedium             ? 5.0f
                     : style == BeamStyle::kSmall              ? 2.0f
                                                               : 1.0f;
  flags.setStrokeWidth(style == BeamStyle::kSmall     ? 2.0f
                       : style == BeamStyle::kOutside ? 5.0f
                                                      : 4.0f);
  flags.setAlphaf(strength * pulse * .7f);
  flags.setImageFilter(
      sk_make_sp<cc::BlurPaintFilter>(blur, blur, SkTileMode::kDecal, nullptr));
  canvas->DrawRoundRect(bounds, radius, flags);
  flags.setImageFilter(nullptr);
  flags.setStrokeWidth(style == BeamStyle::kLine ? 2.2f : 1.4f);
  flags.setAlphaf(strength * .85f);
  canvas->DrawRoundRect(bounds, radius, flags);
}

class SearchInputBorder : public views::FocusableBorder {
 public:
  explicit SearchInputBorder(SearchOptions options)
      : options_(std::move(options)) {
    SetInsets(gfx::Insets::VH(12, 14));
    SetCornerRadius(8);
  }
  void SetPhase(float phase) { phase_ = phase; }
  void Paint(const views::View& view, gfx::Canvas* canvas) override {
    SetColor(CustomSearchColors(view, *options_)
                 ? ui::ColorVariant(SearchColor(*options_, "outline"))
                 : ui::ColorVariant(ui::kColorFocusableBorderUnfocused));
    views::FocusableBorder::Paint(view, canvas);
    if (UseSearchColors(view)) {
      gfx::RectF bounds(view.GetLocalBounds());
      const auto style = SearchStyle(*options_, "input_style");
      bounds.Inset(OutsideSpace(style) ? 7.0f : .5f);
      PaintBeam(canvas, bounds, 8, phase_, view.HasFocus() ? 1.0f : .45f, style,
                *options_);
    }
  }

 private:
  SearchOptions options_;
  float phase_ = 0;
};

class SearchTextfield : public views::Textfield {
  METADATA_HEADER(SearchTextfield, views::Textfield)
 public:
  explicit SearchTextfield(SearchOptions options)
      : options_(std::move(options)) {}
  void OnThemeChanged() override {
    views::Textfield::OnThemeChanged();
    SetPlaceholderText(
        CustomSearchColors(*this, *options_)
            ? u""
            : l10n_util::GetStringUTF16(IDS_BLYNX_COMMAND_SEARCH));
    SetBackgroundColor(
        CustomSearchColors(*this, *options_)
            ? std::optional<ui::ColorVariant>(SearchColor(*options_, "input"))
            : std::nullopt);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const bool custom = CustomSearchColors(*this, *options_);
    if (custom) {
      SetColor(SearchColor(*options_, "input_text"));
      GetRenderText()->set_selection_color(
          SearchColor(*options_, "selection_text"));
      GetRenderText()->set_selection_background_focused_color(
          SearchColor(*options_, "selection"));
    }
    views::Textfield::OnPaint(canvas);
    if (custom && GetText().empty()) {
      canvas->DrawStringRectWithFlags(
          l10n_util::GetStringUTF16(IDS_BLYNX_COMMAND_SEARCH), GetFontList(),
          SearchColor(*options_, "placeholder"),
          GetRenderText()->display_rect(),
          gfx::Canvas::TEXT_ALIGN_LEFT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
    }
  }

 private:
  SearchOptions options_;
};
BEGIN_METADATA(SearchTextfield)
END_METADATA

class SearchLabel : public views::Label {
  METADATA_HEADER(SearchLabel, views::Label)
 public:
  SearchLabel(std::u16string text, SearchOptions options, const char* role)
      : views::Label(text), options_(std::move(options)), role_(role) {}
  void OnThemeChanged() override {
    views::Label::OnThemeChanged();
    SetEnabledColor(CustomSearchColors(*this, *options_)
                        ? ui::ColorVariant(SearchColor(*options_, role_))
                        : ui::ColorVariant(
                              UseSearchColors(*this) &&
                                      *options_->FindString("theme") != "system"
                                  ? ui::kColorSysOnSurface
                                  : ui::kColorLabelForeground));
  }

 private:
  SearchOptions options_;
  const char* role_;
};
BEGIN_METADATA(SearchLabel)
END_METADATA

class SearchResultButton : public views::MdTextButton {
  METADATA_HEADER(SearchResultButton, views::MdTextButton)

 public:
  SearchResultButton(PressedCallback callback, SearchOptions options)
      : views::MdTextButton(std::move(callback), u""),
        options_(std::move(options)) {
    color_hover_.SetSlideDuration(base::Milliseconds(180));
  }
  void Select(bool selected) {
    selected_ = selected;
    SetStyle(selected ? ui::ButtonStyle::kTonal : ui::ButtonStyle::kText);
    ApplyColors();
  }
  void OnThemeChanged() override {
    views::MdTextButton::OnThemeChanged();
    ApplyColors();
  }
  void AdvancePhase(float phase) {
    phase_ = phase;
    if (GetVisible() && color_hover_.GetCurrentValue() > 0) {
      SchedulePaint();
    }
  }

  void StateChanged(ButtonState old_state) override {
    views::MdTextButton::StateChanged(old_state);
    ApplyColors();
    const bool active =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (!AnimateSearch(*this) || !options_->FindBool("active").value()) {
      color_hover_.Reset(active ? 1 : 0);
      SchedulePaint();
    } else if (active) {
      color_hover_.Show();
    } else {
      color_hover_.Hide();
    }
  }

 private:
  void ApplyColors() {
    const bool custom = CustomSearchColors(*this, *options_);
    const bool hover =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SetBgColorOverrideDeprecated(custom
                                     ? std::optional<SkColor>(SearchColor(
                                           *options_, hover       ? "hover"
                                                      : selected_ ? "selected"
                                                                  : "row"))
                                     : std::nullopt);
    SetEnabledTextColors(custom ? std::optional<ui::ColorVariant>(SearchColor(
                                      *options_, hover       ? "hover_text"
                                                 : selected_ ? "selected_text"
                                                             : "row_text"))
                                : std::nullopt);
    SetStrokeColorOverrideDeprecated(
        custom ? std::optional<SkColor>(SearchColor(*options_, "outline"))
               : std::nullopt);
  }
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
    const auto style = SearchStyle(*options_, "hover_style");
    bounds.Inset(OutsideSpace(style) ? 7.0f : 1.5f);
    PaintBeam(canvas, bounds, 11, phase_, amount, style, *options_);
  }
  SearchOptions options_;
  bool selected_ = false;
  float phase_ = 0;
  gfx::SlideAnimation color_hover_{this};
};

BEGIN_METADATA(SearchResultButton)
END_METADATA

// Paint inside reserved transparent frame space so native bounds include the
// halo.
class SearchGlowBorder : public views::BubbleBorder {
 public:
  explicit SearchGlowBorder(SearchOptions options)
      : views::BubbleBorder(FLOAT, DIALOG_SHADOW),
        options_(std::move(options)) {
    set_insets(gfx::Insets(24));
    set_rounded_corners(gfx::RoundedCornersF(22));
  }

  void OnViewThemeChanged(views::View* view) override {
    SetColor(CustomSearchColors(*view, *options_)
                 ? ui::ColorVariant(SearchColor(*options_, "surface"))
                 : ui::ColorVariant(UseSearchColors(*view)
                                        ? SearchSurface(*options_)
                                        : ui::kColorBubbleBackground));
  }
  void SetPhase(float phase) { phase_ = phase; }

  void Paint(const views::View& view, gfx::Canvas* canvas) override {
    views::BubbleBorder::Paint(view, canvas);
    if (!UseSearchColors(view)) {
      return;
    }
    gfx::RectF rim(view.GetLocalBounds());
    rim.Inset(gfx::InsetsF(GetInsets()));
    if (CustomSearchColors(view, *options_)) {
      cc::PaintFlags outline;
      outline.setAntiAlias(true);
      outline.setStyle(cc::PaintFlags::kStroke_Style);
      outline.setStrokeWidth(1);
      outline.setColor(SearchColor(*options_, "outline"));
      canvas->DrawRoundRect(rim, 22, outline);
    }
    PaintBeam(canvas, rim, 22, phase_, .8f,
              SearchStyle(*options_, "outer_style"), *options_);
  }

 private:
  SearchOptions options_;
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
                                     public gfx::AnimationDelegate,
                                     public ui::NativeThemeObserver {
 public:
  Delegate(BrowserWindowInterface& browser, views::View* anchor)
      : views::BubbleDialogDelegate(anchor, views::BubbleBorder::FLOAT),
        browser_(browser.GetWeakPtr()),
        options_(std::make_shared<base::DictValue>(
            LoadSearchAppearance(*browser.GetProfile()->GetPrefs()))) {
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
    auto* heading = root->AddChildView(std::make_unique<SearchLabel>(
        String(IDS_BLYNX_COMMAND_TITLE), options_, "heading"));
    heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading->SetTextStyle(views::style::STYLE_BODY_3_EMPHASIS);
    search_ = root->AddChildView(std::make_unique<SearchTextfield>(options_));
    search_->SetPreferredSize(gfx::Size(width, 54));
    search_->SetFontList(search_->GetFontList().DeriveWithSizeDelta(4));
    search_->SetPlaceholderText(String(IDS_BLYNX_COMMAND_SEARCH));
    search_->GetViewAccessibility().SetName(String(IDS_BLYNX_COMMAND_SEARCH));
    search_->SetController(this);
    auto input_border = std::make_unique<SearchInputBorder>(options_);
    input_beam_ = input_border.get();
    search_->SetBorder(std::move(input_border));
    views::FocusRing::Install(search_);
    views::FocusRing::Get(search_)->SetOutsetFocusRingDisabled(true);
    // The beam is the focus decoration in color mode; retain the native ring
    // for forced colors and increased contrast.
    views::FocusRing::Get(search_)->SetHasFocusPredicate(base::BindRepeating(
        [](SearchOptions options, const views::View* view) {
          return view->HasFocus() &&
                 (!UseSearchColors(*view) ||
                  options->FindDouble("strength").value() == 0);
        },
        options_));
    for (size_t i = 0; i < kRows; ++i) {
      auto* row = root->AddChildView(std::make_unique<SearchResultButton>(
          base::BindRepeating(&Delegate::Choose, base::Unretained(this), i),
          options_));
      row->SetPreferredSize(gfx::Size(width, 36));
      row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      row->SetCornerRadius(12);
      row->SetCustomPadding(gfx::Insets::VH(8, 12));
      rows_.push_back(row);
    }
    auto* hint = root->AddChildView(std::make_unique<SearchLabel>(
        String(IDS_BLYNX_COMMAND_HINT), options_, "hint"));
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
    auto border = std::make_unique<SearchGlowBorder>(options_);
    border->SetColor(background_color());
    glow_ = border.get();
    static_cast<views::BubbleFrameView*>(frame.get())
        ->SetBubbleBorder(std::move(border));
    return frame;
  }

  void StartAnimations() {
    const auto& theme = *options_->FindString("theme");
    GetWidget()->SetColorModeOverride(
        theme == "system"
            ? std::nullopt
            : std::optional<ui::ColorProviderKey::ColorMode>(
                  theme == "light" ? ui::ColorProviderKey::ColorMode::kLight
                                   : ui::ColorProviderKey::ColorMode::kDark));
    if (!theme_observation_.IsObserving()) {
      theme_observation_.Observe(GetBubbleFrameView()->GetNativeTheme());
    }
    UpdateSurface();
    if (options_->FindBool("active").value() &&
        options_->FindDouble("strength").value() > 0 &&
        AnimateSearch(*GetBubbleFrameView())) {
      animation_start_ = base::TimeTicks::Now();
      glow_animation_.Start();
    }
  }

  // Close callbacks dispose of the Widget before this window-owned delegate.
  void PrepareToClose() {
    theme_observation_.Reset();
    glow_animation_.Stop();
    glow_ = nullptr;
    input_beam_ = nullptr;
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
  void UpdateSurface() {
    SetBackgroundColor(
        CustomSearchColors(*GetBubbleFrameView(), *options_)
            ? ui::ColorVariant(SearchColor(*options_, "surface"))
            : ui::ColorVariant(UseSearchColors(*GetBubbleFrameView())
                                   ? SearchSurface(*options_)
                                   : ui::kColorBubbleBackground));
  }
  void OnNativeThemeUpdated(ui::NativeTheme*) override { UpdateSurface(); }
  void AnimationProgressed(const gfx::Animation*) override {
    auto* frame = GetBubbleFrameView();
    if (!glow_ || !GetWidget()->IsVisible() || !AnimateSearch(*frame)) {
      glow_animation_.Stop();
      return;
    }
    const float phase =
        std::fmod((base::TimeTicks::Now() - animation_start_).InSecondsF() *
                      360.0 / options_->FindDouble("duration").value(),
                  360.0);
    glow_->SetPhase(phase);
    input_beam_->SetPhase(phase + 90);
    search_->SchedulePaint();
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
    bool assistant = false;
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
        if (browser_->GetProfile()->IsRegularProfile() &&
            !browser_->GetProfile()->IsOffTheRecord()) {
          add({String(IDS_BLYNX_AI_TITLE) + u" · Ctrl+Alt+A", 0, {}, {}, true});
        }
        add({String(IDS_BLYNX_SEARCH_CUSTOM_TITLE),
             0,
             {},
             u"blynx://settings/appearance"});
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
        rows_[i]->Select(i == selected_);
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
    if (!result.assistant && !result.command && result.input.empty() &&
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
          if (result.assistant) {
            ToggleBlynxAssistant(*browser);
          } else if (result.command) {
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
  SearchOptions options_;
  base::CallbackListSubscription search_subscription_;
  std::vector<Result> matches_;
  size_t selected_ = 0;
  raw_ptr<views::Textfield> search_ = nullptr;
  std::vector<raw_ptr<SearchResultButton>> rows_;
  raw_ptr<SearchGlowBorder> glow_ = nullptr;
  raw_ptr<SearchInputBorder> input_beam_ = nullptr;
  base::TimeTicks animation_start_;
  gfx::InfiniteAnimation glow_animation_{this, base::Milliseconds(33)};
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      theme_observation_{this};
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
