#ifndef SSDP_COMMON_H
#define SSDP_COMMON_H

#include <string>
#include <vector>
#include "ssdp.h" // DelayedSearchResponse

struct SSDPTarget {
    std::string st;
    std::string usn;
};

// The 5 standard targets this device advertises: root device, device UUID,
// MediaServer:1, ContentDirectory:1, ConnectionManager:1.
std::vector<SSDPTarget> BuildAdvertisedTargets(const std::string& uuid);

// If a delayed search response is already queued for the same remote
// address carrying the same ST/USN set, replace it in place instead of
// sending a near-duplicate response burst for one M-SEARCH.
bool CoalesceDelayedResponse(std::vector<DelayedSearchResponse>& queue, DelayedSearchResponse&& response);

// UDA-required randomized M-SEARCH response delay: uniform over
// [0, min(mxSeconds, 5)] seconds, 0 if that bound rounds to zero.
unsigned int ComputeDelayMilliseconds(int mxSeconds);

#endif // SSDP_COMMON_H