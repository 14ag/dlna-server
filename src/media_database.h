#ifndef MEDIA_DATABASE_H
#define MEDIA_DATABASE_H

#include <string>
#include <unordered_map>

constexpr int kPersistentMediaIdBase = 1000000;

struct CachedMediaMetadata {
    std::wstring title;
    std::wstring mimeType;
    std::wstring upnpClass;
    long long durationMs = 0;
    long long bitrate = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::wstring codec;
};

class MediaDatabase {
public:
    static std::wstring DefaultDatabasePath();

    void Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

    int GetOrCreateStableId(const std::wstring& canonicalKey);
    int GetOrCreateStableContainerId(const std::wstring& canonicalKey);
    void MarkScanSuccess(const std::wstring& canonicalKey);
    void RecordScanError(const std::wstring& canonicalKey, const std::wstring& message);
    void CacheMetadata(const std::wstring& canonicalKey, const CachedMediaMetadata& metadata);

private:
    struct Record {
        int id = 0;
        std::wstring key;
        std::wstring scanError;
        CachedMediaMetadata metadata;
    };

    std::unordered_map<std::wstring, Record> m_records;
    int m_nextId = kPersistentMediaIdBase;
};

#endif // MEDIA_DATABASE_H
