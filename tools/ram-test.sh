#!/usr/bin/env bash
# Idle RAM test of the real wam-gui, for A/B comparison of code changes. See
# docs/ram-testing.md. Native mode (default) runs the repo's normal build on the
# system Qt and Kirigami. --sandbox is the old Ubuntu 24.04 cloud-sandbox mode.
#
# usage: tools/ram-test.sh [options]
#   --seed [N]     start with N fake tracked addons (default 40) instead of an empty state
#   --runs N       launches to measure (default 3)
#   --bin PATH     wam-gui to run (default: build/wam-gui, or $WAM_GUI_BIN)
#   --qml DIR      load QML from DIR through WAM_QML_DIR (default: the compiled-in QML)
#   --wayland      use the real Wayland session instead of offscreen
#   --detail N     after the first run, list the top N mappings by PSS per backing file
#   --software     QT_QUICK_BACKEND=software
#   --wait SEC     native: longest idle before reading memory (default 15, stops early
#                  once PSS settles); --sandbox: fixed idle time (default 8)
#   --sandbox      install deps, build Kirigami and a scratch project (Ubuntu 24.04 only)
#   --setup-only   with --sandbox: install deps and build Kirigami, then stop
# env: WAM_RAMTEST_DIR  work dir (default ~/.cache/wam-ramtest)
#      WAM_GUI_BIN      same as --bin
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WAM_RAMTEST_DIR:-$HOME/.cache/wam-ramtest}
P=$WORK/prefix
BIN=${WAM_GUI_BIN:-$REPO/build/wam-gui}
RUNS=3; WAIT=""; SEED=0; SETUP_ONLY=0; SOFTWARE=0; SANDBOX=0; WAYLAND=0; QML=""; DETAIL=0
need_value() { [ $# -ge 2 ] || { echo "$1 needs a value" >&2; exit 1; }; }
while [ $# -gt 0 ]; do
    case $1 in
        --setup-only) SETUP_ONLY=1 ;;
        --sandbox) SANDBOX=1 ;;
        --wayland) WAYLAND=1 ;;
        --seed) SEED=40; if [[ ${2:-} =~ ^[0-9]+$ ]]; then SEED=$2; shift; fi ;;
        --runs) need_value "$@"; RUNS=$2; shift ;;
        --wait) need_value "$@"; WAIT=$2; shift ;;
        --bin) need_value "$@"; BIN=$2; shift ;;
        --qml) need_value "$@"
            QML=$(cd "$2" 2>/dev/null && pwd) || { echo "--qml: no such directory: $2" >&2; exit 1; }
            shift ;;
        --detail) need_value "$@"; DETAIL=$2; shift ;;
        --software) SOFTWARE=1 ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if [ "$SETUP_ONLY" -eq 1 ] && [ "$SANDBOX" -eq 0 ]; then
    echo "--setup-only needs --sandbox" >&2; exit 2
fi
if [ "$SANDBOX" -eq 1 ] && [ "$WAYLAND" -eq 1 ]; then
    echo "--wayland is native-only" >&2; exit 2
fi
PID=""
cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    return 0
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
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

# Prints "rss pss anon file shmem" in MB for a pid.
rollup() {
    awk '/^Rss:/{r=$2} /^Pss:/{p=$2} /^Pss_Anon:/{a=$2} /^Pss_File:/{f=$2} /^Pss_Shmem:/{s=$2} END{printf "%.1f %.1f %.1f %.1f %.1f", r/1024, p/1024, a/1024, f/1024, s/1024}' "/proc/$1/smaps_rollup" 2>/dev/null || true
}

# Top mappings by PSS, grouped by backing file.
detail() {
    echo "top $DETAIL mappings by PSS (MB), run 1:"
    awk '/^[0-9a-f]+-[0-9a-f]+ /{name=($6==""?"[anon]":$6)} /^Pss:/{s[name]+=$2} END{for(k in s) printf "%8.1f %s\n", s[k]/1024, k}' "/proc/$1/smaps" 2>/dev/null | sort -rn | head -"$DETAIL" || true
}

# Waits until PSS moves less than 0.3 MB for 4 samples in a row (every 0.5 s), at most
# $1 seconds. Sets IDLE_SECS. Returns 1 if the process died.
wait_settled() {
    local pid=$1 max=$2 prev=0 stable=0 t=0 p
    while ((t < max * 2)); do
        sleep 0.5; t=$((t + 1))
        kill -0 "$pid" 2>/dev/null || return 1
        p=$(awk '/^Pss:/{print $2}' "/proc/$pid/smaps_rollup" 2>/dev/null || true)
        [ -n "$p" ] || return 1
        if ((t > 4 && p - prev < 300 && prev - p < 300)); then stable=$((stable + 1)); else stable=0; fi
        prev=$p
        ((stable >= 4)) && break
    done
    IDLE_SECS=$((t / 2))
}

