#!/usr/bin/env bash
# Idle RAM test of the real wam-gui (real QML, real Kirigami) in the Ubuntu 24.04
# sandbox, for A/B comparison of code changes. See docs/ram-testing.md.
#
# usage: tools/ram-test.sh [--setup-only] [--seed [N]] [--runs N] [--wait SEC] [--software]
#   --setup-only  install deps and build Kirigami, then stop
#   --seed [N]    start with N fake tracked addons (default 40) instead of an empty state
#   --runs N      launches to measure (default 3)
#   --wait SEC    seconds to idle before reading memory (default 8)
#   --software    QT_QUICK_BACKEND=software
# env: WAM_RAMTEST_DIR  work dir (default ~/.cache/wam-ramtest)
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WAM_RAMTEST_DIR:-$HOME/.cache/wam-ramtest}
P=$WORK/prefix
RUNS=3; WAIT=8; SEED=0; SETUP_ONLY=0; SOFTWARE=0
while [ $# -gt 0 ]; do
    case $1 in
        --setup-only) SETUP_ONLY=1 ;;
        --seed) SEED=40; if [[ ${2:-} =~ ^[0-9]+$ ]]; then SEED=$2; shift; fi ;;
        --runs) RUNS=$2; shift ;;
        --wait) WAIT=$2; shift ;;
        --software) SOFTWARE=1 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
log() { echo "[ram-test] $*"; }
mkdir -p "$WORK"

setup() {
    command -v apt-get >/dev/null || { echo "needs apt-get (Ubuntu 24.04 sandbox)" >&2; exit 1; }
    local SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO=sudo
    log "installing packages"
    $SUDO apt-get update -q >/dev/null 2>&1 || true   # a third-party repo may fail to sign; ignore
    # One command: a single unknown package aborts the whole install.
    $SUDO apt-get install -y -q cmake g++ pkg-config libcurl4-openssl-dev nlohmann-json3-dev \
        libzip-dev libssl-dev qt6-base-dev qt6-base-private-dev qt6-declarative-dev \
        qt6-declarative-private-dev qt6-declarative-dev-tools qt6-svg-dev qt6-shadertools-dev \
        qt6-tools-dev libxkbcommon-dev libgl-dev libvulkan-dev \
        qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
        qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtquick-dialogs \
        qml6-module-qtqml-workerscript qml6-module-qt-labs-platform qml6-module-qtquick-shapes \
        >"$WORK/apt.log" 2>&1 || { tail -5 "$WORK/apt.log"; exit 1; }
    command -v cmake >/dev/null || { echo "cmake missing after apt install" >&2; exit 1; }

    local qtv; qtv=$(qmake6 --version | sed -n 's/.*Qt version \([0-9.]*\).*/\1/p')
    log "Qt $qtv (the patches below exist for Qt 6.4; on 6.5+ they are harmless but unneeded)"

    rm -rf "$WORK/ecm" "$WORK/kiri" "$P"
    git -c advice.detachedHead=false clone -q --depth 1 -b v6.0.0 https://github.com/KDE/extra-cmake-modules.git "$WORK/ecm"
    git -c advice.detachedHead=false clone -q --depth 1 -b v6.0.0 https://github.com/KDE/kirigami.git "$WORK/kiri"

    log "installing ECM"
    cmake -S "$WORK/ecm" -B "$WORK/ecm/b" -DCMAKE_INSTALL_PREFIX="$P" -DBUILD_TESTING=OFF \
        -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF >/dev/null
    cmake --install "$WORK/ecm/b" >/dev/null

    # Qt 6.4 requires a VERSION on qt6_add_qml_module; ECM 6.0 relies on the 6.5 default.
    sed -i 's|    list(APPEND _arguments ${ARG_UNPARSED_ARGUMENTS})|    if (NOT "VERSION" IN_LIST _arguments)\n        list(APPEND _arguments VERSION 1.0)\n    endif()\n    list(APPEND _arguments ${ARG_UNPARSED_ARGUMENTS})|' \
        "$P/share/ECM/modules/ECMQmlModule6.cmake"
    grep -q 'VERSION 1.0' "$P/share/ECM/modules/ECMQmlModule6.cmake" || { echo "ECM patch failed" >&2; exit 1; }

    # Kirigami 6.0 vs Qt 6.4: relax the version check, stub qt6_policy (6.5+), use the
    # 6.4 singletonInstance(typeId) overload (module version is 2.0, 1.0 segfaults),
    # and replace a 6.5-only include.
    printf 'if(NOT COMMAND qt6_policy)\n  macro(qt6_policy)\n  endmacro()\nendif()\n' >"$WORK/stub.cmake"
    sed -i 's/REQUIRED_QT_VERSION 6.5.0/REQUIRED_QT_VERSION 6.4.0/' "$WORK/kiri/CMakeLists.txt"
    sed -i -E 's/(m_engine|engine)->singletonInstance<Kirigami::Platform::Units \*>\("org.kde.kirigami.platform", "Units"\)/\1->singletonInstance<Kirigami::Platform::Units *>(qmlTypeId("org.kde.kirigami.platform", 2, 0, "Units"))/' \
        "$WORK/kiri/src/wheelhandler.cpp" "$WORK/kiri/src/columnview.cpp" "$WORK/kiri/src/icon.cpp"
    sed -i 's|#include <qtypes.h>|#include <QtGlobal>|' "$WORK/kiri/src/padding.cpp"

    log "building Kirigami (slow on one core; log: $WORK/kirigami-build.log)"
    cmake -S "$WORK/kiri" -B "$WORK/kiri/b" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$P" \
        -DCMAKE_PROJECT_INCLUDE="$WORK/stub.cmake" -DCMAKE_INSTALL_PREFIX="$P" \
        -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF >"$WORK/kirigami-build.log" 2>&1
    cmake --build "$WORK/kiri/b" -j"$(nproc)" >>"$WORK/kirigami-build.log" 2>&1 \
        || { tail -20 "$WORK/kirigami-build.log"; exit 1; }
    cmake --install "$WORK/kiri/b" >>"$WORK/kirigami-build.log" 2>&1
    touch "$P/.ok"
}

