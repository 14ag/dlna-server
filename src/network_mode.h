#ifndef NETWORK_MODE_H
#define NETWORK_MODE_H

#include <cstddef>
#include <string>

// pure decisions for localhost mode
// no windows types and no posix types so it compiles on both platforms
// mirrors the extraction pattern of close_pending_state h and health_check_policy h
enum class NetworkMode {
    Normal,
    Localhost
};

enum class NetworkModeEvent {
    None,
    Lost,
    Restored
};

inline NetworkMode DecideStartMode(size_t realEndpointCount) {
    return realEndpointCount == 0 ? NetworkMode::Localhost : NetworkMode::Normal;
}

// mode is the current mode and realEndpointCount comes from a fresh endpoint enumeration
// restoredAlreadyAnnounced stops the restored event from repeating until the network is lost again
inline NetworkModeEvent DecideNetworkModeEvent(NetworkMode mode, size_t realEndpointCount, bool restoredAlreadyAnnounced) {
    if (mode == NetworkMode::Normal) {
        return realEndpointCount == 0 ? NetworkModeEvent::Lost : NetworkModeEvent::None;
    }
    return (realEndpointCount > 0 && !restoredAlreadyAnnounced) ? NetworkModeEvent::Restored : NetworkModeEvent::None;
}

inline std::wstring LocalhostEndpointText(int port) {
    return L"127.0.0.1:" + std::to_wstring(port);
}

// one copy of every user visible sentence so no front end can drift
inline const wchar_t* NetworkLostGuiText() {
    return L"Network connection lost.\n\nThe server is now in localhost mode and can only be reached from this computer.";
}

inline const wchar_t* NetworkRestoredGuiText() {
    return L"Network connection restored.\n\nA server restart is needed to make the server visible to other devices again.\n\nRestart server now?";
}

inline const wchar_t* NetworkLostCliText() {
    return L"warning: network connection lost, switching to localhost mode";
}

inline const wchar_t* LocalhostRunningCliText() {
    return L"Server Running on localhost";
}

inline const wchar_t* NetworkRestoredCliText() {
    return L"network connection restored, run the same command again to restart in normal mode";
}

#endif // NETWORK_MODE_H