measure() {
    local cfg=$WORK/xdg-config data=$WORK/xdg-data rt=$WORK/xdg-runtime home=$WORK/home
    rm -rf "$cfg" "$data" "$rt" "$home"; mkdir -p "$cfg" "$data" "$home"; mkdir -m 700 "$rt"
    [ "$SEED" -gt 0 ] && seed_state "$SEED" "$cfg" "$data"
    # Bring the desktop's colour scheme along: without it the app gets KDE's light
    # default text on its fixed dark surfaces, and the theme is not what users run.
    local kg=${XDG_CONFIG_HOME:-$HOME/.config}/kdeglobals
    [ -f "$kg" ] && cp "$kg" "$cfg/"
    # Isolated state, so the real ~/.config/wow-addon-manager is never read or written.
    local envs=(HOME="$home" XDG_CONFIG_HOME="$cfg" XDG_DATA_HOME="$data" XDG_RUNTIME_DIR="$rt")
    local bin max
    if [ "$SANDBOX" -eq 1 ]; then
        bin=$WORK/app/b/wam-gui; max=${WAIT:-8}
        envs+=(QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$P/lib/x86_64-linux-gnu"
            QT_PLUGIN_PATH="$P/lib/x86_64-linux-gnu/plugins"
            QML_IMPORT_PATH="$P/lib/x86_64-linux-gnu/qml" WAM_QML_DIR="$REPO/src/gui/qml")
    else
        bin=$BIN; max=${WAIT:-15}
        [ -x "$bin" ] || { echo "no wam-gui at $bin (build it, or pass --bin)" >&2; exit 1; }
        if [ "$WAYLAND" -eq 1 ]; then
            # XDG_RUNTIME_DIR is overridden, so the socket needs an absolute path.
            local wd=${WAYLAND_DISPLAY:-wayland-0}
            [[ $wd == /* ]] || wd=/run/user/$(id -u)/$wd
            envs+=(QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY="$wd")
        else
            envs+=(QT_QPA_PLATFORM=offscreen)
        fi
        [ -n "$QML" ] && envs+=(WAM_QML_DIR="$QML")
    fi
    [ "$SOFTWARE" -eq 1 ] && envs+=(QT_QUICK_BACKEND=software)
    local state="empty"; [ "$SEED" -gt 0 ] && state="$SEED fake addons"
    if [ "$SANDBOX" -eq 1 ]; then
        log "sandbox: $state, idle ${max}s, $RUNS runs (stderr: $WORK/run.log)"
    else
        log "native: $bin, $state, ${QML:+qml $QML, }settle up to ${max}s, $RUNS runs (stderr: $WORK/run.log)"
    fi
    printf '%-4s %7s %7s %7s %7s %7s %5s\n' run RSS_MB PSS_MB ANON_MB FILE_MB SHMEM_MB secs
    local sumrss=0 sumpss=0 sumanon=0 sumfile=0 i pid rss pss anon file shm
    for ((i = 1; i <= RUNS; i++)); do
        env "${envs[@]}" "$bin" >"$WORK/run.log" 2>&1 & pid=$!; PID=$pid
        if [ "$SANDBOX" -eq 1 ]; then
            sleep "$max"; IDLE_SECS=$max
            kill -0 "$pid" 2>/dev/null || { echo "wam-gui exited early:" >&2; tail -10 "$WORK/run.log" >&2; exit 1; }
        else
            wait_settled "$pid" "$max" || { echo "wam-gui exited early:" >&2; tail -10 "$WORK/run.log" >&2; exit 1; }
        fi
        read -r rss pss anon file shm <<<"$(rollup "$pid")"
        printf '%-4s %7s %7s %7s %7s %7s %5s\n' "$i" "$rss" "$pss" "$anon" "$file" "$shm" "$IDLE_SECS"
        if [ "$i" -eq 1 ] && [ "$DETAIL" -gt 0 ]; then detail "$pid" >"$WORK/detail.txt"; fi
        sumrss=$(awk "BEGIN{print $sumrss + $rss}"); sumpss=$(awk "BEGIN{print $sumpss + $pss}")
        sumanon=$(awk "BEGIN{print $sumanon + $anon}"); sumfile=$(awk "BEGIN{print $sumfile + $file}")
        kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true
    done
    awk -v n="$RUNS" -v r="$sumrss" -v p="$sumpss" -v a="$sumanon" -v f="$sumfile" \
        'BEGIN{printf "avg  %7.1f %7.1f %7.1f %7.1f\n", r/n, p/n, a/n, f/n}'
    if [ "$DETAIL" -gt 0 ]; then echo; cat "$WORK/detail.txt"; fi
}

if [ "$SANDBOX" -eq 1 ]; then
    [ -f "$P/.ok" ] || setup
    [ "$SETUP_ONLY" -eq 1 ] && { log "setup done"; exit 0; }
    build_app
fi
measure
