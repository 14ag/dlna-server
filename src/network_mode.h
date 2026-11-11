#ifndef NETWORK_MODE_H
#define NETWORK_MODE_H

#include <cstddef>

// pure decisions for standby mode
// no windows types and no posix types so it compiles on both platforms
// mirrors the extraction pattern of close_pending_state h and health_check_policy h
enum class NetworkMode {
    Normal,
    Standby
};

enum class NetworkModeEvent {
    None,
    EnterStandby,
    LeaveStandby
};

// zero usable endpoints at start means the server waits for a network
inline NetworkMode DecideStartMode(size_t realEndpointCount) {
    return realEndpointCount == 0 ? NetworkMode::Standby : NetworkMode::Normal;
}

// mode is the current mode and realEndpointCount comes from a fresh endpoint enumeration
inline NetworkModeEvent DecideNetworkModeEvent(NetworkMode mode, size_t realEndpointCount) {
    if (mode == NetworkMode::Normal) {
        return realEndpointCount == 0 ? NetworkModeEvent::EnterStandby : NetworkModeEvent::None;
    }
    return realEndpointCount > 0 ? NetworkModeEvent::LeaveStandby : NetworkModeEvent::None;
}

// one copy of every user visible sentence so no front end can drift
inline const wchar_t* StandbyEnteredGuiText() {
    return L"Network connection lost.\n\nThe server is in standby and will resume by itself when the network returns.";
}

inline const wchar_t* StandbyEnteredCliText() {
    return L"warning: network connection lost, waiting in standby";
}

#endif // NETWORK_MODE_H
