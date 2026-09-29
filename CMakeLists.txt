cmake_minimum_required(VERSION 3.16)
project(wow_addon_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WAM_BUILD_GUI "Build the Qt6/Kirigami GUI (needs Qt 6.5+ and Kirigami at runtime)" ON)

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
    src/core/toc_reader.cpp
    src/core/reconciler.cpp
)
target_include_directories(wam_core PUBLIC src)
target_link_libraries(wam_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json PkgConfig::LIBZIP OpenSSL::Crypto)

add_executable(wam src/cli/main.cpp)
target_link_libraries(wam PRIVATE wam_core)

if(WAM_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2)

    # qt_add_qml_module() needs AUTOMOC on the target (it generates the QML
    # type metadata from moc output). Set before any GUI target is created.
    set(CMAKE_AUTOMOC ON)

    # Qt-facing adapter over wam_core: one worker thread owns all core state,
    # the controller/models are what QML talks to.
    add_library(wam_gui STATIC
        src/gui/wam_worker.cpp
        src/gui/wam_controller.cpp
        src/gui/search_results_model.cpp
        src/gui/installed_addons_model.cpp
        src/gui/pending_updates_model.cpp
        src/gui/scan_results_model.cpp
        src/gui/install_files_model.cpp
    )
    target_include_directories(wam_gui PUBLIC src)
    target_link_libraries(wam_gui PUBLIC wam_core Qt6::Core Qt6::Gui Qt6::Qml)

    qt_add_executable(wam-gui src/gui/main.cpp)
    qt_add_qml_module(wam-gui
        URI Wam
        VERSION 1.0
        QML_FILES
            src/gui/qml/Main.qml
            src/gui/qml/AddonIcon.qml
            src/gui/qml/InstalledPage.qml
            src/gui/qml/SearchPage.qml
            src/gui/qml/ScanDialog.qml
            src/gui/qml/SettingsPage.qml
            src/gui/qml/UpdatesDialog.qml
    )
    target_link_libraries(wam-gui PRIVATE wam_gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2)

    install(TARGETS wam-gui RUNTIME DESTINATION bin)
    install(FILES data/wow-addon-manager.desktop DESTINATION share/applications)
endif()

install(TARGETS wam RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(tests)
