#ifndef SOURCE_WATCHER_H
#define SOURCE_WATCHER_H

#include <string>

struct ConfigSnapshot;

std::string ComputeMediaSourceSignature(const ConfigSnapshot& cfg);
bool MediaSourcesHaveChanged(const ConfigSnapshot& cfg, std::string& signature);

#endif // SOURCE_WATCHER_H
