#include "browser/ui/blynx_assistant_panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "browser/ai/assistant_protocol.h"
#include "browser/ui/native_appearance_controller.h"
#include "build/build_config.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_extraction/content/browser/inner_text.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/tts_controller.h"
#include "content/public/browser/tts_utterance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/infinite_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"

namespace browser {
namespace {
std::u16string S(int id) {
  return l10n_util::GetStringUTF16(id);
}
bool Regular(BrowserWindowInterface* browser) {
  return browser && browser->GetProfile()->IsRegularProfile() &&
         !browser->GetProfile()->IsOffTheRecord() &&
         !browser->GetProfile()->IsGuestSession();
}
bool Editable(BrowserWindowInterface* browser) {
  if (!Regular(browser)) {
    return false;
  }
  return CanWriteAssistantConfig(*browser->GetProfile()->GetPrefs());
}
bool Colorful(const views::View& view) {
  return view.GetNativeTheme()->forced_colors() ==
             ui::ColorProviderKey::ForcedColors::kNone &&
         view.GetNativeTheme()->preferred_contrast() !=
             ui::NativeTheme::PreferredContrast::kMore;
}

constexpr char kAssistantSpeechSource[] = "https://blynx.invalid/assistant";

std::unique_ptr<ui::SimpleComboboxModel> EmptyModelList() {
  return std::make_unique<ui::SimpleComboboxModel>(
      std::vector<ui::SimpleComboboxModel::Item>());
}
// Native adaptation of Libraries.dev segmented Border beam. The rim
// paints behind native controls, has no hit target, and never opens a
// microphone.
class AssistantComposer : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(AssistantComposer, views::View)
 public:
  void SetProcessing(bool processing) {
    processing_ = processing;
    animation_.Stop();
    if (processing && IsDrawn() && Colorful(*this) &&
        gfx::Animation::ShouldRenderRichAnimation() &&
        !gfx::Animation::PrefersReducedMotion()) {
      animation_.Start();
    }
    SchedulePaint();
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        ui::ColorVariant(ui::kColorTextfieldBackground), 24));
    SetProcessing(processing_);
  }
  void VisibilityChanged(views::View* from, bool visible) override {
    views::View::VisibilityChanged(from, visible);
    SetProcessing(processing_);
  }
  void RemovedFromWidget() override {
    animation_.Stop();
    views::View::RemovedFromWidget();
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    views::View::OnPaintBackground(canvas);
    if (!Colorful(*this)) {
      return;
    }
    gfx::RectF bounds(GetLocalBounds());
    canvas->Save();
    canvas->ClipPath(SkPath::RRect(gfx::RectFToSkRect(bounds), 24, 24), true);
    constexpr std::array<SkColor4f, 9> colors = {
        SkColor4f{.2f, .8f, .9f, 0},   SkColor4f{.2f, .8f, .9f, .8f},
        SkColor4f{.2f, .8f, .9f, 0},   SkColor4f{.7f, .6f, .9f, 0},
        SkColor4f{.7f, .6f, .9f, .7f}, SkColor4f{.7f, .6f, .9f, 0},
        SkColor4f{.9f, .8f, .4f, 0},   SkColor4f{.9f, .8f, .4f, .7f},
        SkColor4f{.2f, .8f, .9f, 0}};
    constexpr std::array<SkScalar, 9> stops = {0,   .08f, .2f,  .4f, .5f,
                                               .6f, .75f, .85f, 1};
    SkMatrix rotation;
    rotation.setRotate(phase_, bounds.CenterPoint().x(),
                       bounds.CenterPoint().y());
    cc::PaintFlags rim;
    rim.setAntiAlias(true);
    rim.setStyle(cc::PaintFlags::kStroke_Style);
    rim.setStrokeWidth(processing_ ? 2.f : 1.f);
    rim.setAlphaf(processing_ ? .9f : .3f);
    rim.setShader(cc::PaintShader::MakeSweepGradient(
        bounds.CenterPoint().x(), bounds.CenterPoint().y(), colors.data(),
        stops.data(), colors.size(), SkTileMode::kClamp, 0, 360,
        SkGradient::Interpolation(), 0, &rotation));
    bounds.Inset(1);
    canvas->DrawRoundRect(bounds, 23, rim);
    canvas->Restore();
  }

 private:
  void AnimationProgressed(const gfx::Animation*) override {
    if (!IsDrawn() || !Colorful(*this) ||
        gfx::Animation::PrefersReducedMotion()) {
      animation_.Stop();
      return;
    }
    phase_ = std::fmod(phase_ + 1.2f, 360.f);
    SchedulePaint();
  }
  bool processing_ = false;
  float phase_ = 0;
  gfx::InfiniteAnimation animation_{this, base::Milliseconds(33)};
};
BEGIN_METADATA(AssistantComposer)
END_METADATA

// Textarea consumes Return before TextfieldController sees it. Intercept at
// the native pre-handler so Enter sends, while Shift+Enter and IME stay native.
class AssistantInput : public views::Textarea {
  METADATA_HEADER(AssistantInput, views::Textarea)
 public:
  explicit AssistantInput(base::RepeatingClosure submit)
      : submit_(std::move(submit)) {}
  bool PreHandleKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_RETURN && !event.IsShiftDown() &&
        !HasCompositionText()) {
      submit_.Run();
      return true;
    }
    return views::Textarea::PreHandleKeyPressed(event);
  }

 private:
  base::RepeatingClosure submit_;
};
BEGIN_METADATA(AssistantInput)
END_METADATA

