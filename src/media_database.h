#ifndef MEDIA_DATABASE_H
#define MEDIA_DATABASE_H

#include <string>
#include <unordered_map>

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

private:
    struct Record {
        int id = 0;
        std::wstring key;
        std::wstring scanError;
    };

    std::unordered_map<std::wstring, Record> m_records;
    int m_nextId = kPersistentMediaIdBase;
};

#endif // MEDIA_DATABASE_H
