#include "media_database.h"

#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"

#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#endif

namespace {
std::string EscapeField(const std::wstring& value) {
    std::string text = WideToUtf8(value);
    std::string escaped;
    for (char ch : text) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '\t': escaped += "\\t"; break;
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::wstring UnescapeField(const std::string& value) {
    std::string text;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            if (next == 't') text.push_back('\t');
            else if (next == 'r') text.push_back('\r');
            else if (next == 'n') text.push_back('\n');
            else text.push_back(next);
        } else {
            text.push_back(value[i]);
        }
    }
    return Utf8ToWide(text);
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

std::string ReadWholeFile(const std::wstring& path) {
#ifdef _WIN32
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return {};
#else
    FILE* fp = std::fopen(WideToUtf8(path).c_str(), "rb");
    if (!fp) return {};
#endif
    std::string text;
    char buffer[4096];
    while (true) {
        size_t readCount = std::fread(buffer, 1, sizeof(buffer), fp);
        if (readCount > 0) text.append(buffer, readCount);
        if (readCount < sizeof(buffer)) break;
    }
    std::fclose(fp);
    return text;
}

bool WriteWholeFile(const std::wstring& path, const std::string& text) {
#ifdef _WIN32
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp) return false;
#else
    FILE* fp = std::fopen(WideToUtf8(path).c_str(), "wb");
    if (!fp) return false;
#endif
    const bool ok = std::fwrite(text.data(), 1, text.size(), fp) == text.size();
    std::fclose(fp);
    return ok;
}

bool ReplaceFileAtomic(const std::wstring& tempPath, const std::wstring& finalPath) {
#ifdef _WIN32
    return MoveFileExW(tempPath.c_str(),
                       finalPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(WideToUtf8(tempPath).c_str(), WideToUtf8(finalPath).c_str()) == 0;
#endif
}

std::wstring ContainerRecordKey(const std::wstring& canonicalKey) {
    return L"container\n" + canonicalKey;
}
}

std::wstring MediaDatabase::DefaultDatabasePath() {
    std::wstring path = AppConfig.GetDefaultPlaylistPath();
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    return dir + L"media-cache.tsv";
}

void MediaDatabase::Load(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
    m_nextId = kPersistentMediaIdBase;

    std::string text = ReadWholeFile(path);
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 2) continue;
        int id = 0;
        if (!TryParseIntStrict(fields[0], id) || id < kPersistentMediaIdBase) continue;

        Record record;
        record.id = id;
        record.key = UnescapeField(fields[1]);
        if (record.key.empty()) continue;
        if (fields.size() > 2) record.scanError = UnescapeField(fields[2]);

        m_records[record.key] = record;
        if (id >= m_nextId) m_nextId = id + 1;
    }
}

bool MediaDatabase::Save(const std::wstring& path) const {
    std::unique_lock<std::mutex> lock(m_mutex);
    std::ostringstream out;
    out << "# dlna-server media-cache.tsv v1\n";
    for (const auto& entry : m_records) {
        const Record& record = entry.second;
        out << record.id << '\t'
            << EscapeField(record.key) << '\t'
            << EscapeField(record.scanError) << '\n';
    }
    const std::string content = out.str();
    // Up to kPlaylistScanPoolSize (20) concurrent scan workers call
    // GetOrCreateStableId()/RecordScanError()/MarkScanSuccess() on this
    // same instance, all serialized through m_mutex. Save() runs once at
    // the end of a scan pass; m_records is not read again after this
    // point, so release the lock before the write-then-rename disk I/O
    // below instead of holding it across both file operations. See
    // F-LOCK-01.
    lock.unlock();
    const std::wstring tempPath = path + L".tmp";
    if (!WriteWholeFile(tempPath, content)) return false;
    if (ReplaceFileAtomic(tempPath, path)) return true;
#ifdef _WIN32
    DeleteFileW(tempPath.c_str());
#else
    std::remove(WideToUtf8(tempPath).c_str());
#endif
    return false;
}

void MediaDatabase::BeginScanPass() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_touchedThisPass.clear();
}

size_t MediaDatabase::PruneUntouched() {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t erased = 0;
    for (auto it = m_records.begin(); it != m_records.end();) {
        if (m_touchedThisPass.find(it->first) == m_touchedThisPass.end()) {
            m_freedIds.push_back(it->second.id);
            it = m_records.erase(it);
            ++erased;
        } else {
            ++it;
        }
    }
    return erased;
}

int MediaDatabase::GetOrCreateStableIdLocked(const std::wstring& canonicalKey) {
    m_touchedThisPass.insert(canonicalKey);
    auto found = m_records.find(canonicalKey);
    if (found != m_records.end()) {
        return found->second.id;
    }

    int id;
    if (!m_freedIds.empty()) {
        id = m_freedIds.back();
        m_freedIds.pop_back();
    } else if (m_nextId == (std::numeric_limits<int>::max)()) {
        // Signed-overflow guard: m_nextId++ here would be undefined
        // behavior (SEI CERT INT30-C / INT32-C; cppreference
        // "Arithmetic operators"). INT_MAX itself is still a valid,
        // legitimate final allocation, so issue it and reset the
        // counter to the base ID for every subsequent request, rather
        // than wrapping into negative territory or reading garbage.
        // The free-list above should already be satisfying most
        // requests for any server whose catalog churns, since every
        // PruneUntouched() call returns IDs to circulation -- this
        // path is a last-resort guard, not the expected steady state.
        // See F-CMR-04.
        LogPrint(L"MediaDatabase ID space exhausted; reusing base ID. "
                  L"Restart the server to fully reset the ID space.");
        id = m_nextId;
        m_nextId = kPersistentMediaIdBase;
    } else {
        id = m_nextId++;
    }

    Record record;
    record.id = id;
    record.key = canonicalKey;
    m_records[canonicalKey] = record;
    return id;
}

int MediaDatabase::GetOrCreateStableId(const std::wstring& canonicalKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return GetOrCreateStableIdLocked(canonicalKey);
}

int MediaDatabase::GetOrCreateStableContainerId(const std::wstring& canonicalKey) {
    return GetOrCreateStableId(ContainerRecordKey(canonicalKey));
}

void MediaDatabase::MarkScanSuccess(const std::wstring& canonicalKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_records.find(canonicalKey);
    if (found != m_records.end()) {
        found->second.scanError.clear();
    }
}

void MediaDatabase::RecordScanError(const std::wstring& canonicalKey, const std::wstring& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const int id = GetOrCreateStableIdLocked(canonicalKey);
    Record& record = m_records[canonicalKey];
    record.id = id;
    record.scanError = message;
}