class AssistantPanel : public views::View {
  METADATA_HEADER(AssistantPanel, views::View)
 public:
  explicit AssistantPanel(BrowserWindowInterface& browser)
      : browser_(browser.GetWeakPtr()) {
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface, 0));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 12));
    auto* header = AddChildView(std::make_unique<views::View>());
    auto* header_layout =
        header->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    auto* title = AddLabel(header, S(IDS_BLYNX_AI_TITLE));
    title->SetFontList(title->font_list().Derive(2, gfx::Font::NORMAL,
                                                 gfx::Font::Weight::MEDIUM));
    header_layout->SetFlexForView(title, 1);
    auto* new_chat = AddButton(
        header, IDS_BLYNX_AI_NEW_CHAT,
        base::BindRepeating(&AssistantPanel::NewChat, base::Unretained(this)));
    new_chat->SetText(u"+");
    new_chat->SetMinSize(gfx::Size(32, 32));
    new_chat->SetTooltipText(S(IDS_BLYNX_AI_NEW_CHAT));
    new_chat->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_NEW_CHAT));
    settings_button_ =
        AddButton(header, IDS_BLYNX_AI_SETUP,
                  base::BindRepeating(&AssistantPanel::ToggleSetup,
                                      base::Unretained(this)));
    auto* close = AddButton(
        header, IDS_BLYNX_AI_CLOSE,
        base::BindRepeating(&AssistantPanel::Close, base::Unretained(this)));
    close->SetText(u"×");
    close->SetMinSize(gfx::Size(32, 32));
    close->SetTooltipText(S(IDS_BLYNX_AI_CLOSE));
    close->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_CLOSE));
    setup_scroll_ = AddChildView(std::make_unique<views::ScrollView>());
    setup_scroll_->ClipHeightTo(0, 280);
    setup_scroll_->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    setup_ = setup_scroll_->SetContents(std::make_unique<views::View>());
    setup_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 7));
    enabled_ = setup_->AddChildView(std::make_unique<views::Checkbox>(
        S(IDS_BLYNX_AI_ENABLE),
        base::BindRepeating(&AssistantPanel::Save, base::Unretained(this))));
    api_ = setup_->AddChildView(std::make_unique<views::Checkbox>(
        S(IDS_BLYNX_AI_API),
        base::BindRepeating(&AssistantPanel::ProviderChanged,
                            base::Unretained(this))));
    endpoint_ = AddField(setup_, IDS_BLYNX_AI_ENDPOINT);
    model_label_ = AddLabel(setup_, S(IDS_BLYNX_AI_MODEL));
    model_ = setup_->AddChildView(std::make_unique<views::Textfield>());
    model_->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_MODEL));
    model_models_label_ = AddLabel(setup_, S(IDS_BLYNX_AI_MODELS));
    auto* model_row = setup_->AddChildView(std::make_unique<views::View>());
    model_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    model_list_ = model_row->AddChildView(
        std::make_unique<views::Combobox>(EmptyModelList()));
    model_list_->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_MODELS));
    model_list_->SetCallback(base::BindRepeating(&AssistantPanel::ModelSelected,
                                                 base::Unretained(this)));
    static_cast<views::BoxLayout*>(model_row->GetLayoutManager())
        ->SetFlexForView(model_list_, 1);
    refresh_models_ =
        AddButton(model_row, IDS_BLYNX_AI_REFRESH_MODELS,
                  base::BindRepeating(&AssistantPanel::DiscoverModels,
                                      base::Unretained(this)));
    model_hint_ = AddLabel(setup_, S(IDS_BLYNX_AI_MODEL_HINT));
    model_hint_->SetMultiLine(true);
    model_label_->SetVisible(false);
    model_->SetVisible(false);
    model_models_label_->SetVisible(false);
    model_list_->SetVisible(false);
    refresh_models_->SetVisible(false);
    key_label_ = AddLabel(setup_, S(IDS_BLYNX_AI_KEY));
    key_ = setup_->AddChildView(std::make_unique<views::Textfield>());
    key_->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_KEY));
    key_->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    key_note_ = AddLabel(setup_, S(IDS_BLYNX_AI_KEY_NOTE));
    speak_ = setup_->AddChildView(std::make_unique<views::Checkbox>(
        S(IDS_BLYNX_AI_SPEAK),
        base::BindRepeating(&AssistantPanel::SpeechChanged,
                            base::Unretained(this))));
    AddLabel(setup_, S(IDS_BLYNX_AI_SPEECH_NOTE));
    AddButton(
        setup_, IDS_BLYNX_AI_SAVE,
        base::BindRepeating(&AssistantPanel::Save, base::Unretained(this)));
    const auto& stored =
        browser.GetProfile()->GetPrefs()->GetDict(kAssistantConfigPref);
    const auto config = ValidateAssistantConfig(stored)
                            ? stored.Clone()
                            : DefaultAssistantConfig();
    enabled_->SetChecked(config.FindBool("enabled").value());
    api_->SetChecked(*config.FindString("provider") == "api");
    endpoint_->SetText(base::UTF8ToUTF16(*config.FindString("endpoint")));
    model_->SetText(base::UTF8ToUTF16(*config.FindString("model")));
    speak_->SetChecked(false);
    UpdateProviderUI();
    setup_scroll_->SetVisible(false);
    auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
    scroll_ = scroll;
    scroll->SetBackgroundColor(ui::ColorVariant(ui::kColorSysSurface));
    // Bounded measurement sizes contents at the viewport width, while the
    // parent flex layout owns the actual viewport height.
    scroll->ClipHeightTo(0, 0);
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    auto transcript = std::make_unique<views::View>();
    transcript->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 0), 12));
    transcript_ = scroll->SetContents(std::move(transcript));
    transcript_->GetViewAccessibility().SetRole(ax::mojom::Role::kLog);
    static_cast<views::BoxLayout*>(GetLayoutManager())
        ->SetFlexForView(scroll, 1);
    ShowWelcome();
    auto* context_row = AddChildView(std::make_unique<views::View>());
    context_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
    context_ = context_row->AddChildView(
        std::make_unique<views::Checkbox>(S(IDS_BLYNX_AI_SHARE_TABS)));
    page_ = context_row->AddChildView(
        std::make_unique<views::Checkbox>(S(IDS_BLYNX_AI_SHARE_PAGE)));
    auto* composer = AddChildView(std::make_unique<AssistantComposer>());
    composer_ = composer;
    composer->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(14), 10));
    input_ = composer->AddChildView(std::make_unique<AssistantInput>(
        base::BindRepeating(&AssistantPanel::Send, base::Unretained(this))));
    input_->SetPlaceholderText(S(IDS_BLYNX_AI_PLACEHOLDER));
    input_->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_PLACEHOLDER));
    input_->SetPreferredSize(gfx::Size(180, 64));
    input_->SetBorder(views::CreateEmptyBorder(gfx::Insets(2)));
    auto* controls = composer->AddChildView(std::make_unique<views::View>());
    controls->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    send_ = AddButton(
        controls, IDS_BLYNX_AI_SEND,
        base::BindRepeating(&AssistantPanel::Send, base::Unretained(this)));
    stop_ = AddButton(
        controls, IDS_BLYNX_AI_STOP,
        base::BindRepeating(&AssistantPanel::Stop, base::Unretained(this)));
    auto* model_caption = AddLabel(controls, base::UTF8ToUTF16(CurrentModel()));
    model_caption_ = model_caption;
    model_caption->SetElideBehavior(gfx::ELIDE_MIDDLE);
    model_caption->SetMultiLine(false);
    static_cast<views::BoxLayout*>(controls->GetLayoutManager())
        ->SetFlexForView(model_caption, 1);
    controls->ReorderChildView(model_caption, 0);
    send_->SetText(u"↑");
    send_->SetMinSize(gfx::Size(36, 36));
    send_->SetTooltipText(S(IDS_BLYNX_AI_SEND));
    send_->GetViewAccessibility().SetName(S(IDS_BLYNX_AI_SEND));
    status_ = AddLabel(this, S(IDS_BLYNX_AI_READY));
    status_->GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
    SetBusy(false);
    if (!Editable(&browser)) {
      enabled_->SetEnabled(false);
      Status(IDS_BLYNX_AI_UNAVAILABLE);
    }
  }
  void RemovedFromWidget() override {
    Stop();
    ClearKey();
    views::View::RemovedFromWidget();
  }
  void VisibilityChanged(views::View* from, bool visible) override {
    views::View::VisibilityChanged(from, visible);
    if (!IsDrawn() && status_) {
      Stop();
      ClearKey();
    }
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (input_) {
      input_->SetBackgroundColor(
          ui::ColorVariant(ui::kColorTextfieldBackground));
      input_->SetTextColorId(ui::kColorTextfieldForeground);
    }
  }

 private:
  enum class PendingRequest { kNone, kChat, kModels };

  views::Label* AddLabel(views::View* parent, std::u16string text) {
    auto* label = parent->AddChildView(std::make_unique<views::Label>(text));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetMultiLine(true);
    label->SetEnabledColor(ui::kColorTextfieldForeground);
    return label;
  }
  views::MdTextButton* AddButton(views::View* parent,
                                 int id,
                                 base::RepeatingClosure callback) {
    return parent->AddChildView(
        std::make_unique<views::MdTextButton>(std::move(callback), S(id)));
  }
  views::Textfield* AddField(views::View* parent, int id) {
    AddLabel(parent, S(id));
    auto* field = parent->AddChildView(std::make_unique<views::Textfield>());
    field->GetViewAccessibility().SetName(S(id));
    return field;
  }
  void ShowWelcome() {
    welcome_ = true;
    auto* heading = AddLabel(transcript_, S(IDS_BLYNX_AI_PLACEHOLDER));
    heading->SetFontList(heading->font_list().Derive(
        10, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));
    heading->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    heading->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(92, 4, 16, 4)));
    auto* note = AddLabel(transcript_, S(IDS_BLYNX_AI_WELCOME));
    note->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  }
  void AddMessage(std::u16string text, bool user = false) {
    if (welcome_) {
      transcript_->RemoveAllChildViews();
      welcome_ = false;
    }
    auto* message = AddLabel(transcript_, std::move(text));
    // Let BoxLayout measure multiline height at the viewport width. Fixed
    // message sizes clip replies and break narrow side panels.
    if (user) {
      message->SetBackground(
          views::CreateRoundedRectBackground(ui::kColorSysSurface3, 18));
    }
    message->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(11, 13, 11, 13)));
    message->SetSelectable(true);
    if (transcript_->children().size() > 40) {
      transcript_->RemoveChildViewT(transcript_->children().front());
    }
    transcript_->InvalidateLayout();
    InvalidateLayout();
    // Layer-backed scrolling must wait for a rendered layout; a posted task
    // can run before the new message has real bounds.
    scroll_->RegisterNextSuccessfulFramePostLayoutCallback(base::BindOnce(
        [](base::WeakPtr<AssistantPanel> panel) {
          if (panel && panel->IsDrawn()) {
            panel->scroll_->ScrollToPosition(
                panel->scroll_->vertical_scroll_bar(),
                panel->transcript_->height());
          }
        },
        weak_factory_.GetWeakPtr()));
  }

  void ClearKey() {
    key_->SetText(u"");
    key_->ClearEditHistory();
  }
  void Status(int id) {
    status_->SetText(S(id));
    status_->GetViewAccessibility().AnnounceText(S(id));
  }
  void SetBusy(bool busy) {
    busy_ = busy;
    send_->SetEnabled(!busy);
    stop_->SetEnabled(busy);
    stop_->SetVisible(busy);
    send_->SetVisible(!busy);
    composer_->SetProcessing(busy);
    api_->SetEnabled(!busy);
    endpoint_->SetEnabled(!busy);
    model_->SetEnabled(!busy);
    model_list_->SetEnabled(!busy);
    refresh_models_->SetEnabled(!busy);
    key_->SetEnabled(!busy);
    context_->SetEnabled(!busy);
    page_->SetEnabled(!busy);
  }
  void ToggleSetup() {
    const bool visible = !setup_scroll_->GetVisible();
    setup_scroll_->SetVisible(visible);
    if (visible && !api_->GetChecked() && model_names_.empty()) {
      DiscoverModels();
    }
    InvalidateLayout();
  }
  void ProviderChanged() {
    Stop();
    ClearKey();
    const bool api = api_->GetChecked();
    endpoint_->SetText(api ? u"https://api.example.com/v1/chat/completions"
                           : u"http://127.0.0.1:11434/api/chat");
    model_->SetText(api ? u"" : u"qwen2.5:0.5b");
    model_names_.clear();
    UpdateProviderUI();
  }
  void UpdateProviderUI() {
    const bool ollama = !api_->GetChecked();
    model_label_->SetVisible(!ollama);
    model_->SetVisible(!ollama);
    model_models_label_->SetVisible(ollama);
    model_list_->SetVisible(ollama);
    refresh_models_->SetVisible(ollama);
    model_hint_->SetVisible(ollama);
    key_label_->SetVisible(!ollama);
    key_->SetVisible(!ollama);
    key_note_->SetVisible(!ollama);
    InvalidateLayout();
  }
  void ModelSelected() {
    const auto selected = model_list_->GetSelectedIndex();
    if (selected && *selected < model_names_.size()) {
      model_->SetText(base::UTF8ToUTF16(model_names_[*selected]));
    }
  }
  void SpeechChanged() {
    if (!speak_->GetChecked()) {
      StopSpeech();
    } else if (!SpeechAvailable()) {
      Status(IDS_BLYNX_AI_SPEECH_UNAVAILABLE);
    }
  }
  bool SpeechAvailable() const {
#if BUILDFLAG(IS_LINUX)
    return base::CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableSpeechDispatcher);
