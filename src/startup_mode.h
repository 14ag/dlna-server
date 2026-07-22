#ifndef STARTUP_MODE_H
#define STARTUP_MODE_H

// pure decision extracted so it can be exercised by a print flag
// headless is forced whenever any source input was supplied on the
// command line whether from source or from bare dropped paths
// otherwise headless follows exactly whatever the person typed
inline bool ShouldStartHeadless(bool explicitHeadlessFlag, bool hasRuntimeSources) {
    return explicitHeadlessFlag || hasRuntimeSources;
}

#endif // STARTUP_MODE_H
