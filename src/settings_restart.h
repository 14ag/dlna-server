#ifndef SETTINGS_RESTART_H
#define SETTINGS_RESTART_H

#include <string>
#include <vector>

struct ConfigSnapshot;

// Names of settings that differ between `before` and `after` AND are only
// read once, at Server::Start() time, so a running server will not pick
// the new value up without a restart.
std::vector<std::wstring> DetermineSettingsRequiringRestart(const ConfigSnapshot& before,
                                                             const ConfigSnapshot& after);

#endif // SETTINGS_RESTART_H