build_app() {
    # Scratch project that compiles the repo's sources in place. Only main.cpp differs:
    # Qt 6.4 lacks loadFromModule, so the compiled-in QML path is dropped and the app's
    # own WAM_QML_DIR mode loads the unmodified QML from the repo.
    local app=$WORK/app; mkdir -p "$app"
    sed 's|engine.loadFromModule("Wam", "Main");|return 1;|' "$REPO/src/gui/main.cpp" >"$app/main.cpp"
    cat >"$app/CMakeLists.txt" <<CM
cmake_minimum_required(VERSION 3.16)
project(ramtest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2)
file(GLOB CORE $REPO/src/core/*.cpp)
file(GLOB GUI $REPO/src/gui/*.cpp)
list(FILTER GUI EXCLUDE REGEX "/main\\\\.cpp\$")
add_executable(wam-gui main.cpp \${CORE} \${GUI})
target_include_directories(wam-gui PRIVATE $REPO/src)
target_link_libraries(wam-gui PRIVATE CURL::libcurl nlohmann_json::nlohmann_json PkgConfig::LIBZIP crypto
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2)
CM
    log "building wam-gui from $REPO"
    cmake -S "$app" -B "$app/b" -DCMAKE_BUILD_TYPE=Release >"$WORK/app-build.log" 2>&1
    cmake --build "$app/b" -j"$(nproc)" >>"$WORK/app-build.log" 2>&1 \
        || { tail -20 "$WORK/app-build.log"; exit 1; }
}

seed_state() {
    local n=$1 cfg=$2 data=$3 wow=$WORK/seed-wow
    rm -rf "$wow"; mkdir -p "$wow/Interface/AddOns" "$data/wow-addon-manager" "$cfg/wow-addon-manager"
    {
        echo '{"addons":['
        for ((i = 1; i <= n; i++)); do
            mkdir -p "$wow/Interface/AddOns/FakeAddon$i"
            printf '## Title: Fake Addon %d\n## Version: 1.%d\n## X-Curse-Project-ID: %d\n' "$i" "$i" "$((90000 + i))" \
                >"$wow/Interface/AddOns/FakeAddon$i/FakeAddon$i.toc"
            printf '{"modId":%d,"fileId":%d,"displayName":"Fake Addon %d","fileName":"fake%d.zip","channel":1,"gameVersions":["12.0.0"],"folders":["FakeAddon%d"],"installedAt":"2026-01-01T00:00:00Z","manuallyProvided":false,"flavorTypeId":517,"flavorName":"Retail","fileDisplayName":"v1.%d","fileDate":"2026-01-01T00:00:00Z","modSlug":"fake-%d","author":"Someone"}%s\n' \
                "$((90000 + i))" "$((i + 1000))" "$i" "$i" "$i" "$i" "$i" "$([ "$i" -lt "$n" ] && echo ,)"
        done
        echo ']}'
    } >"$data/wow-addon-manager/installed.json"
    printf '{"wow_path":"%s","curseforge_api_key":null,"wow_flavor_id":null,"wow_flavor_name":null}\n' "$wow" \
        >"$cfg/wow-addon-manager/config.json"
}

measure() {
    local cfg=$WORK/xdg-config data=$WORK/xdg-data rt=$WORK/xdg-runtime
    rm -rf "$cfg" "$data" "$rt"; mkdir -p "$cfg" "$data"; mkdir -m 700 "$rt"
    [ "$SEED" -gt 0 ] && seed_state "$SEED" "$cfg" "$data"
    export QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME=$cfg XDG_DATA_HOME=$data XDG_RUNTIME_DIR=$rt \
        LD_LIBRARY_PATH=$P/lib/x86_64-linux-gnu QT_PLUGIN_PATH=$P/lib/x86_64-linux-gnu/plugins \
        QML_IMPORT_PATH=$P/lib/x86_64-linux-gnu/qml WAM_QML_DIR=$REPO/src/gui/qml
    [ "$SOFTWARE" -eq 1 ] && export QT_QUICK_BACKEND=software
    local state="empty"; [ "$SEED" -gt 0 ] && state="$SEED fake addons"
    log "state: $state, idle ${WAIT}s, $RUNS runs (stderr: $WORK/run.log)"
    printf '%-4s %8s %8s %9s %9s\n' run RSS_MB PSS_MB ANON_MB FILE_MB
    local sumpss=0 sumanon=0 i pid line rss pss anon file
    for ((i = 1; i <= RUNS; i++)); do
        "$WORK/app/b/wam-gui" >"$WORK/run.log" 2>&1 & pid=$!
        sleep "$WAIT"
        kill -0 "$pid" 2>/dev/null || { echo "wam-gui exited early:" >&2; tail -10 "$WORK/run.log" >&2; exit 1; }
        line=$(awk '/^Rss:/{r=$2} /^Pss:/{p=$2} /^Pss_Anon:/{a=$2} /^Pss_File:/{f=$2} END{printf "%d %d %d %d", r/1024, p/1024, a/1024, f/1024}' "/proc/$pid/smaps_rollup")
        read -r rss pss anon file <<<"$line"
        printf '%-4s %8s %8s %9s %9s\n' "$i" "$rss" "$pss" "$anon" "$file"
        sumpss=$((sumpss + pss)); sumanon=$((sumanon + anon))
        kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true
    done
    printf 'avg  %8s %8s %9s\n' "" "$((sumpss / RUNS))" "$((sumanon / RUNS))"
}

[ -f "$P/.ok" ] || setup
[ "$SETUP_ONLY" -eq 1 ] && { log "setup done"; exit 0; }
build_app
measure
