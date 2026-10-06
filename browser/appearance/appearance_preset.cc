#include "browser/appearance/appearance_preset.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

namespace browser {
namespace {

enum Field : std::uint32_t {
  kSchemaVersion = 0,
  kPresetId,
  kDisplayName,
  kTheme,
  kDensity,
  kTabStyle,
  kAccentColor,
  kBackgroundColor,
  kInterfaceScale,
  kCornerRadius,
  kAnimationScale,
  kTransparency,
  kShowBookmarksBar,
  kShowSidebar,
  kInvalidField,
};

constexpr std::uint32_t kAllFields = (1u << kInvalidField) - 1u;

constexpr std::array<std::string_view, kInvalidField> kFieldNames = {
    "schema_version",
    "preset_id",
    "display_name",
    "theme",
    "density",
    "tab_style",
    "accent_color",
    "background_color",
    "interface_scale",
    "corner_radius",
    "animation_scale",
    "transparency",
    "show_bookmarks_bar",
    "show_sidebar"};

bool IsHexDigit(char character) {
  return (character >= '0' && character <= '9') ||
         (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

bool IsValidColor(std::string_view color) {
  if (color.size() != 7 || color.front() != '#') {
    return false;
  }
  for (std::size_t i = 1; i < color.size(); ++i) {
    if (!IsHexDigit(color[i])) {
      return false;
    }
  }
  return true;
}

bool IsValidIdentifier(std::string_view identifier) {
  if (identifier.empty() || identifier.size() > 32) {
    return false;
  }
  for (std::size_t i = 0; i < identifier.size(); ++i) {
    const char character = identifier[i];
    const bool valid = (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') ||
                       character == '_' || character == '-';
    if (!valid || (i == 0 && character == '-')) {
      return false;
    }
  }
  return true;
}

bool IsValidUtf8(std::string_view value) {
  for (std::size_t i = 0; i < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[i]);
    if (first <= 0x7f) {
      ++i;
      continue;
    }

    std::size_t length = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      length = 2;
      code_point = first & 0x1f;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      length = 3;
      code_point = first & 0x0f;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      length = 4;
      code_point = first & 0x07;
      minimum = 0x10000;
    } else {
      return false;
    }

    if (i + length > value.size()) {
      return false;
    }
    for (std::size_t j = 1; j < length; ++j) {
      const unsigned char continuation =
          static_cast<unsigned char>(value[i + j]);
      if ((continuation & 0xc0) != 0x80) {
        return false;
      }
      code_point = (code_point << 6) | (continuation & 0x3f);
    }
    if (code_point < minimum || code_point > 0x10ffff ||
        (code_point >= 0xd800 && code_point <= 0xdfff)) {
      return false;
    }
    i += length;
  }
  return true;
}

bool IsValidDisplayName(std::string_view name) {
  if (name.empty() || name.size() > 64 || !IsValidUtf8(name)) {
    return false;
  }
  for (const char raw_character : name) {
    const unsigned char character = static_cast<unsigned char>(raw_character);
    if (character < 0x20 || character == 0x7f) {
      return false;
    }
  }
  return true;
}

const char* ThemeName(AppearanceTheme theme) {
  switch (theme) {
    case AppearanceTheme::kSystem:
      return "system";
    case AppearanceTheme::kLight:
      return "light";
    case AppearanceTheme::kDark:
      return "dark";
  }
  return nullptr;
}

const char* DensityName(AppearanceDensity density) {
  switch (density) {
    case AppearanceDensity::kCompact:
      return "compact";
    case AppearanceDensity::kComfortable:
      return "comfortable";
    case AppearanceDensity::kSpacious:
      return "spacious";
  }
  return nullptr;
}

const char* TabStyleName(AppearanceTabStyle style) {
  switch (style) {
    case AppearanceTabStyle::kRounded:
      return "rounded";
    case AppearanceTabStyle::kUnderline:
      return "underline";
    case AppearanceTabStyle::kPill:
      return "pill";
  }
  return nullptr;
}

template <typename Enum>
bool ParseEnum(std::string_view value, Enum* result);

template <>
bool ParseEnum(std::string_view value, AppearanceTheme* result) {
  if (value == "system") {
    *result = AppearanceTheme::kSystem;
  } else if (value == "light") {
    *result = AppearanceTheme::kLight;
  } else if (value == "dark") {
    *result = AppearanceTheme::kDark;
  } else {
    return false;
  }
  return true;
}

template <>
bool ParseEnum(std::string_view value, AppearanceDensity* result) {
  if (value == "compact") {
    *result = AppearanceDensity::kCompact;
  } else if (value == "comfortable") {
    *result = AppearanceDensity::kComfortable;
  } else if (value == "spacious") {
    *result = AppearanceDensity::kSpacious;
  } else {
    return false;
  }
  return true;
}

template <>
bool ParseEnum(std::string_view value, AppearanceTabStyle* result) {
  if (value == "rounded") {
    *result = AppearanceTabStyle::kRounded;
  } else if (value == "underline") {
    *result = AppearanceTabStyle::kUnderline;
  } else if (value == "pill") {
    *result = AppearanceTabStyle::kPill;
  } else {
    return false;
  }
  return true;
}

void AppendJsonString(std::string_view value, std::string* output) {
  output->push_back('"');
  constexpr std::string_view kHex = "0123456789ABCDEF";
  for (const char raw_character : value) {
    const unsigned char character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"':
        output->append("\\\"");
        break;
      case '\\':
        output->append("\\\\");
        break;
      case '\b':
        output->append("\\b");
        break;
      case '\f':
        output->append("\\f");
        break;
      case '\n':
        output->append("\\n");
        break;
      case '\r':
        output->append("\\r");
        break;
      case '\t':
        output->append("\\t");
        break;
      default:
        if (character < 0x20 || character == 0x7f) {
          output->append("\\u00");
          output->push_back(kHex[character >> 4]);
          output->push_back(kHex[character & 0x0f]);
        } else {
          output->push_back(static_cast<char>(character));
        }
    }
  }
  output->push_back('"');
}

bool AppendJsonNumber(double value, std::string* output) {
  std::array<char, 64> buffer{};
  const auto conversion = std::to_chars(
      std::to_address(buffer.begin()), std::to_address(buffer.end()), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (conversion.ec != std::errc()) {
    return false;
  }
  output->append(buffer.data(),
                 static_cast<std::size_t>(conversion.ptr - buffer.data()));
  return true;
}

class JsonParser final {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  AppearancePresetParseResult Parse() {
    if (input_.size() > AppearancePreset::kMaximumJsonBytes) {
      return Fail(AppearancePresetError::kInputTooLarge);
    }
    if (input_.empty()) {
      return Fail(AppearancePresetError::kEmptyInput);
    }
    SkipWhitespace();
    if (position_ == input_.size()) {
      return Fail(AppearancePresetError::kEmptyInput);
    }
    if (!Consume('{')) {
      return Fail(AppearancePresetError::kMalformedJson);
    }

    AppearancePreset preset;
    std::uint32_t fields = 0;
    SkipWhitespace();
    if (!Consume('}')) {
      while (true) {
        std::string key;
        if (!ParseString(&key)) {
          return result_;
        }
        SkipWhitespace();
        if (!Consume(':')) {
          return Fail(AppearancePresetError::kMalformedJson);
        }
        SkipWhitespace();

        const Field field = FieldForKey(key);
        if (field == kInvalidField) {
          return Fail(AppearancePresetError::kUnknownField);
        }
        const std::uint32_t bit = 1u << field;
        if ((fields & bit) != 0) {
          return Fail(AppearancePresetError::kDuplicateField);
        }
        fields |= bit;

        if (!ParseField(field, &preset)) {
          return result_;
        }

        SkipWhitespace();
        if (Consume('}')) {
          break;
        }
        if (!Consume(',')) {
          return Fail(AppearancePresetError::kMalformedJson);
        }
        SkipWhitespace();
        if (Peek() == '}') {
          return Fail(AppearancePresetError::kMalformedJson);
        }
      }
    }

    SkipWhitespace();
    if (position_ != input_.size()) {
      return Fail(AppearancePresetError::kMalformedJson);
    }
    if (fields != kAllFields) {
      return Fail(AppearancePresetError::kMissingField);
    }

    const AppearancePresetError validation = ValidateAppearancePreset(preset);
    if (validation != AppearancePresetError::kNone) {
      return Fail(validation);
    }
    result_.preset = std::move(preset);
    result_.error = AppearancePresetError::kNone;
    result_.error_offset = 0;
    return result_;
  }

 private:
  AppearancePresetParseResult Fail(AppearancePresetError error) {
    result_.preset.reset();
    result_.error = error;
    result_.error_offset = position_;
    return result_;
  }

  char Peek() const {
    return position_ < input_.size() ? input_[position_] : '\0';
  }

  bool Consume(char expected) {
    if (Peek() != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  void SkipWhitespace() {
    while (position_ < input_.size()) {
      const char character = input_[position_];
      if (character != ' ' && character != '\t' && character != '\n' &&
          character != '\r') {
        break;
      }
      ++position_;
    }
  }

  static int HexValue(char character) {
    if (character >= '0' && character <= '9') {
      return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
      return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
      return character - 'A' + 10;
    }
    return -1;
  }

  bool ParseUnicodeEscape(std::string* output) {
    if (position_ + 4 > input_.size()) {
      return false;
    }
    std::uint32_t code_point = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const int digit = HexValue(input_[position_ + i]);
      if (digit < 0) {
        return false;
      }
      code_point = (code_point << 4) | static_cast<std::uint32_t>(digit);
    }
    position_ += 4;

    if (code_point >= 0xd800 && code_point <= 0xdbff) {
      if (position_ + 6 > input_.size() || input_[position_] != '\\' ||
          input_[position_ + 1] != 'u') {
        return false;
      }
      position_ += 2;
      std::uint32_t low = 0;
      for (std::size_t i = 0; i < 4; ++i) {
        const int digit = HexValue(input_[position_ + i]);
        if (digit < 0) {
          return false;
        }
        low = (low << 4) | static_cast<std::uint32_t>(digit);
      }
      if (low < 0xdc00 || low > 0xdfff) {
        return false;
      }
      position_ += 4;
      code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
    } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
      return false;
    }

    if (code_point <= 0x7f) {
      output->push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ff) {
      output->push_back(static_cast<char>(0xc0 | (code_point >> 6)));
      output->push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point <= 0xffff) {
      output->push_back(static_cast<char>(0xe0 | (code_point >> 12)));
      output->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
      output->push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
      output->push_back(static_cast<char>(0xf0 | (code_point >> 18)));
      output->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
      output->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
      output->push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
    return true;
  }

  bool ParseString(std::string* output) {
    if (!Consume('"')) {
      return Fail(AppearancePresetError::kWrongType), false;
    }
    output->clear();
    while (position_ < input_.size()) {
      const unsigned char character =
          static_cast<unsigned char>(input_[position_++]);
      if (character == '"') {
        return true;
      }
      if (character < 0x20) {
        Fail(AppearancePresetError::kMalformedJson);
        return false;
      }
      if (character != '\\') {
        output->push_back(static_cast<char>(character));
        continue;
      }
      if (position_ >= input_.size()) {
        Fail(AppearancePresetError::kMalformedJson);
        return false;
      }
      const char escape = input_[position_++];
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          output->push_back(escape);
          break;
        case 'b':
          output->push_back('\b');
          break;
        case 'f':
          output->push_back('\f');
          break;
        case 'n':
          output->push_back('\n');
          break;
        case 'r':
          output->push_back('\r');
          break;
        case 't':
          output->push_back('\t');
          break;
        case 'u':
          if (!ParseUnicodeEscape(output)) {
            Fail(AppearancePresetError::kMalformedJson);
            return false;
          }
          break;
        default:
          Fail(AppearancePresetError::kMalformedJson);
          return false;
      }
    }
    Fail(AppearancePresetError::kMalformedJson);
    return false;
  }

  bool ParseNumber(double* output) {
    const std::size_t start = position_;
    if (Peek() == '-') {
      ++position_;
    }
    if (Peek() == '0') {
      ++position_;
    } else if (Peek() >= '1' && Peek() <= '9') {
      while (Peek() >= '0' && Peek() <= '9') {
        ++position_;
      }
    } else {
      return Fail(AppearancePresetError::kWrongType), false;
    }
    if (Peek() == '.') {
      ++position_;
      if (!(Peek() >= '0' && Peek() <= '9')) {
        return Fail(AppearancePresetError::kMalformedJson), false;
      }
      while (Peek() >= '0' && Peek() <= '9') {
        ++position_;
      }
    }
    if (Peek() == 'e' || Peek() == 'E') {
      ++position_;
      if (Peek() == '+' || Peek() == '-') {
        ++position_;
      }
      if (!(Peek() >= '0' && Peek() <= '9')) {
        return Fail(AppearancePresetError::kMalformedJson), false;
      }
      while (Peek() >= '0' && Peek() <= '9') {
        ++position_;
      }
    }

    const auto token = input_.substr(start, position_ - start);
    const char* first = token.data();
    const char* last = std::to_address(token.end());
    const auto conversion =
        std::from_chars(first, last, *output, std::chars_format::general);
    if (conversion.ec == std::errc::result_out_of_range ||
        !std::isfinite(*output)) {
      return Fail(AppearancePresetError::kNonFiniteNumber), false;
    }
    if (conversion.ec != std::errc() || conversion.ptr != last) {
      return Fail(AppearancePresetError::kMalformedJson), false;
    }
    return true;
  }

  bool ParseBool(bool* output) {
    if (input_.substr(position_, 4) == "true") {
      *output = true;
      position_ += 4;
      return true;
    }
    if (input_.substr(position_, 5) == "false") {
      *output = false;
      position_ += 5;
      return true;
    }
    return Fail(AppearancePresetError::kWrongType), false;
  }

  static Field FieldForKey(std::string_view key) {
    for (std::size_t i = 0; i < kFieldNames.size(); ++i) {
      if (key == kFieldNames[i]) {
        return static_cast<Field>(i);
      }
    }
    return kInvalidField;
  }

  bool ParseField(Field field, AppearancePreset* preset) {
    std::string string_value;
    double number_value = 0.0;
    bool bool_value = false;
    switch (field) {
      case kSchemaVersion:
        if (!ParseNumber(&number_value)) {
          return false;
        }
        if (number_value != AppearancePreset::kCurrentSchemaVersion) {
          return Fail(AppearancePresetError::kUnsupportedVersion), false;
        }
        preset->schema_version = static_cast<std::uint32_t>(number_value);
        return true;
      case kPresetId:
        if (!ParseString(&string_value)) {
          return false;
        }
        preset->preset_id = std::move(string_value);
        return true;
      case kDisplayName:
        if (!ParseString(&string_value)) {
          return false;
        }
        preset->display_name = std::move(string_value);
        return true;
      case kTheme:
        if (!ParseString(&string_value) ||
            !ParseEnum(string_value, &preset->theme)) {
          if (result_.error == AppearancePresetError::kNone) {
            Fail(AppearancePresetError::kInvalidEnum);
          }
          return false;
        }
        return true;
      case kDensity:
        if (!ParseString(&string_value) ||
            !ParseEnum(string_value, &preset->density)) {
          if (result_.error == AppearancePresetError::kNone) {
            Fail(AppearancePresetError::kInvalidEnum);
          }
          return false;
        }
        return true;
      case kTabStyle:
        if (!ParseString(&string_value) ||
            !ParseEnum(string_value, &preset->tab_style)) {
          if (result_.error == AppearancePresetError::kNone) {
            Fail(AppearancePresetError::kInvalidEnum);
          }
          return false;
        }
        return true;
      case kAccentColor:
        if (!ParseString(&string_value)) {
          return false;
        }
        preset->accent_color = std::move(string_value);
        return true;
      case kBackgroundColor:
        if (!ParseString(&string_value)) {
          return false;
        }
        preset->background_color = std::move(string_value);
        return true;
      case kInterfaceScale:
        if (!ParseNumber(&number_value)) {
          return false;
        }
        preset->interface_scale = number_value;
        return true;
      case kCornerRadius:
        if (!ParseNumber(&number_value)) {
          return false;
        }
        preset->corner_radius = number_value;
        return true;
      case kAnimationScale:
        if (!ParseNumber(&number_value)) {
          return false;
        }
        preset->animation_scale = number_value;
        return true;
      case kTransparency:
        if (!ParseNumber(&number_value)) {
          return false;
        }
        preset->transparency = number_value;
        return true;
      case kShowBookmarksBar:
        if (!ParseBool(&bool_value)) {
          return false;
        }
        preset->show_bookmarks_bar = bool_value;
        return true;
      case kShowSidebar:
        if (!ParseBool(&bool_value)) {
          return false;
        }
        preset->show_sidebar = bool_value;
        return true;
      case kInvalidField:
        break;
    }
    return Fail(AppearancePresetError::kMalformedJson), false;
  }

  std::string_view input_;
  std::size_t position_ = 0;
  AppearancePresetParseResult result_;
};

}  // namespace

AppearancePreset AppearancePreset::Default() {
  return AppearancePreset();
}

AppearancePreset AppearancePreset::Dark() {
  AppearancePreset preset;
  preset.preset_id = "dark";
  preset.display_name = "Dark";
  preset.theme = AppearanceTheme::kDark;
  preset.accent_color = "#8AB4F8";
  preset.background_color = "#202124";
  preset.show_sidebar = true;
  return preset;
}

AppearancePreset AppearancePreset::Compact() {
  AppearancePreset preset;
  preset.preset_id = "compact";
  preset.display_name = "Compact";
  preset.density = AppearanceDensity::kCompact;
  preset.tab_style = AppearanceTabStyle::kUnderline;
  preset.interface_scale = 0.9;
  preset.corner_radius = 4.0;
  preset.animation_scale = 0.75;
  preset.show_bookmarks_bar = false;
  return preset;
}

bool operator==(const AppearancePreset& left, const AppearancePreset& right) {
  return left.schema_version == right.schema_version &&
         left.preset_id == right.preset_id &&
         left.display_name == right.display_name && left.theme == right.theme &&
         left.density == right.density && left.tab_style == right.tab_style &&
         left.accent_color == right.accent_color &&
         left.background_color == right.background_color &&
         left.interface_scale == right.interface_scale &&
         left.corner_radius == right.corner_radius &&
         left.animation_scale == right.animation_scale &&
         left.transparency == right.transparency &&
         left.show_bookmarks_bar == right.show_bookmarks_bar &&
         left.show_sidebar == right.show_sidebar;
}

bool operator!=(const AppearancePreset& left, const AppearancePreset& right) {
  return !(left == right);
}

AppearancePresetError ValidateAppearancePreset(const AppearancePreset& preset) {
  if (preset.schema_version != AppearancePreset::kCurrentSchemaVersion) {
    return AppearancePresetError::kUnsupportedVersion;
  }
  if (!IsValidIdentifier(preset.preset_id)) {
    return AppearancePresetError::kInvalidIdentifier;
  }
  if (!IsValidDisplayName(preset.display_name)) {
    return AppearancePresetError::kInvalidString;
  }
  if (ThemeName(preset.theme) == nullptr ||
      DensityName(preset.density) == nullptr ||
      TabStyleName(preset.tab_style) == nullptr) {
    return AppearancePresetError::kInvalidEnum;
  }
  if (!IsValidColor(preset.accent_color) ||
      !IsValidColor(preset.background_color)) {
    return AppearancePresetError::kInvalidColor;
  }
  if (!std::isfinite(preset.interface_scale) ||
      !std::isfinite(preset.corner_radius) ||
      !std::isfinite(preset.animation_scale) ||
      !std::isfinite(preset.transparency)) {
    return AppearancePresetError::kNonFiniteNumber;
  }
  if (preset.interface_scale < 0.8 || preset.interface_scale > 1.5 ||
      preset.corner_radius < 0.0 || preset.corner_radius > 24.0 ||
      preset.animation_scale < 0.0 || preset.animation_scale > 2.0 ||
      preset.transparency < 0.0 || preset.transparency > 1.0) {
    return AppearancePresetError::kOutOfRange;
  }
  return AppearancePresetError::kNone;
}

std::optional<std::string> ExportAppearancePreset(
    const AppearancePreset& preset) {
  if (ValidateAppearancePreset(preset) != AppearancePresetError::kNone) {
    return std::nullopt;
  }

  std::string json;
  json.reserve(384);
  json.append("{\"schema_version\":");
  json.append(std::to_string(preset.schema_version));
  json.append(",\"preset_id\":");
  AppendJsonString(preset.preset_id, &json);
  json.append(",\"display_name\":");
  AppendJsonString(preset.display_name, &json);
  json.append(",\"theme\":");
  AppendJsonString(ThemeName(preset.theme), &json);
  json.append(",\"density\":");
  AppendJsonString(DensityName(preset.density), &json);
  json.append(",\"tab_style\":");
  AppendJsonString(TabStyleName(preset.tab_style), &json);
  json.append(",\"accent_color\":");
  AppendJsonString(preset.accent_color, &json);
  json.append(",\"background_color\":");
  AppendJsonString(preset.background_color, &json);
  json.append(",\"interface_scale\":");
  if (!AppendJsonNumber(preset.interface_scale, &json)) {
    return std::nullopt;
  }
  json.append(",\"corner_radius\":");
  if (!AppendJsonNumber(preset.corner_radius, &json)) {
    return std::nullopt;
  }
  json.append(",\"animation_scale\":");
  if (!AppendJsonNumber(preset.animation_scale, &json)) {
    return std::nullopt;
  }
  json.append(",\"transparency\":");
  if (!AppendJsonNumber(preset.transparency, &json)) {
    return std::nullopt;
  }
  json.append(",\"show_bookmarks_bar\":");
  json.append(preset.show_bookmarks_bar ? "true" : "false");
  json.append(",\"show_sidebar\":");
  json.append(preset.show_sidebar ? "true" : "false");
  json.push_back('}');
  return json;
}

AppearancePresetParseResult ImportAppearancePreset(std::string_view json) {
  return JsonParser(json).Parse();
}

std::array<AppearancePreset, 3> BuiltInAppearancePresets() {
  return {AppearancePreset::Default(), AppearancePreset::Dark(),
          AppearancePreset::Compact()};
}

}  // namespace browser
