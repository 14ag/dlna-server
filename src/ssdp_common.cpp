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
    constexpr unsigned int kMaxDelayFractionNumerator = 4;
    constexpr unsigned int kMaxDelayFractionDenominator = 5;
    constexpr unsigned int kHardCapMs = 1000;
    const unsigned int scaled = (fullWindowMs * kMaxDelayFractionNumerator) / kMaxDelayFractionDenominator;
    return scaled < kHardCapMs ? scaled : kHardCapMs;
}

unsigned int ComputeDelayMilliseconds(int mxSeconds) {
    unsigned int maxDelay = ComputeMaxDelayMilliseconds(mxSeconds);
    if (maxDelay == 0) return 0;
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned int> distribution(0, maxDelay);
    return distribution(generator);
}

std::string BuildSearchResponseMessage(const SsdpSearchResponseFields& fields) {
    return "HTTP/1.1 200 OK\r\n"
           "CACHE-CONTROL: max-age=1800\r\n"
           "DATE: " + fields.date + "\r\n"
           "EXT:\r\n"
           "LOCATION: " + fields.locationUrl + "\r\n"
           "SERVER: " + fields.serverHeader + "\r\n"
           "ST: " + fields.st + "\r\n"
           "USN: " + fields.usn + "\r\n"
           "BOOTID.UPNP.ORG: " + std::to_string(fields.bootId) + "\r\n"
           "CONFIGID.UPNP.ORG: " + std::to_string(fields.configId) + "\r\n"
           "Content-Length: 0\r\n"
           "\r\n";
}