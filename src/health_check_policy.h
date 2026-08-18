#ifndef HEALTH_CHECK_POLICY_H
#define HEALTH_CHECK_POLICY_H

// pure decision extracted so it can be exercised by a print flag test
// on both platforms mirrors the extraction pattern already used by
// startup_mode h server_close_policy h and source_drop_policy h in
// this codebase
// a single unhealthy poll can be a legitimate in flight ssdp restart
// window not a real crash requiring kMinConsecutiveUnhealthyPolls
// consecutive unhealthy polls in a row before the caller treats the
// server as actually dead see the workflow document for the timing
// citation this bounds against
inline constexpr int kMinConsecutiveUnhealthyPolls = 4;

// consecutiveUnhealthyPolls is the caller's own running count of how
// many polls in a row IsHealthy returned false ending with and
// including this call isRunning mirrors Server IsRunning isHealthy
// mirrors the just sampled Server IsHealthy result
// returns true only once the unhealthy streak has reached the
// required threshold false while still inside the grace period or
// while the server is not running at all
inline bool ShouldTreatServerAsUnhealthy(bool isRunning, bool isHealthy, int consecutiveUnhealthyPolls) {
    if (!isRunning || isHealthy) return false;
    return consecutiveUnhealthyPolls >= kMinConsecutiveUnhealthyPolls;
}

#endif // HEALTH_CHECK_POLICY_H