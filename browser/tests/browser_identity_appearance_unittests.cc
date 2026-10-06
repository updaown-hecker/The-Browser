#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <string>
#include <string_view>

#include "browser/appearance/appearance_preset.h"
#include "browser/identity/product_identity.h"

namespace {

int failures = 0;

void Expect(bool condition, std::string_view message) {
  if (condition) {
    return;
  }
  ++failures;
  std::cerr << "[FAIL] " << message << '\n';
}

void TestProductIdentityAndPaths() {
  const browser::ProductIdentity identity = browser::GetProductIdentity();
  Expect(identity.product_name == "Browser Final", "product name is stable");
  Expect(identity.executable_name == "browser-final",
         "executable name is stable");
  Expect(identity.application_data_directory_name == "BrowserFinal",
         "application data directory is stable");
  Expect(identity.profile_directory_name == "Default",
         "default profile directory is stable");

  const auto paths = browser::MakeProductDataPaths(
      std::filesystem::path("/tmp/browser-test-data"));
  Expect(paths.has_value(), "default data paths are accepted");
  if (paths.has_value()) {
    Expect(paths->root == std::filesystem::path("/tmp/browser-test-data"),
           "root path is preserved");
    Expect(paths->profile ==
               std::filesystem::path("/tmp/browser-test-data") / "Default",
           "profile path is deterministic");
    Expect(paths->cache == paths->profile / "Cache",
           "cache path is under profile");
    Expect(paths->local_state ==
               std::filesystem::path("/tmp/browser-test-data") / "Local State",
           "local state path is under product root");
  }

  Expect(browser::MakeProductDataPaths("/tmp/browser-test-data", "Profile_1")
             .has_value(),
         "safe custom profile name is accepted");
  Expect(!browser::MakeProductDataPaths("/tmp/browser-test-data", "../escape")
              .has_value(),
         "parent traversal profile is rejected");
  Expect(!browser::MakeProductDataPaths("/tmp/browser-test-data", "a/b")
              .has_value(),
         "separator in profile name is rejected");
  Expect(!browser::MakeProductDataPaths({}, "Default").has_value(),
         "empty data root is rejected");
  for (const std::string_view unsafe :
       {".", "..", "a\\b", "NUL", "con", "COM1", "lpt9", "name.", "name ",
        "name:stream", "a*", "a\"b", "a?b"}) {
    Expect(!browser::MakeProductDataPaths("/tmp/browser-test-data", unsafe)
                .has_value(),
           "non-portable profile name is rejected");
  }
  Expect(!browser::MakeProductDataPaths("/tmp/browser-test-data",
                                        std::string(65, 'a'))
              .has_value(),
         "oversized profile name is rejected");
  const auto root = browser::GetDefaultDataRoot();
  Expect(!root.has_value() || root->is_absolute(),
         "default root is absolute or unavailable");
}

#if !defined(_WIN32)
class ScopedEnvironmentVariable final {
 public:
  ScopedEnvironmentVariable(const char* name, const char* value) : name_(name) {
    const char* original = std::getenv(name);
    if (original != nullptr) {
      original_ = original;
    }
    Set(value);
  }

  ~ScopedEnvironmentVariable() {
    Set(original_.has_value() ? original_->c_str() : nullptr);
  }

  void Set(const char* value) {
    const int status =
        value == nullptr ? unsetenv(name_) : setenv(name_, value, 1);
    Expect(status == 0, "test environment update succeeds");
  }

