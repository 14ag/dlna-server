#ifndef SEARCH_CACHE_BUDGET_H
#define SEARCH_CACHE_BUDGET_H

#include <cstddef>

// Pure predicate: should ContentDirectory's search cache evict at least
// one more entry before accepting incomingItemCount items for a
// container, given the current container count, the current total
// cached item count across every entry, the container-count cap, and
// the total-item budget. previousItemCountForSameContainer is 0 for a
// fresh insert, or the size of the entry about to be overwritten for
// the same containerId. Extracted as a pure function with no
// mutex/container dependency so this specific budget arithmetic has a
// dedicated fast deterministic CLI test independent of a live server
// or a large synthetic media library, matching the existing extraction
// pattern already used by ClampBrowseRequestedCount in
// browse_page_cap.h and ShouldEvictBeforeCacheInsert in
// network_sources.h. See F-CACHE-01.
inline bool SearchCacheNeedsEviction(size_t currentContainerCount,
                                     size_t currentTotalItems,
                                     size_t previousItemCountForSameContainer,
                                     size_t incomingItemCount,
                                     bool containerAlreadyCached,
                                     size_t maxContainers,
                                     size_t maxTotalItems) {
    if (!containerAlreadyCached && currentContainerCount >= maxContainers) return true;
    const size_t projectedTotal = currentTotalItems - previousItemCountForSameContainer + incomingItemCount;
    return projectedTotal > maxTotalItems;
}

#endif // SEARCH_CACHE_BUDGET_H