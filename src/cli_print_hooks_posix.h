#ifndef CLI_PRINT_HOOKS_POSIX_H
#define CLI_PRINT_HOOKS_POSIX_H

#include <iosfwd>

// Prints usage to out; stdout for --help, stderr for errors
void PrintUsage(std::ostream& out);

// Returns true if argv holds a --print-* hook and it was handled with
// exitCode set to the process exit code else returns false so normal
// startup can continue
bool TryRunPrintHook(int argc, char** argv, int& exitCode);

#endif // CLI_PRINT_HOOKS_POSIX_H