 private:
  const char* name_;
  std::optional<std::string> original_;
};

void TestLinuxDefaultDataRoot() {
  ScopedEnvironmentVariable home("HOME", "/tmp/browser-user");
  ScopedEnvironmentVariable config_home("XDG_CONFIG_HOME",
                                        "/tmp/browser-config");
  Expect(
      browser::GetDefaultDataRoot() == std::optional<std::filesystem::path>(
                                           "/tmp/browser-config/BrowserFinal"),
      "absolute XDG configuration root takes precedence");
  config_home.Set("relative-ignored");
  Expect(browser::GetDefaultDataRoot() ==
             std::optional<std::filesystem::path>(
                 "/tmp/browser-user/.config/BrowserFinal"),
         "relative XDG configuration root falls back to absolute home");
  config_home.Set(nullptr);
  home.Set(nullptr);
  Expect(!browser::GetDefaultDataRoot().has_value(),
         "missing user environment does not fall back to working directory");
  home.Set("relative-home");
  Expect(!browser::GetDefaultDataRoot().has_value(),
         "relative home directory is rejected");
}
#endif

void TestAppearanceRoundTrip() {
  for (const browser::AppearancePreset& preset :
       browser::BuiltInAppearancePresets()) {
    Expect(browser::ValidateAppearancePreset(preset) ==
               browser::AppearancePresetError::kNone,
           "built-in preset validates");
    const auto exported = browser::ExportAppearancePreset(preset);
    Expect(exported.has_value(), "valid preset exports");
    if (!exported.has_value()) {
      continue;
    }
    Expect(exported->find("password") == std::string::npos &&
               exported->find("token") == std::string::npos,
           "export contains no secret fields");
    const browser::AppearancePresetParseResult imported =
        browser::ImportAppearancePreset(*exported);
    Expect(imported.ok(), "exported preset imports");
    if (imported.ok()) {
      Expect(*imported.preset == preset, "preset survives export/import");
    }
  }

  browser::AppearancePreset custom = browser::AppearancePreset::Default();
  custom.preset_id = "night_mode";
  custom.display_name = "Night \"Mode\"";
  custom.theme = browser::AppearanceTheme::kDark;
  custom.accent_color = "#abcdef";
  custom.interface_scale = 1.25;
  custom.transparency = 0.35;
  custom.show_sidebar = true;
  const auto exported = browser::ExportAppearancePreset(custom);
  Expect(exported.has_value(), "custom preset exports");
  if (exported.has_value()) {
    const auto imported = browser::ImportAppearancePreset(*exported);
    Expect(imported.ok(), "escaped display name imports");
    if (imported.ok()) {
      Expect(*imported.preset == custom, "custom preset round-trips");
    }
  }
}

std::string ValidPresetJson() {
  const auto exported =
      browser::ExportAppearancePreset(browser::AppearancePreset::Default());
  return exported.value_or(std::string());
}

std::string ReplaceOnce(std::string value,
                        std::string_view needle,
                        std::string_view replacement) {
  const std::size_t position = value.find(needle);
  Expect(position != std::string::npos, "test replacement target exists");
  if (position != std::string::npos) {
    value.replace(position, needle.size(), replacement);
  }
  return value;
}

void ExpectImportError(std::string_view json,
                       browser::AppearancePresetError error) {
  const auto result = browser::ImportAppearancePreset(json);
  Expect(!result.ok() && !result.preset.has_value(),
         "failed import never yields a partial preset");
  Expect(result.error == error, "import returns expected failure category");
  Expect(result.error_offset <= json.size(), "error offset is in bounds");
}

void TestAppearanceValidationAndImportFailures() {
  browser::AppearancePreset invalid = browser::AppearancePreset::Default();
  invalid.interface_scale = 2.0;
  Expect(browser::ValidateAppearancePreset(invalid) ==
             browser::AppearancePresetError::kOutOfRange,
         "out-of-range numeric value is rejected");
  Expect(!browser::ExportAppearancePreset(invalid).has_value(),
         "invalid preset is not exported");

  invalid = browser::AppearancePreset::Default();
  invalid.accent_color = "red";
  Expect(browser::ValidateAppearancePreset(invalid) ==
             browser::AppearancePresetError::kInvalidColor,
         "non-hex color is rejected");

  const std::string valid = ValidPresetJson();
  const auto whitespace =
      browser::ImportAppearancePreset(" \n\t" + valid + "\r ");
  Expect(whitespace.ok(), "JSON whitespace around object is accepted");
  const std::size_t first_comma = valid.find(',');
  const std::string reordered =
      "{" + valid.substr(first_comma + 1, valid.size() - first_comma - 2) +
      ",\"schema_version\":1}";
  const auto reordered_result = browser::ImportAppearancePreset(reordered);
  Expect(reordered_result.ok() &&
             *reordered_result.preset == browser::AppearancePreset::Default(),
         "import field order is unrestricted");
  Expect(browser::ImportAppearancePreset(valid + " trailing").error ==
             browser::AppearancePresetError::kMalformedJson,
         "trailing data is rejected");
  Expect(browser::ImportAppearancePreset("[]").error ==
             browser::AppearancePresetError::kMalformedJson,
         "non-object root is rejected");
  Expect(browser::ImportAppearancePreset("{}").error ==
             browser::AppearancePresetError::kMissingField,
         "missing fields are rejected");

  std::string unknown = valid;
  unknown.insert(unknown.size() - 1, ",\"password\":\"not-stored\"");
  Expect(browser::ImportAppearancePreset(unknown).error ==
             browser::AppearancePresetError::kUnknownField,
         "unknown and secret-looking fields are rejected");

  std::string duplicate = valid;
  duplicate.insert(duplicate.size() - 1, ",\"theme\":\"light\"");
  Expect(browser::ImportAppearancePreset(duplicate).error ==
             browser::AppearancePresetError::kDuplicateField,
         "duplicate fields are rejected");
  std::string escaped_duplicate = valid;
  escaped_duplicate.insert(escaped_duplicate.size() - 1,
                           ",\"\\u0074heme\":\"dark\"");
  ExpectImportError(escaped_duplicate,
                    browser::AppearancePresetError::kDuplicateField);

  std::string bad_number = valid;
  const std::string needle = "\"interface_scale\":1";
  const std::size_t number_position = bad_number.find(needle);
  Expect(number_position != std::string::npos,
         "canonical export contains interface scale");
  if (number_position != std::string::npos) {
    bad_number.replace(number_position, needle.size(), "\"interface_scale\":9");
    Expect(browser::ImportAppearancePreset(bad_number).error ==
               browser::AppearancePresetError::kOutOfRange,
           "out-of-range imported number is rejected");
  }

  std::string bad_enum = valid;
  const std::size_t theme_position = bad_enum.find("\"theme\":\"system\"");
  Expect(theme_position != std::string::npos,
         "canonical export contains theme");
  if (theme_position != std::string::npos) {
    bad_enum.replace(theme_position, std::string("\"theme\":\"system\"").size(),
                     "\"theme\":\"neon\"");
    Expect(browser::ImportAppearancePreset(bad_enum).error ==
               browser::AppearancePresetError::kInvalidEnum,
           "unknown enum value is rejected");
  }

  Expect(browser::ImportAppearancePreset(" ").error ==
             browser::AppearancePresetError::kEmptyInput,
         "whitespace-only input is rejected");
}

void TestBoundsAndMalformedValues() {
  using Error = browser::AppearancePresetError;
  const std::string valid = ValidPresetJson();
  ExpectImportError("", Error::kEmptyInput);
  ExpectImportError(
      std::string(browser::AppearancePreset::kMaximumJsonBytes + 1, ' '),
      Error::kInputTooLarge);
  ExpectImportError(
      ReplaceOnce(valid, "\"schema_version\":1", "\"schema_version\":2"),
      Error::kUnsupportedVersion);
  ExpectImportError(
      ReplaceOnce(valid, "\"schema_version\":1", "\"schema_version\":1.5"),
      Error::kUnsupportedVersion);
  ExpectImportError(ReplaceOnce(valid, "\"preset_id\":\"default\"",
                                "\"preset_id\":\"../escape\""),
                    Error::kInvalidIdentifier);
  ExpectImportError(ReplaceOnce(valid, "\"accent_color\":\"#4F6BED\"",
                                "\"accent_color\":\"#12345678\""),
                    Error::kInvalidColor);
  ExpectImportError(ReplaceOnce(valid, "\"accent_color\":\"#4F6BED\"",
                                "\"accent_color\":\"#GGGGGG\""),
                    Error::kInvalidColor);
  ExpectImportError(
      ReplaceOnce(valid, "\"show_sidebar\":false", "\"show_sidebar\":1"),
      Error::kWrongType);
  ExpectImportError(
      ReplaceOnce(valid, "\"theme\":\"system\"", "\"theme\":null"),
      Error::kWrongType);
  ExpectImportError(
      ReplaceOnce(valid, "\"interface_scale\":1", "\"interface_scale\":1e9999"),
      Error::kNonFiniteNumber);
  ExpectImportError(
      ReplaceOnce(valid, "\"interface_scale\":1", "\"interface_scale\":NaN"),
      Error::kWrongType);
  for (const std::string_view invalid_number :
       {"01", "+1", "1.", "1e", "--1"}) {
    const auto result = browser::ImportAppearancePreset(ReplaceOnce(
        valid, "\"interface_scale\":1",
        std::string("\"interface_scale\":") + std::string(invalid_number)));
    Expect(!result.ok(), "invalid JSON numeric grammar is rejected");
  }
  ExpectImportError(
      ReplaceOnce(valid, "\"display_name\":\"Browser Final Default\"",
                  "\"display_name\":\"bad\\nname\""),
      Error::kInvalidString);
  ExpectImportError(
      ReplaceOnce(valid, "\"display_name\":\"Browser Final Default\"",
                  "\"display_name\":\"bad\\uD800name\""),
      Error::kMalformedJson);
  ExpectImportError(
      ReplaceOnce(valid, "\"display_name\":\"Browser Final Default\"",
                  "\"display_name\":\"bad\\uDC00name\""),
      Error::kMalformedJson);
  ExpectImportError(
      ReplaceOnce(valid, "\"display_name\":\"Browser Final Default\"",
                  "\"display_name\":\"bad\\qname\""),
      Error::kMalformedJson);
  std::string invalid_utf8 = "\"display_name\":\"";
  invalid_utf8.append("\xC0\xAF");
  invalid_utf8.push_back('"');
  ExpectImportError(
      ReplaceOnce(valid, "\"display_name\":\"Browser Final Default\"",
                  invalid_utf8),
      Error::kInvalidString);

  for (std::size_t length = 0; length < valid.size(); ++length) {
    Expect(!browser::ImportAppearancePreset(
                std::string_view(valid).substr(0, length))
                .ok(),
           "every truncated valid document is rejected");
  }

  browser::AppearancePreset preset;
  preset.display_name = std::string(64, 'a');
  Expect(browser::ValidateAppearancePreset(preset) == Error::kNone,
         "maximum length display name is valid");
  preset.display_name.push_back('a');
  Expect(browser::ValidateAppearancePreset(preset) == Error::kInvalidString,
         "oversized display name is rejected");
  preset = browser::AppearancePreset::Default();
  preset.preset_id = std::string(32, 'a');
  Expect(browser::ValidateAppearancePreset(preset) == Error::kNone,
         "maximum length identifier is valid");
  preset.preset_id.push_back('a');
  Expect(browser::ValidateAppearancePreset(preset) == Error::kInvalidIdentifier,
         "oversized identifier is rejected");
  preset = browser::AppearancePreset::Default();
  preset.theme = static_cast<browser::AppearanceTheme>(99);
  Expect(browser::ValidateAppearancePreset(preset) == Error::kInvalidEnum,
         "invalid native enum is rejected before export");
  preset = browser::AppearancePreset::Default();
  preset.interface_scale = std::numeric_limits<double>::quiet_NaN();
  Expect(browser::ValidateAppearancePreset(preset) == Error::kNonFiniteNumber,
         "native NaN is rejected");
  preset.interface_scale = std::numeric_limits<double>::infinity();
  Expect(!browser::ExportAppearancePreset(preset).has_value(),
         "native infinity is not exported");

  struct NumericBound {
    double browser::AppearancePreset::* member;
    double minimum;
    double maximum;
  };
  for (const NumericBound bound :
       {NumericBound{&browser::AppearancePreset::interface_scale, 0.8, 1.5},
        NumericBound{&browser::AppearancePreset::corner_radius, 0.0, 24.0},
        NumericBound{&browser::AppearancePreset::animation_scale, 0.0, 2.0},
        NumericBound{&browser::AppearancePreset::transparency, 0.0, 1.0}}) {
    for (const double accepted : {bound.minimum, bound.maximum}) {
      preset = browser::AppearancePreset::Default();
      preset.*bound.member = accepted;
      Expect(browser::ExportAppearancePreset(preset).has_value(),
             "numeric bounds are inclusive");
    }
    for (const double rejected : {bound.minimum - 0.01, bound.maximum + 0.01}) {
      preset = browser::AppearancePreset::Default();
      preset.*bound.member = rejected;
      Expect(browser::ValidateAppearancePreset(preset) == Error::kOutOfRange,
             "values outside each numeric bound are rejected");
    }
  }
}

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
// libstdc++ inspects user-defined facets with dynamic_cast. Do not install one
// in Chromium's no-RTTI build; the default CMake test exercises this locale.
class CommaDecimalPoint final : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
};
#endif

