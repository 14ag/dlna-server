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

    // drops every cached Search result
    // call this right after MediaSources ResetForRescan so a stale
    // container id from before the rescan can never be served back
    // out of this cache again see Server Start and Server Rescan on
    // both platforms for the call sites
    void ClearSearchCache();

    // test only how many distinct containers are currently cached
    long GetSearchCacheSizeForTest();

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
    // hard cap so a long running server that gets Searched against
    // many distinct containers over its uptime cannot grow this
    // cache without limit mirrors kMaxUpnpSubscriptions in
    // upnp_eventing h
    static constexpr size_t kMaxSearchCacheContainers = 256;
    mutable std::mutex m_searchCacheMutex;
    mutable std::unordered_map<int, SearchCacheEntry> m_searchCacheByContainer;
};

#define AppContent ContentDirectory::Get()

#endif // CONTENTDIRECTORY_H
