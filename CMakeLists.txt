cmake_minimum_required(VERSION 3.16)

project(browser_identity_appearance LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(browser_identity_appearance STATIC
  browser/appearance/appearance_preset.cc
  browser/appearance/appearance_preset.h
  browser/identity/product_identity.cc
  browser/identity/product_identity.h
)

target_compile_features(browser_identity_appearance PUBLIC cxx_std_23)
target_include_directories(browser_identity_appearance PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(browser_identity_appearance_unittests
  browser/tests/browser_identity_appearance_unittests.cc
)
target_compile_features(browser_identity_appearance_unittests PRIVATE cxx_std_23)
target_link_libraries(browser_identity_appearance_unittests PRIVATE
  browser_identity_appearance
)

enable_testing()
add_test(NAME browser_identity_appearance_unittests
         COMMAND browser_identity_appearance_unittests)
