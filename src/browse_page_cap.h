#ifndef BROWSE_PAGE_CAP_H
#define BROWSE_PAGE_CAP_H

#include <algorithm>

// Server-side hard ceiling on how many items a single Browse/Search
// response may return even when the client requests RequestedCount=0
// ("return everything remaining", legal per the UPnP ContentDirectory:1
// Browse/Search action semantics). Bounds BuildDIDL's didl.reserve()
// to roughly 8 MiB (256 + kMaxBrowseResponseItems * 512 bytes)
// regardless of how large the target container's catalog is. A
// well-behaved control point that wants the rest of a large container
// is expected to continue paging with subsequent StartingIndex-
// advanced requests, exactly as it already must for any
// RequestedCount>0 response that does not exhaust TotalMatches --
// NumberReturned being smaller than TotalMatches is already normal,
// spec-legal pagination behavior this change does not alter. See
// F-CMR-05.
inline constexpr int kMaxBrowseResponseItems = 16000;

// Extracted as a pure function -- no MediaItem/hostUrl/filter
// dependency -- so this specific clamping arithmetic has a dedicated,
// fast, deterministic CLI test independent of a live scan or a large
// synthetic media library.
inline int ClampBrowseRequestedCount(int requestedCount, int available) {
    const int uncapped = requestedCount == 0
        ? available
        : (std::min)(requestedCount, available);
    return (std::min)(uncapped, kMaxBrowseResponseItems);
}

#endif // BROWSE_PAGE_CAP_H
