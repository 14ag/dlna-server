#ifndef CONTENTDIRECTORY_H
#define CONTENTDIRECTORY_H

#include <string>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "media_sources.h"

class ContentDirectory {
public:
    static ContentDirectory& Get();

    std::string GetDeviceDescriptionXML(const std::string& hostUrl);
    std::string GetContentDirectoryXML();
    std::string GetConnectionManagerXML();

    std::string HandleContentDirectoryControl(const std::string& soapBody, const std::string& hostUrl);
    std::string HandleConnectionManagerControl(const std::string& soapBody);

    // Test-only: how many times the Search action actually re-walked the
    // catalog instead of returning a cached result.
    static long GetSearchRecomputeCountForTest();

private:
    ContentDirectory() {}

    struct SearchCacheEntry {
        int systemUpdateId;
        std::wstring key;
        std::vector<MediaItem> results;
    };
    mutable std::mutex m_searchCacheMutex;
    mutable std::unordered_map<int, SearchCacheEntry> m_searchCacheByContainer;
};

#define AppContent ContentDirectory::Get()

#endif // CONTENTDIRECTORY_H