void TestUnicodeAndLocaleIndependence() {
  const std::string valid = ValidPresetJson();
  const auto unicode = browser::ImportAppearancePreset(
      ReplaceOnce(valid, "\"display_name\":\"Browser Final Default\"",
                  "\"display_name\":\"Caf\\u00E9 \\uD83D\\uDE80\""));
  Expect(unicode.ok(), "valid unicode escape and surrogate pair import");
  if (unicode.ok()) {
    Expect(unicode.preset->display_name == "Caf\xC3\xA9 \xF0\x9F\x9A\x80",
           "unicode escapes decode to UTF-8");
    const auto exported = browser::ExportAppearancePreset(*unicode.preset);
    Expect(
        exported.has_value() && browser::ImportAppearancePreset(*exported).ok(),
        "unicode preset round-trips");
  }

  const std::locale old_locale = std::locale();
#if defined(__GXX_RTTI) || defined(_CPPRTTI)
  std::locale::global(
      std::locale(std::locale::classic(), new CommaDecimalPoint));
#endif
  browser::AppearancePreset preset;
  preset.interface_scale = 1.25;
  const auto exported = browser::ExportAppearancePreset(preset);
  Expect(exported.has_value() &&
             exported->find("\"interface_scale\":1.25") != std::string::npos,
         "export uses JSON decimal point regardless of locale");
  if (exported.has_value()) {
    const auto imported = browser::ImportAppearancePreset(*exported);
    Expect(imported.ok() && imported.preset->interface_scale == 1.25,
           "import is locale independent");
  }
  std::locale::global(old_locale);
}

}  // namespace

int main() {
  TestProductIdentityAndPaths();
#if !defined(_WIN32)
  TestLinuxDefaultDataRoot();
#endif
  TestAppearanceRoundTrip();
  TestAppearanceValidationAndImportFailures();
  TestBoundsAndMalformedValues();
  TestUnicodeAndLocaleIndependence();
  if (failures == 0) {
    std::cout << "All browser identity/appearance tests passed.\n";
    return 0;
  }
  std::cerr << failures << " test expectation(s) failed.\n";
  return 1;
}
