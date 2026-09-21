#ifndef CLI_PRINT_HOOKS_POSIX_H
#define CLI_PRINT_HOOKS_POSIX_H

// Prints short usage to stderr shared by main and the print hook runner
void PrintUsage(const char* exe);

// Returns true if argv holds a --print-* hook and it was handled with
// exitCode set to the process exit code else returns false so normal
// startup can continue
bool TryRunPrintHook(int argc, char** argv, int& exitCode);

#endif // CLI_PRINT_HOOKS_POSIX_H
