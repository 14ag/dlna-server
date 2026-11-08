#ifndef CLI_PRINT_HOOKS_WIN_H
#define CLI_PRINT_HOOKS_WIN_H

// Runs the matching print hook when argv holds one anywhere and applies
// any source or debug flag found before it so hook output matches the
// old inline argv loop byte for byte
// Returns true when a hook ran with exitCode set to the process exit code
// Returns false when no hook matched and normal startup must continue
bool TryRunPrintHook(int argc, wchar_t** argv, int& exitCode);

#endif // CLI_PRINT_HOOKS_WIN_H
