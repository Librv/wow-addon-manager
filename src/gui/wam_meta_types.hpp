#pragma once
#include <QMetaType>
#include <QList>
#include "core/curseforge_client.hpp"
#include "core/state_store.hpp"
#include "gui/scan_results_model.hpp"

Q_DECLARE_METATYPE(wam::CurseForgeMod)
Q_DECLARE_METATYPE(wam::CurseForgeFile)
Q_DECLARE_METATYPE(wam::GameVersionType)
Q_DECLARE_METATYPE(wam::InstalledAddon)

namespace wam::gui {

// Registers the wam_core value types used as cross-thread signal arguments.
// On Qt 6 this is largely redundant (argument types are registered
// automatically for pointer-to-member/lambda connects), but it is harmless
// and keeps queued delivery working if a string-based connection is ever
// added. Call once before the worker thread starts.
inline void registerMetaTypes() {
    qRegisterMetaType<wam::CurseForgeMod>();
    qRegisterMetaType<wam::CurseForgeFile>();
    qRegisterMetaType<wam::GameVersionType>();
    qRegisterMetaType<wam::InstalledAddon>();
    qRegisterMetaType<wam::gui::ScanGroup>();
    qRegisterMetaType<QList<wam::gui::ScanGroup>>();
    qRegisterMetaType<QList<wam::CurseForgeMod>>();
    qRegisterMetaType<QList<wam::CurseForgeFile>>();
    qRegisterMetaType<QList<wam::GameVersionType>>();
    qRegisterMetaType<QList<wam::InstalledAddon>>();
}

} // namespace wam::gui
