#include "browser/identity/product_identity.h"

#include <cstdlib>
#include <string>

namespace browser {
namespace {

bool IsSafeProfileDirectoryName(std::string_view name) {
  if (name.empty() || name.size() > 64 || name.front() == ' ' ||
      name.back() == ' ') {
    return false;
  }

  std::string uppercase_name;
  uppercase_name.reserve(name.size());
  for (const char character : name) {
    const bool is_lowercase = character >= 'a' && character <= 'z';
    if (!is_lowercase && !(character >= 'A' && character <= 'Z') &&
        !(character >= '0' && character <= '9') && character != '_' &&
        character != '-' && character != ' ') {
      return false;
    }
    uppercase_name.push_back(is_lowercase ? static_cast<char>(character - 32)
                                          : character);
  }
  if (uppercase_name == "CON" || uppercase_name == "PRN" ||
      uppercase_name == "AUX" || uppercase_name == "NUL" ||
      (uppercase_name.size() == 4 &&
       (uppercase_name.substr(0, 3) == "COM" ||
        uppercase_name.substr(0, 3) == "LPT") &&
       uppercase_name.back() >= '1' && uppercase_name.back() <= '9')) {
    return false;
  }
  return true;
}

#if defined(_WIN32)
std::filesystem::path EnvironmentPath(const wchar_t* name) {
  const wchar_t* value = _wgetenv(name);
  return value == nullptr || *value == L'\0' ? std::filesystem::path()
                                             : std::filesystem::path(value);
}
#else
std::filesystem::path EnvironmentPath(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr || *value == '\0' ? std::filesystem::path()
                                            : std::filesystem::path(value);
}
#endif

}  // namespace

std::optional<ProductDataPaths> MakeProductDataPaths(
    const std::filesystem::path& data_root,
    std::string_view profile_directory_name) {
  if (data_root.empty() ||
      !IsSafeProfileDirectoryName(profile_directory_name)) {
    return std::nullopt;
  }

  const std::filesystem::path profile_name{std::string(profile_directory_name)};
  ProductDataPaths paths;
  paths.root = data_root;
  paths.profile = data_root / profile_name;
  paths.cache = paths.profile / "Cache";
  paths.local_state = data_root / "Local State";
  return paths;
}

std::optional<std::filesystem::path> GetDefaultDataRoot() {
  const ProductIdentity identity = GetProductIdentity();

#if defined(_WIN32)
  std::filesystem::path root = EnvironmentPath(L"LOCALAPPDATA");
  if (!root.is_absolute()) {
    root = EnvironmentPath(L"USERPROFILE");
    if (root.is_absolute()) {
      root /= L"AppData";
      root /= L"Local";
    }
  }
#else
  std::filesystem::path root = EnvironmentPath("XDG_CONFIG_HOME");
  if (!root.is_absolute()) {
    root = EnvironmentPath("HOME");
    if (root.is_absolute()) {
      root /= ".config";
    }
  }
#endif

  if (!root.is_absolute()) {
    return std::nullopt;
  }
  return root / std::filesystem::path(identity.application_data_directory_name);
}

}  // namespace browser
