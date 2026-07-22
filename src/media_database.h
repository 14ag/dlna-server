#ifndef MEDIA_DATABASE_H
#define MEDIA_DATABASE_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

constexpr int kPersistentMediaIdBase = 1000000;

class MediaDatabase {
public:
    static std::wstring DefaultDatabasePath();

    void Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

    int GetOrCreateStableId(const std::wstring& canonicalKey);
    int GetOrCreateStableContainerId(const std::wstring& canonicalKey);
    void MarkScanSuccess(const std::wstring& canonicalKey);
    void RecordScanError(const std::wstring& canonicalKey, const std::wstring& message);

    // Call once at the start of a full catalog scan pass (before any
    // source is scanned). Clears the touched-key tracking set so
    // PruneUntouched() at the end of the pass can tell which records were
    // not revisited this pass.
    void BeginScanPass();
    // Call once after every configured source has finished scanning (i.e.
    // after the full catalog pass that started with BeginScanPass()).
    // Erases every record whose key was not touched (via
    // GetOrCreateStableId or RecordScanError) during that pass. Returns
    // the number of records erased, for logging.
    size_t PruneUntouched();

private:
    struct Record {
        int id = 0;
        std::wstring key;
        std::wstring scanError;
    };

    // Shared by GetOrCreateStableId and RecordScanError so both mark a key
    // touched and both run under exactly one m_mutex acquisition. See
    // Task 14 of dlna-server-concurrency-memory-fix-workflow-17-7-26.md.
    // Caller must already hold m_mutex.
    int GetOrCreateStableIdLocked(const std::wstring& canonicalKey);

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, Record> m_records;
    int m_nextId = kPersistentMediaIdBase;
    std::unordered_set<std::wstring> m_touchedThisPass;
};

#endif // MEDIA_DATABASE_H
