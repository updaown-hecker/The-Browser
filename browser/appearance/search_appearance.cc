#include "browser/appearance/search_appearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include "base/strings/string_util.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace browser {
namespace {
constexpr std::array<std::string_view, 5> kStyles = {
    "md", "sm", "line", "pulse-inner", "pulse-outside"};
constexpr std::array<std::string_view, 5> kPalettes = {
    "colorful", "mono", "ocean", "sunset", "custom"};
constexpr std::array<std::string_view, 3> kThemes = {"system", "light", "dark"};
bool Color(std::string_view value) {
  return value.size() == 7 && value[0] == '#' &&
         value.substr(1).find_first_not_of("0123456789abcdefABCDEF") ==
             std::string_view::npos;
}
}  // namespace

base::DictValue DefaultSearchAppearance() {
  auto colors = base::DictValue()
                    .Set("surface", "#14171A")
                    .Set("heading", "#F1F3F4")
                    .Set("hint", "#B4BBC4")
                    .Set("input", "#1C2025")
                    .Set("input_text", "#F1F3F4")
                    .Set("placeholder", "#ADB8C8")
                    .Set("selection", "#245F95")
                    .Set("selection_text", "#FFFFFF")
                    .Set("row", "#1C2025")
                    .Set("row_text", "#B3D3FA")
                    .Set("selected", "#064F75")
                    .Set("selected_text", "#FFFFFF")
                    .Set("hover", "#30343A")
                    .Set("hover_text", "#FFFFFF")
                    .Set("outline", "#69727D")
                    .Set("beam_1", "#20BFDF")
                    .Set("beam_2", "#8CD938")
                    .Set("beam_3", "#E6CC40")
                    .Set("beam_4", "#AD80D9");
  return base::DictValue()
      .Set("version", 1)
      .Set("outer_style", "pulse-outside")
      .Set("hover_style", "pulse-inner")
      .Set("input_style", "line")
      .Set("palette", "colorful")
      .Set("strength", .7)
      .Set("active", true)
      .Set("duration", 10.0)
      .Set("theme", "system")
      .Set("custom_colors", false)
      .Set("colors", std::move(colors));
}

bool ValidateSearchAppearance(const base::DictValue& value) {
  const auto defaults = DefaultSearchAppearance();
  if (value.size() != defaults.size() || value.FindInt("version") != 1 ||
      !value.FindBool("active").has_value() ||
      !value.FindBool("custom_colors").has_value()) {
    return false;
  }
  for (const char* key : {"outer_style", "hover_style", "input_style"}) {
    const auto* style = value.FindString(key);
    if (!style || !std::ranges::contains(kStyles, *style)) {
      return false;
    }
  }
  const auto* palette = value.FindString("palette");
  const auto* theme = value.FindString("theme");
  const auto strength = value.FindDouble("strength");
  const auto duration = value.FindDouble("duration");
  if (!palette || !std::ranges::contains(kPalettes, *palette) || !theme ||
      !std::ranges::contains(kThemes, *theme) || !strength || !duration ||
      !std::isfinite(*strength) || *strength < 0 || *strength > 1 ||
      !std::isfinite(*duration) || *duration < 2 || *duration > 30) {
    return false;
  }
  const auto* colors = value.FindDict("colors");
  const auto& expected = *defaults.FindDict("colors");
  if (!colors || colors->size() != expected.size()) {
    return false;
  }
  for (auto item : expected) {
    const auto* color = colors->FindString(item.first);
    if (!color || !Color(*color)) {
      return false;
    }
  }
  return true;
}
void RegisterSearchAppearancePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kSearchAppearancePref,
                                   DefaultSearchAppearance());
}
base::DictValue LoadSearchAppearance(const PrefService& prefs) {
  const auto& value = prefs.GetDict(kSearchAppearancePref);
  return ValidateSearchAppearance(value) ? value.Clone()
                                         : DefaultSearchAppearance();
}
bool CanWriteSearchAppearance(const PrefService& prefs) {
  const auto* pref = prefs.FindPreference(kSearchAppearancePref);
  if (!pref || !pref->IsUserModifiable()) {
    return false;
  }
  const auto version = prefs.GetDict(kSearchAppearancePref).FindInt("version");
  return !version || *version == 1;
}
bool WriteSearchAppearance(PrefService& prefs, const base::DictValue& value) {
  if (!CanWriteSearchAppearance(prefs) || !ValidateSearchAppearance(value)) {
    return false;
  }
  prefs.SetDict(kSearchAppearancePref, value.Clone());
  return true;
}
}  // namespace browser
