#ifndef BROWSER_APPEARANCE_APPEARANCE_PRESET_H_
#define BROWSER_APPEARANCE_APPEARANCE_PRESET_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace browser {

enum class AppearanceTheme {
  kSystem,
  kLight,
  kDark,
};

enum class AppearanceDensity {
  kCompact,
  kComfortable,
  kSpacious,
};

enum class AppearanceTabStyle {
  kRounded,
  kUnderline,
  kPill,
};

enum class AppearancePresetError {
  kNone,
  kEmptyInput,
  kInputTooLarge,
  kMalformedJson,
  kUnknownField,
  kDuplicateField,
  kMissingField,
  kWrongType,
  kUnsupportedVersion,
  kInvalidEnum,
  kInvalidIdentifier,
  kInvalidString,
  kInvalidColor,
  kOutOfRange,
  kNonFiniteNumber,
};

struct AppearancePreset final {
  static constexpr std::uint32_t kCurrentSchemaVersion = 1;
  static constexpr std::size_t kMaximumJsonBytes = 4096;

  std::uint32_t schema_version = kCurrentSchemaVersion;
  std::string preset_id = "default";  // 1-32 ASCII identifier bytes.
  std::string display_name = "Browser Final Default";  // 1-64 UTF-8 bytes.
  AppearanceTheme theme = AppearanceTheme::kSystem;
  AppearanceDensity density = AppearanceDensity::kComfortable;
  AppearanceTabStyle tab_style = AppearanceTabStyle::kRounded;
  std::string accent_color = "#4F6BED";  // RGB only, exactly #RRGGBB.
  std::string background_color = "#FFFFFF";
  double interface_scale = 1.0;  // [0.8, 1.5].
  double corner_radius = 8.0;    // [0, 24] device-independent pixels.
  double animation_scale = 1.0;  // [0, 2]; 0 disables optional animations.
  double transparency = 0.0;     // [0, 1]; callers handle native fallbacks.
  bool show_bookmarks_bar = true;
  bool show_sidebar = false;

  static AppearancePreset Default();
  static AppearancePreset Dark();
  static AppearancePreset Compact();
};

bool operator==(const AppearancePreset& left, const AppearancePreset& right);
bool operator!=(const AppearancePreset& left, const AppearancePreset& right);

// The result has a value only when error == kNone. error_offset is the byte
// offset reached when parsing or validation failed. No partially imported
// preset is returned on failure.
struct AppearancePresetParseResult final {
  std::optional<AppearancePreset> preset;
  AppearancePresetError error = AppearancePresetError::kNone;
  std::size_t error_offset = 0;

  bool ok() const { return preset.has_value(); }
};

AppearancePresetError ValidateAppearancePreset(const AppearancePreset& preset);

// Produces canonical JSON with a fixed key order. It returns nullopt rather
// than serializing invalid data. The schema intentionally contains no secret
// or credential fields; unknown input fields are rejected on import.
std::optional<std::string> ExportAppearancePreset(
    const AppearancePreset& preset);

// Accepts at most kMaximumJsonBytes. The flat schema has no recursive parsing,
// arbitrary file paths, URLs, executable code, or credential fields.
AppearancePresetParseResult ImportAppearancePreset(std::string_view json);

std::array<AppearancePreset, 3> BuiltInAppearancePresets();

}  // namespace browser

#endif  // BROWSER_APPEARANCE_APPEARANCE_PRESET_H_
