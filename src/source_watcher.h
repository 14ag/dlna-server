#ifndef SOURCE_WATCHER_H
#define SOURCE_WATCHER_H

#include <string>

struct ConfigSnapshot;

std::string ComputeMediaSourceSignature(const ConfigSnapshot& cfg);
bool MediaSourcesHaveChanged(const ConfigSnapshot& cfg, std::string& signature);
bool ShouldAutoRescan(const ConfigSnapshot& cfg, bool sourcesChanged);

#endif // SOURCE_WATCHER_H
