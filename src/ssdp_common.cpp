#include "ssdp_common.h"
#include <algorithm>
#include <cstring>
#include <random>

std::vector<SSDPTarget> BuildAdvertisedTargets(const std::string& uuid) {
    return {
        {"upnp:rootdevice", "uuid:" + uuid + "::upnp:rootdevice"},
        {"uuid:" + uuid, "uuid:" + uuid},
        {"urn:schemas-upnp-org:device:MediaServer:1", "uuid:" + uuid + "::urn:schemas-upnp-org:device:MediaServer:1"},
        {"urn:schemas-upnp-org:service:ContentDirectory:1", "uuid:" + uuid + "::urn:schemas-upnp-org:service:ContentDirectory:1"},
        {"urn:schemas-upnp-org:service:ConnectionManager:1", "uuid:" + uuid + "::urn:schemas-upnp-org:service:ConnectionManager:1"},
    };
}

bool CoalesceDelayedResponse(std::deque<DelayedSearchResponse>& queue, DelayedSearchResponse&& response) {
    for (auto& queued : queue) {
        if (queued.remoteLen == response.remoteLen &&
            std::memcmp(&queued.remoteAddr, &response.remoteAddr, static_cast<size_t>(response.remoteLen)) == 0 &&
            queued.logUsn == response.logUsn &&
            queued.logSt == response.logSt) {
            queued = std::move(response);
            return true;
        }
    }
    return false;
}

unsigned int ComputeMaxDelayMilliseconds(int mxSeconds) {
    int boundedSeconds = (std::max)(0, (std::min)(mxSeconds, 5));
    if (boundedSeconds <= 1) return 0;
    unsigned int fullWindowMs = static_cast<unsigned int>(boundedSeconds * 1000);
    // pupnp derived control points such as vlc use the mx value both
    // as the m search header field and as their own read timeout for
    // the search response socket see SearchByTarget in pupnp source
    // file ssdp ctrlpt c where timeTillRead is set equal to mx
    // a reply scheduled near the full mx window can therefore arrive
    // after the client has already stopped listening for one
    // scaling the legal zero to mx window down to four fifths keeps
    // every possible delay value fully inside the spec legal range
    // while leaving a fixed margin before that hard client side cutoff
    constexpr unsigned int kMaxDelayFractionNumerator = 4;
    constexpr unsigned int kMaxDelayFractionDenominator = 5;
    return (fullWindowMs * kMaxDelayFractionNumerator) / kMaxDelayFractionDenominator;
}

unsigned int ComputeDelayMilliseconds(int mxSeconds) {
    unsigned int maxDelay = ComputeMaxDelayMilliseconds(mxSeconds);
    if (maxDelay == 0) return 0;
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned int> distribution(0, maxDelay);
    return distribution(generator);
}