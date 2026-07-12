#include "fs_backend.h"
#include "netutils.h"
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {
bool IsHiddenPath(const fs::path& path) {
    const std::string name = path.filename().u8string();
    return !name.empty() && name[0] == '.';
}

bool HasAnyReadBit(fs::perms permissions) {
    using fs::perms;
    return (permissions & (perms::owner_read | perms::group_read | perms::others_read)) != perms::none;
}
}

bool FsExists(const std::wstring& path) {
    std::error_code ec;
    return fs::exists(fs::path(WideToUtf8(path)), ec);
}

bool FsIsDirectory(const std::wstring& path) {
    std::error_code ec;
    return fs::is_directory(fs::path(WideToUtf8(path)), ec);
}

bool FsIsRegularFile(const std::wstring& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(WideToUtf8(path)), ec);
}

bool FsFileSize(const std::wstring& path, long long& sizeOut) {
    std::error_code ec;
    fs::path p(WideToUtf8(path));
    if (!fs::is_regular_file(p, ec)) return false;
    const uintmax_t size = fs::file_size(p, ec);
    if (ec) return false;
    sizeOut = static_cast<long long>(size);
    return true;
}

bool FsListDirectory(const std::wstring& path, std::vector<FsDirEntry>& outEntries) {
    outEntries.clear();
    fs::path root(WideToUtf8(path));
    std::error_code ec;
    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::directory_entry entry = *it;
        std::error_code entryEc;
        if (entry.is_symlink(entryEc)) continue;
        if (IsHiddenPath(entry.path())) continue;
        const fs::file_status status = entry.status(entryEc);
        if (entryEc || !HasAnyReadBit(status.permissions())) continue;
        FsDirEntry outEntry;
        outEntry.name = Utf8ToWide(entry.path().filename().u8string());
        outEntry.fullPath = Utf8ToWide(entry.path().u8string());
        bool isDirEc = false;
        outEntry.isDirectory = entry.is_directory(entryEc);
        (void)isDirEc;
        outEntries.push_back(std::move(outEntry));
    }
    return true;
}