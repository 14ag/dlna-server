#ifndef SSDP_COMMON_H
#define SSDP_COMMON_H

#include <string>
#include <vector>
#include <deque>
#include "ssdp.h" // DelayedSearchResponse, SSDPTarget

// The 5 standard targets this device advertises: root device, device UUID,
// MediaServer:1, ContentDirectory:1, ConnectionManager:1.
std::vector<SSDPTarget> BuildAdvertisedTargets(const std::string& uuid);

// If a delayed search response is already queued for the same remote
// address carrying the same ST/USN set, replace it in place instead of
// sending a near-duplicate response burst for one M-SEARCH.
bool CoalesceDelayedResponse(std::deque<DelayedSearchResponse>& queue, DelayedSearchResponse&& response);

// pure ceiling calculation for ComputeDelayMilliseconds see that
// function for the full citation trail on why this is narrower than
// the full mx second window exposed separately so it can be tested
// with no random number generator involved
unsigned int ComputeMaxDelayMilliseconds(int mxSeconds);

// UDA-required randomized M-SEARCH response delay: uniform over
// [0, ComputeMaxDelayMilliseconds(mxSeconds)] milliseconds, 0 if that
// bound rounds to zero.
unsigned int ComputeDelayMilliseconds(int mxSeconds);

#endif // SSDP_COMMON_H