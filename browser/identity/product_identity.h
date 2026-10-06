#ifndef BROWSER_IDENTITY_PRODUCT_IDENTITY_H_
#define BROWSER_IDENTITY_PRODUCT_IDENTITY_H_

#include <filesystem>
#include <optional>
#include <string_view>

namespace browser {

// These values are intentionally owned by the browser rather than inherited
// from Chromium branding. Keep them stable once released: profile and local
// state directories are part of the on-disk compatibility contract.
struct ProductIdentity final {
  std::string_view product_name;
  // Executable basename; Windows packaging adds the .exe suffix.
  std::string_view executable_name;
  std::string_view application_data_directory_name;
  std::string_view profile_directory_name;

  static constexpr ProductIdentity Defaults() noexcept {
    return {"Browser Final", "browser-final", "BrowserFinal", "Default"};
  }
};

constexpr ProductIdentity GetProductIdentity() noexcept {
  return ProductIdentity::Defaults();
}

struct ProductDataPaths final {
  std::filesystem::path root;
  std::filesystem::path profile;
  std::filesystem::path cache;
  std::filesystem::path local_state;
};

// Constructs paths without creating directories. The profile component must
// be a single portable ASCII directory name (at most 64 bytes). Traversal,
// platform separators, and Windows reserved device names are rejected on
// every platform. No filesystem access or directory creation occurs here.
std::optional<ProductDataPaths> MakeProductDataPaths(
    const std::filesystem::path& data_root,
    std::string_view profile_directory_name =
        ProductIdentity::Defaults().profile_directory_name);

// Returns a platform-appropriate absolute root using standard per-user
// environment variables. Returns nullopt rather than storing profiles in the
// working directory if no valid per-user location exists. Windows uses wide
// environment values so non-ASCII user directories remain intact. Chromium
// integration may instead pass a PathService-resolved root to the function
// above; this module does not override Chromium's profile or cache services.
std::optional<std::filesystem::path> GetDefaultDataRoot();

}  // namespace browser

#endif  // BROWSER_IDENTITY_PRODUCT_IDENTITY_H_
