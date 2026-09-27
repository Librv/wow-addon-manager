cmake_minimum_required(VERSION 3.16)
project(wow_addon_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# The libzip-dev package's own CMake config on some distros (e.g. Debian/
# Ubuntu) references helper binaries (zipcmp/zipmerge/ziptool) that aren't
# always installed alongside the library, which breaks find_package(libzip).
# pkg-config is more portable here.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

add_library(wam_core
    src/core/config.cpp
    src/core/http_client.cpp
    src/core/curseforge_client.cpp
    src/core/addon_installer.cpp
    src/core/state_store.cpp
)
target_include_directories(wam_core PUBLIC src)
target_link_libraries(wam_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json PkgConfig::LIBZIP OpenSSL::Crypto)

add_executable(wam src/cli/main.cpp)
target_link_libraries(wam PRIVATE wam_core)

enable_testing()
add_subdirectory(tests)
