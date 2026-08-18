#ifndef GEOMETRY_DUMP_POLICY_H
#define GEOMETRY_DUMP_POLICY_H

// pure decision extracted so it can be exercised by a print flag test
// on both platforms mirrors the extraction pattern already used by
// startup_mode h server_close_policy h and source_drop_policy h in
// this codebase
// geometry dumps previously piggybacked on the general purpose debug
// log flag so every dialog open during an ordinary debug session
// added roughly twenty five extra log lines the posix gtk4 build
// already avoids this by gating its equivalent dump behind a
// dedicated hidden cli flag see g_dumpGeometry in gtk4_gui_main cpp
// this predicate gives the win32 build the same dedicated gate
inline bool ShouldDumpDialogGeometry(bool dedicatedGeometryDumpFlagEnabled) {
    return dedicatedGeometryDumpFlagEnabled;
}

#endif // GEOMETRY_DUMP_POLICY_H