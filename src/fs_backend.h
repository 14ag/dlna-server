#ifndef FS_BACKEND_H
#define FS_BACKEND_H

#include <string>
#include <vector>

struct FsDirEntry {
    std::wstring name;      // filename only, no path
    std::wstring fullPath;
    bool isDirectory;
};

// One free function per filesystem primitive the scan path needs. Two
// translation units implement this against Win32 (FindFirstFileW /
// GetFileAttributesExW) and against std::filesystem respectively; the
// orchestration logic in media_sources_common.cpp calls only these, never a
// platform API directly, so both platforms execute the identical scan
// control flow and can no longer silently diverge (see the Scan()
// pre-check mismatch this phase closes).
bool FsExists(const std::wstring& path);
bool FsIsDirectory(const std::wstring& path);
bool FsIsRegularFile(const std::wstring& path);
// Returns false (and leaves sizeOut unchanged) if the size cannot be read.
bool FsFileSize(const std::wstring& path, long long& sizeOut);
// Lists immediate children only (non-recursive). Skips hidden/system/
// reparse-point entries on Win32 and dotfiles/unreadable entries on POSIX,
// matching each platform's existing skip rules. Returns false if the
// directory could not be opened at all (permission denied, does not
// exist) -- callers use this to distinguish "empty folder" from
// "unreadable folder" exactly as ScanFolder's FindFirstFileW-failure path
// and the POSIX directory_iterator error-code path already do today.
bool FsListDirectory(const std::wstring& path, std::vector<FsDirEntry>& outEntries);

#endif // FS_BACKEND_H