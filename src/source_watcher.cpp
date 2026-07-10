#include "source_watcher.h"

#include "config.h"
#include "netutils.h"
#include "network_sources.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {
constexpr int kMaxWatchDepth = 64;
constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void HashByte(uint64_t& hash, unsigned char value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void HashText(uint64_t& hash, const std::string& text) {
    for (unsigned char ch : text) {
        HashByte(hash, ch);
    }
    HashByte(hash, 0);
}

void HashNumber(uint64_t& hash, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        HashByte(hash, static_cast<unsigned char>((value >> (i * 8)) & 0xff));
    }
}

fs::path ToNativePath(const std::wstring& path) {
#ifdef _WIN32
    return fs::path(path);
#else
    return fs::path(WideToUtf8(path));
#endif
}

uint64_t FileTimeCount(const fs::directory_entry& entry, std::error_code& ec) {
    auto writeTime = entry.last_write_time(ec);
    if (ec) return 0;
    return static_cast<uint64_t>(writeTime.time_since_epoch().count());
}

void HashEntry(uint64_t& hash, const fs::directory_entry& entry) {
    std::error_code ec;
    fs::path path = entry.path();
    HashText(hash, path.u8string());
    HashNumber(hash, FileTimeCount(entry, ec));
    if (ec) {
        HashText(hash, "mtime-error");
        HashText(hash, ec.message());
        ec.clear();
    }

    ec.clear();
    if (entry.is_regular_file(ec)) {
        HashText(hash, "file");
        ec.clear();
        const uintmax_t size = entry.file_size(ec);
        if (ec) {
            HashText(hash, "size-error");
            HashText(hash, ec.message());
        } else {
            HashNumber(hash, static_cast<uint64_t>(size));
        }
    } else if (entry.is_directory(ec)) {
        HashText(hash, "dir");
    } else {
        HashText(hash, "other");
    }
}

void HashLocalSource(uint64_t& hash, const std::wstring& source) {
    fs::path root = ToNativePath(source);
    std::error_code ec;
    HashText(hash, WideToUtf8(source));
    if (!fs::exists(root, ec)) {
        HashText(hash, "missing");
        return;
    }

    fs::directory_entry rootEntry(root, ec);
    if (!ec) {
        HashEntry(hash, rootEntry);
    }

    ec.clear();
    if (fs::is_regular_file(root, ec)) return;
    if (ec || !fs::is_directory(root, ec)) return;

    std::filesystem::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (it.depth() > kMaxWatchDepth) {
            HashText(hash, "depth-limit");
            it.disable_recursion_pending();
        }
        std::error_code entryEc;
        if (it->is_symlink(entryEc)) {
            HashText(hash, "symlink-skip");
            HashText(hash, it->path().u8string());
        } else {
            HashEntry(hash, *it);
        }
        it.increment(ec);
    }
}
}

std::string ComputeMediaSourceSignature(const ConfigSnapshot& cfg) {
    uint64_t hash = kFnvOffset;
    for (const auto& source : cfg.mediaSources) {
        if (!source.enabled || IsRemoteMediaUrl(source.path)) continue;
        HashLocalSource(hash, source.path);
    }
    if (cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty() && !IsRemoteMediaUrl(cfg.defaultPlaylistPath)) {
        HashLocalSource(hash, cfg.defaultPlaylistPath);
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

bool MediaSourcesHaveChanged(const ConfigSnapshot& cfg, std::string& signature) {
    std::string next = ComputeMediaSourceSignature(cfg);
    if (next == signature) return false;
    signature = next;
    return true;
}

bool ShouldAutoRescan(const ConfigSnapshot& cfg, bool sourcesChanged) {
    return cfg.backgroundScanEnabled && sourcesChanged;
}
