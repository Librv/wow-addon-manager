# src/gui rules

The Qt adapter layer (`wam_gui`) and the Kirigami app (`wam-gui`). The
repo-wide rules in `/AGENTS.md` apply too. Read `docs/gui.md` before changing
QML.

- **Threading:** `WamWorker` lives on one dedicated thread and owns the core's
  config, profiles, the selected profile's state and the API client.
  `WamController` is what QML sees; its slots forward to the worker with
  queued `invokeMethod` calls and return at once.
  Never call a `WamWorker` member directly from the GUI thread.
- **Requests run one at a time, in order.** A new request waits behind
  everything already queued; design UI feedback with that in mind.
- **Models** (`*_model.cpp`) are filled from worker results on the GUI thread.
  A new role added to the installed model is usable in `AddonDetails.qml` as
  `addon.<role>` with no further plumbing.
- **Every key QML reads must exist.** A missing key in a `QVariantMap` becomes
  `undefined` and breaks string bindings at runtime (this happened with the
  update queue's `head`). Fill defaults on the C++ side and test for them.
- **Qt 6.5 is the minimum** (`loadFromModule`). Do not use newer API without
  raising the floor in `CMakeLists.txt` and the README.
- **QML:** one component per file, knobs in a `Tunables` block, colours from
  `AppColors`. New files go in `WAM_QML_FILES`; new singletons also in
  `qml/qmldir`.
- **Tests** for the adapter go in `tests/gui_tests.cpp` and run offline, with
  `QT_QPA_PLATFORM=offscreen` if no display is available. QML itself is not
  covered by tests: if you changed QML and could not run the app, say so.