#else
    return true;
#endif
  }
  void StopSpeech() {
    content::TtsController::GetInstance()->Stop(GURL(kAssistantSpeechSource));
  }
  void SpeakReply(const std::string& message) {
    if (!speak_->GetChecked() || !SpeechAvailable()) {
      return;
    }
    const std::string text(base::TruncateUTF8ToByteSize(message, 8192));
    if (text.empty()) {
      return;
    }
    auto utterance = content::TtsUtterance::Create(browser_->GetProfile());
    utterance->SetText(text);
    utterance->SetSrcUrl(GURL(kAssistantSpeechSource));
    utterance->SetLang("en-US");
    utterance->SetContinuousParameters(1.0, 1.0, 1.0);
    utterance->SetShouldClearQueue(false);
    content::TtsController::GetInstance()->SpeakOrEnqueue(std::move(utterance));
  }
  std::string CurrentModel() const {
    return base::UTF16ToUTF8(model_->GetText());
  }
  void UpdateModels(const std::vector<std::string>& names) {
    model_names_ = names;
    std::vector<ui::SimpleComboboxModel::Item> items;
    items.reserve(names.size());
    for (const auto& name : names) {
      items.emplace_back(base::UTF8ToUTF16(name));
    }
    auto* model =
        static_cast<ui::SimpleComboboxModel*>(model_list_->GetModel());
    model->UpdateItemList(std::move(items));
    const std::string saved = CurrentModel();
    const auto found =
        std::find(model_names_.begin(), model_names_.end(), saved);
    const size_t index =
        found == model_names_.end()
            ? 0
            : static_cast<size_t>(found - model_names_.begin());
    if (!model_names_.empty()) {
      model_list_->SetSelectedIndex(index);
      model_->SetText(base::UTF8ToUTF16(model_names_[index]));
    } else {
      model_list_->SetSelectedIndex(std::nullopt);
    }
  }
  void DiscoverModels() {
    if (busy_ || api_->GetChecked() || !Editable(browser_.get())) {
      return;
    }
    const auto config =
        DefaultAssistantConfig()
            .Set("enabled", enabled_->GetChecked())
            .Set("provider", "ollama")
            .Set("endpoint", base::UTF16ToUTF8(endpoint_->GetText()))
            .Set("model", CurrentModel());
    if (!ValidateAssistantConfig(config)) {
      Status(IDS_BLYNX_AI_INVALID);
      return;
    }
    GURL::Replacements replacements;
    replacements.SetPathStr("/api/tags");
    replacements.ClearQuery();
    replacements.ClearRef();
    const GURL tags_url =
        GURL(*config.FindString("endpoint")).ReplaceComponents(replacements);
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = tags_url;
    request->method = net::HttpRequestHeaders::kGetMethod;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->redirect_mode = network::mojom::RedirectMode::kError;
    request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
    request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                               "application/json");
    constexpr auto annotation =
        net::DefineNetworkTrafficAnnotation("blynx_assistant_models", R"(
      semantics { sender: "Blynx assistant" description: "User-requested discovery of installed models from a local Ollama server."
        trigger: "User opens assistant settings or presses Refresh installed models." data: "Local Ollama model names only." destination: OTHER destination_other: "User-configured loopback Ollama server." }
      policy { cookies_allowed: NO setting: "Assistant settings; disabled by default." policy_exception_justification: "No dedicated enterprise policy; feature is unavailable for managed assistant preferences." })");
    loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
    pending_request_ = PendingRequest::kModels;
    SetBusy(true);
    Status(IDS_BLYNX_AI_DISCOVERING);
    loader_->SetTimeoutDuration(base::Seconds(10));
    loader_->SetOnRedirectCallback(base::BindRepeating(
        [](base::WeakPtr<AssistantPanel> self, const GURL&,
           const net::RedirectInfo&, const network::mojom::URLResponseHead&,
           std::vector<std::string>*) {
          if (self) {
            self->Stop();
            self->Status(IDS_BLYNX_AI_REDIRECT);
          }
        },
        weak_factory_.GetWeakPtr()));
    loader_->DownloadToString(browser_->GetProfile()
                                  ->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess()
                                  .get(),
                              base::BindOnce(&AssistantPanel::ModelsResponse,
                                             weak_factory_.GetWeakPtr()),
                              512 * 1024);
  }
  void ModelsResponse(std::optional<std::string> body) {
    const int http = loader_->ResponseInfo() && loader_->ResponseInfo()->headers
                         ? loader_->ResponseInfo()->headers->response_code()
                         : 0;
    loader_.reset();
    pending_request_ = PendingRequest::kNone;
    SetBusy(false);
    if (!body || http < 200 || http >= 300) {
      Status(IDS_BLYNX_AI_MODELS_ERROR);
      return;
    }
    const auto models = ParseOllamaModels(*body);
    if (!models) {
      Status(IDS_BLYNX_AI_MODELS_ERROR);
      return;
    }
    UpdateModels(*models);
    Status(models->empty() ? IDS_BLYNX_AI_NO_MODELS
                           : IDS_BLYNX_AI_MODELS_FOUND);
  }
  bool SaveConfig() {
    if (!Editable(browser_.get())) {
      Status(IDS_BLYNX_AI_UNAVAILABLE);
      return false;
    }
    auto config = DefaultAssistantConfig();
    config.Set("enabled", enabled_->GetChecked());
    config.Set("provider", api_->GetChecked() ? "api" : "ollama");
    config.Set("endpoint", base::UTF16ToUTF8(endpoint_->GetText()));
    config.Set("model", base::UTF16ToUTF8(model_->GetText()));
    const auto key = base::UTF16ToUTF8(key_->GetText());
    if (!ValidateAssistantConfig(config) || key.size() > 4096 ||
        !net::HttpUtil::IsValidHeaderValue(key)) {
      Status(IDS_BLYNX_AI_INVALID);
      return false;
    }
    const auto& old =
        browser_->GetProfile()->GetPrefs()->GetDict(kAssistantConfigPref);
    if (*old.FindString("provider") != *config.FindString("provider") ||
        *old.FindString("endpoint") != *config.FindString("endpoint") ||
        *old.FindString("model") != *config.FindString("model")) {
      history_.clear();
      transcript_->RemoveAllChildViews();
      ShowWelcome();
    }
    return WriteAssistantConfig(*browser_->GetProfile()->GetPrefs(), config);
  }
  void Save() {
    Stop();
    if (SaveConfig()) {
      if (!enabled_->GetChecked()) {
        NewChat();
        ClearKey();
      }
      if (model_caption_) {
        model_caption_->SetText(base::UTF8ToUTF16(CurrentModel()));
      }
      setup_scroll_->SetVisible(false);
      input_->RequestFocus();
      Status(IDS_BLYNX_AI_SAVED);
    }
  }
  void ClearProposals() {
    actions_.clear();
    targets_.clear();
    target_urls_.clear();
  }
  void Stop() {
    weak_factory_.InvalidateWeakPtrs();
    StopSpeech();
    ClearProposals();
    loader_.reset();
    pending_request_ = PendingRequest::kNone;
    pending_user_.clear();
    if (composer_) {
      SetBusy(false);
    }
    if (status_) {
      Status(IDS_BLYNX_AI_STOPPED);
    }
  }
  void NewChat() {
    Stop();
    ClearProposals();
    history_.clear();
    transcript_->RemoveAllChildViews();
    ShowWelcome();
    input_->SetText(u"");
    input_->ClearEditHistory();
  }
  void Close() {
    Stop();
    ClearKey();
    if (browser_) {
      SidePanelUI::From(browser_.get())->Close();
    }
  }
  bool Enabled() {
    return Editable(browser_.get()) && browser_->GetProfile()
                                           ->GetPrefs()
                                           ->GetDict(kAssistantConfigPref)
                                           .FindBool("enabled")
                                           .value_or(false);
  }
  void Send() {
    if (busy_) {
      return;
    }
    if (!SaveConfig()) {
      setup_scroll_->SetVisible(true);
      return;
    }
    if (!Enabled()) {
      Status(IDS_BLYNX_AI_ENABLE_FIRST);
      setup_scroll_->SetVisible(true);
      return;
    }
    std::string prompt = base::UTF16ToUTF8(input_->GetText());
    if (base::TrimWhitespaceASCII(prompt, base::TRIM_ALL).empty() ||
        prompt.size() > 8000) {
      Status(IDS_BLYNX_AI_PROMPT_LIMIT);
      return;
    }
    const auto& config =
        browser_->GetProfile()->GetPrefs()->GetDict(kAssistantConfigPref);
    if (*config.FindString("provider") == "api" && key_->GetText().empty()) {
      Status(IDS_BLYNX_AI_KEY_REQUIRED);
      setup_scroll_->SetVisible(true);
      return;
    }
    ClearProposals();
    SetBusy(true);
    Status(IDS_BLYNX_AI_THINKING);
    AddMessage(base::UTF8ToUTF16(prompt), true);
    input_->SetText(u"");
    base::DictValue shared;
    auto* tabs = browser_->GetTabStripModel();
    if (context_->GetChecked() || page_->GetChecked()) {
      base::ListValue tab_list;
      for (int i = 0; i < tabs->count() && targets_.size() < 100; ++i) {
        auto* tab = tabs->GetWebContentsAt(i);
        if (!IsAssistantWebURL(tab->GetLastCommittedURL())) {
          continue;
        }
        if (!context_->GetChecked() && tab != tabs->GetActiveWebContents()) {
          continue;
        }
        targets_.push_back(tab->GetWeakPtr());
        target_urls_.push_back(tab->GetLastCommittedURL());
        tab_list.Append(
            base::DictValue()
                .Set("tab", static_cast<int>(targets_.size()))
                .Set("title", base::UTF16ToUTF8(tab->GetTitle()).substr(0, 500))
                .Set("url", tab->GetLastCommittedURL().spec())
                .Set("active", tab == tabs->GetActiveWebContents()));
      }
      shared.Set("tabs", std::move(tab_list));
    }
    if (page_->GetChecked()) {
      auto* tab = tabs->GetActiveWebContents();
      if (!tab || !IsAssistantWebURL(tab->GetLastCommittedURL())) {
        SetBusy(false);
        Status(IDS_BLYNX_AI_PAGE_UNAVAILABLE);
        return;
      }
      const auto url = tab->GetLastCommittedURL();
      content_extraction::GetInnerText(
          *tab->GetPrimaryMainFrame(), std::nullopt,
          base::BindOnce(&AssistantPanel::PageReady, weak_factory_.GetWeakPtr(),
                         std::move(prompt), std::move(shared),
                         tab->GetWeakPtr(), url));
      return;
    }
    Request(std::move(prompt), std::move(shared));
  }
  void PageReady(std::string prompt,
                 base::DictValue shared,
                 base::WeakPtr<content::WebContents> tab,
                 GURL url,
                 std::unique_ptr<content_extraction::InnerTextResult> result) {
    if (!Enabled() || !tab || tab->GetLastCommittedURL() != url || !result) {
      SetBusy(false);
      Status(IDS_BLYNX_AI_PAGE_UNAVAILABLE);
      return;
    }
    shared.Set("page_text", std::string(base::TruncateUTF8ToByteSize(
                                result->inner_text, 12000)));
    Request(std::move(prompt), std::move(shared));
  }
  void Request(std::string prompt, base::DictValue shared) {
    if (!Enabled()) {
      Stop();
      return;
    }
    const auto config = browser_->GetProfile()
                            ->GetPrefs()
                            ->GetDict(kAssistantConfigPref)
                            .Clone();
    const bool local = *config.FindString("provider") == "ollama";
    base::ListValue messages;
    messages.Append(base::DictValue()
                        .Set("role", "system")
                        .Set("content", AssistantSystemPrompt()));
    for (const auto& turn : history_) {
      messages.Append(turn.Clone());
    }
    auto user = base::DictValue()
                    .Set("request", prompt)
                    .Set("browser_context", std::move(shared));
    const auto user_json = base::WriteJson(user).value();
    messages.Append(
        base::DictValue().Set("role", "user").Set("content", user_json));
    pending_user_ =
        base::DictValue().Set("role", "user").Set("content", prompt);
    auto body = base::DictValue()
                    .Set("model", *config.FindString("model"))
                    .Set("messages", std::move(messages))
                    .Set("stream", false);
    if (local) {
      body.Set("keep_alive", 0);
      body.Set("options", base::DictValue().Set("num_predict", 2048));
    }
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(*config.FindString("endpoint"));
    request->method = "POST";
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
    const auto key = base::UTF16ToUTF8(key_->GetText());
    if (!local && !key.empty()) {
      request->headers.SetHeader(net::HttpRequestHeaders::kAuthorization,
                                 "Bearer " + key);
    }
    constexpr auto annotation =
        net::DefineNetworkTrafficAnnotation("blynx_assistant_chat", R"(
      semantics { sender: "Blynx assistant" description: "User-requested chat with a user-configured local Ollama or HTTPS API server."
        trigger: "User enables the assistant and presses Send."
        data: "Chat text and session history; tab URLs/titles or page text only when the user checks the corresponding sharing controls. API credential if provided."
        destination: OTHER destination_other: "User-configured model endpoint." }
      policy { cookies_allowed: NO setting: "Disabled by default. Disable the assistant in its side panel to stop requests."
        policy_exception_justification: "No dedicated enterprise policy; feature is unavailable for managed assistant preferences." })");
    loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
    pending_request_ = PendingRequest::kChat;
    loader_->SetTimeoutDuration(base::Seconds(120));
    loader_->SetOnRedirectCallback(base::BindRepeating(
        [](base::WeakPtr<AssistantPanel> self, const GURL&,
           const net::RedirectInfo&, const network::mojom::URLResponseHead&,
           std::vector<std::string>*) {
          if (auto* panel = self.get()) {
            panel->Stop();
            panel->Status(IDS_BLYNX_AI_REDIRECT);
          }
        },
        weak_factory_.GetWeakPtr()));
    loader_->AttachStringForUpload(base::WriteJson(body).value(),
                                   "application/json");
    loader_->DownloadToString(browser_->GetProfile()
                                  ->GetDefaultStoragePartition()
                                  ->GetURLLoaderFactoryForBrowserProcess()
                                  .get(),
                              base::BindOnce(&AssistantPanel::Response,
                                             weak_factory_.GetWeakPtr(), local),
                              512 * 1024);
  }
  void Response(bool local, std::optional<std::string> body) {
    const int http = loader_->ResponseInfo() && loader_->ResponseInfo()->headers
                         ? loader_->ResponseInfo()->headers->response_code()
                         : 0;
    loader_.reset();
    pending_request_ = PendingRequest::kNone;
    SetBusy(false);
    if (!Enabled()) {
      ClearProposals();
      return;
    }
    if (!body || http < 200 || http >= 300) {
      Status(IDS_BLYNX_AI_NETWORK_ERROR);
      return;
    }
    auto response = base::JSONReader::Read(*body, base::JSON_PARSE_RFC, 32);
    const std::string* text = nullptr;
    if (response && response->is_dict()) {
      if (local) {
        text = response->GetDict().FindStringByDottedPath("message.content");
      } else if (const auto* choices = response->GetDict().FindList("choices");
                 choices && !choices->empty() && (*choices)[0].is_dict()) {
        text =
            (*choices)[0].GetDict().FindStringByDottedPath("message.content");
      }
    }
    auto reply = text ? ParseAssistantReply(*text) : std::nullopt;
    if (!reply) {
      Status(IDS_BLYNX_AI_BAD_REPLY);
      return;
    }
    const auto& message = *reply->FindString("message");
    AddMessage(base::UTF8ToUTF16(message));
    SpeakReply(message);
    history_.Append(std::move(pending_user_));
    history_.Append(
        base::DictValue().Set("role", "assistant").Set("content", *text));
    while (history_.size() > 12) {
      history_.erase(history_.begin());
    }
    actions_ = std::move(*reply->FindList("actions"));
    Status(IDS_BLYNX_AI_READY);
    if (!actions_.empty()) {
      RunActions();
    }
  }
  void RunActions() {
    if (busy_ || !Enabled() || !ValidateAssistantActions(actions_) ||
        actions_.empty()) {
      ClearProposals();
      return;
    }
    auto* tabs = browser_->GetTabStripModel();
    NativeAppearanceController appearance(*browser_->GetProfile());
    int closing = 0;
    for (const auto& item : actions_) {
      if (*item.GetDict().FindString("type") == "close_tab") {
        ++closing;
      }
    }
    if (closing >= tabs->count()) {
      Status(IDS_BLYNX_AI_STALE);
      ClearProposals();
      return;
    }
    for (const auto& item : actions_) {
      const auto& action = item.GetDict();
      if (const auto tab = action.FindInt("tab")) {
        if (static_cast<size_t>(*tab) > target_urls_.size() ||
            !targets_[*tab - 1] ||
            targets_[*tab - 1]->GetLastCommittedURL() !=
                target_urls_[*tab - 1]) {
          Status(IDS_BLYNX_AI_STALE);
          ClearProposals();
          return;
        }

        if (static_cast<size_t>(*tab) > targets_.size() ||
            !targets_[*tab - 1] ||
            tabs->GetIndexOfWebContents(targets_[*tab - 1].get()) < 0 ||
            !IsAssistantWebURL(targets_[*tab - 1]->GetLastCommittedURL()) ||
            (*action.FindString("type") == "close_tab" && tabs->count() <= 1)) {
          Status(IDS_BLYNX_AI_STALE);
          ClearProposals();
          return;
        }
      }
      if (action.FindString("type")->starts_with("set_") &&
          appearance.GetAccess() != NativeAppearanceAccess::kAllowed) {
        Status(IDS_BLYNX_AI_UNAVAILABLE);
        ClearProposals();
        return;
      }
    }
    auto actions = std::move(actions_);
    auto targets = targets_;
    auto urls = target_urls_;
    ClearProposals();
    Status(IDS_BLYNX_AI_ACTIONS_DONE);
    AddMessage(S(IDS_BLYNX_AI_ACTIONS_DONE));
    history_.Append(base::DictValue()
                        .Set("role", "user")
                        .Set("content",
                             "Browser result: requested actions were "
                             "dispatched through browser "
                             "services; final page results are not verified."));
    // Navigation can replace/hide the panel. Dispatch with owned snapshots and
    // no panel dereferences once browser actions begin.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<AssistantPanel> panel,
               base::WeakPtr<BrowserWindowInterface> browser,
               base::ListValue actions,
               std::vector<base::WeakPtr<content::WebContents>> targets,
               std::vector<GURL> urls) {
              if (panel) {
                ExecuteActions(std::move(browser), std::move(actions),
                               std::move(targets), std::move(urls));
              }
            },
            weak_factory_.GetWeakPtr(), browser_, std::move(actions),
            std::move(targets), std::move(urls)));
  }
  static void ExecuteActions(
      base::WeakPtr<BrowserWindowInterface> browser,
      base::ListValue actions,
      std::vector<base::WeakPtr<content::WebContents>> targets,
      std::vector<GURL> urls) {
    for (const auto& item : actions) {
      if (!Editable(browser.get()) || !browser->GetProfile()
                                           ->GetPrefs()
                                           ->GetDict(kAssistantConfigPref)
                                           .FindBool("enabled")
                                           .value_or(false)) {
        return;
      }
      auto* tabs = browser->GetTabStripModel();
      const auto& action = item.GetDict();
      const auto& type = *action.FindString("type");
      content::WebContents* tab = nullptr;
      if (const auto id = action.FindInt("tab")) {
        const size_t index = *id - 1;
        if (index >= targets.size() || index >= urls.size() ||
            !targets[index] ||
            targets[index]->GetLastCommittedURL() != urls[index] ||
            tabs->GetIndexOfWebContents(targets[index].get()) < 0) {
          return;
        }
        tab = targets[index].get();
      }
      if (type == "open_tab") {
        NavigateParams params(browser.get(), GURL(*action.FindString("url")),
                              ui::PAGE_TRANSITION_TYPED);
        params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
        Navigate(&params);
      } else if (type == "activate_tab" && tab) {
        tabs->ActivateTabAt(tabs->GetIndexOfWebContents(tab));
      } else if (type == "close_tab" && tab && tabs->count() > 1) {
        tabs->CloseWebContentsAt(tabs->GetIndexOfWebContents(tab),
                                 TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      } else if (type == "back" && tab && tab->GetController().CanGoBack()) {
        tab->GetController().GoBack();
      } else if (type == "forward" && tab &&
                 tab->GetController().CanGoForward()) {
        tab->GetController().GoForward();
      } else if (type == "reload" && tab) {
        tab->GetController().Reload(content::ReloadType::NORMAL, true);
      } else if (type.starts_with("set_")) {
        NativeAppearanceController appearance(*browser->GetProfile());
        if (appearance.GetAccess() != NativeAppearanceAccess::kAllowed) {
          return;
        }
        auto preset = appearance.Load().preset;
        if (type == "set_vertical_tabs") {
          appearance.Apply(preset, action.FindBool("enabled"));
        } else if (type == "set_theme") {
          const auto& theme = *action.FindString("theme");
          preset.theme = theme == "dark"    ? AppearanceTheme::kDark
                         : theme == "light" ? AppearanceTheme::kLight
                                            : AppearanceTheme::kSystem;
          appearance.Apply(preset);
        }
      }
    }
  }
  base::WeakPtr<BrowserWindowInterface> browser_;
  raw_ptr<views::View> setup_ = nullptr;
  raw_ptr<views::View> transcript_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::ScrollView> setup_scroll_ = nullptr;
  raw_ptr<AssistantComposer> composer_ = nullptr;
  raw_ptr<views::Textarea> input_ = nullptr;
  raw_ptr<views::Textfield> endpoint_ = nullptr, model_ = nullptr,
                            key_ = nullptr;
  raw_ptr<views::Label> model_label_ = nullptr, model_models_label_ = nullptr,
                        model_hint_ = nullptr, key_label_ = nullptr,
                        key_note_ = nullptr;
  raw_ptr<views::Combobox> model_list_ = nullptr;
  raw_ptr<views::Checkbox> enabled_ = nullptr, api_ = nullptr,
                           context_ = nullptr, page_ = nullptr,
                           speak_ = nullptr;
  raw_ptr<views::MdTextButton> settings_button_ = nullptr,
                               refresh_models_ = nullptr, send_ = nullptr,
                               stop_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<views::Label> model_caption_ = nullptr;
  bool welcome_ = false;
  base::ListValue history_, actions_;
  base::DictValue pending_user_;
  std::vector<base::WeakPtr<content::WebContents>> targets_;
  std::vector<GURL> target_urls_;
  std::vector<std::string> model_names_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  PendingRequest pending_request_ = PendingRequest::kNone;
  bool busy_ = false;
  base::WeakPtrFactory<AssistantPanel> weak_factory_{this};
};
BEGIN_METADATA(AssistantPanel)
END_METADATA
}  // namespace
void ToggleBlynxAssistant(BrowserWindowInterface& browser) {
  if (!Regular(&browser)) {
    return;
  }
  auto* registry = SidePanelRegistry::From(&browser);
  auto* panel = SidePanelUI::From(&browser);
  if (!registry || !panel) {
    return;
  }
  const SidePanelEntry::Key key(SidePanelEntry::Id::kBlynxAssistant);
  if (panel->IsSidePanelEntryShowing(key)) {
    panel->Close();
    return;
  }
  if (!registry->GetEntryForKey(key)) {
    auto entry = std::make_unique<SidePanelEntry>(
        key,
        base::BindRepeating(
            [](base::WeakPtr<BrowserWindowInterface> browser,
               SidePanelEntryScope&) -> std::unique_ptr<views::View> {
              return browser ? std::make_unique<AssistantPanel>(*browser)
                             : nullptr;
            },
            browser.GetWeakPtr()),
        base::BindRepeating([] { return 400; }));
    entry->set_should_show_header(false);
    entry->set_should_show_ephemerally_in_toolbar(false);
    registry->Register(std::move(entry));
  }
  panel->Show(key);
}
}  // namespace browser
