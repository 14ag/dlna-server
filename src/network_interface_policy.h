#ifndef NETWORK_INTERFACE_POLICY_H
#define NETWORK_INTERFACE_POLICY_H

// Pure decision for whether a network adapter that is NOT explicitly
// named in NetworkInterfaceAllowList should still be used to advertise
// SSDP/HTTP endpoints. No Win32 types, no POSIX types -- takes plain
// bools decided by the caller from live adapter enumeration. See
// dlna-server-network-endpoint-gateway-fix-workflow-30-7-26.md for the
// full citation trail.
//
// isLikelyVirtualByName: the adapter's friendly/interface name matched
//   a known virtual-adapter substring/prefix (VirtualBox, VMware,
//   Hyper-V internal switches, WSL, Docker, VPN tunnel/loopback
//   adapters, etc.)
// hasDefaultGateway: THIS specific adapter has at least one configured
//   default-route gateway (IPv4 or IPv6). This is the discriminator
//   between a virtual adapter that only provides host-local/NAT
//   connectivity (Hyper-V Default Switch, Docker's default bridge --
//   no gateway) and one that bridges real LAN traffic (Hyper-V
//   External Switch, a bridged VM adapter, a Linux bridge holding a
//   physical NIC -- has a gateway, inherited from the bridged network).
//
// A name match alone must never be sufficient to exclude an adapter:
// legitimate LAN-bridging setups routinely produce adapter names that
// contain a virtualization-vendor substring. A configured default
// gateway is the signal that the adapter can actually reach devices
// beyond the local segment, which a purely cosmetic name cannot prove
// or disprove either way.
inline bool ShouldUseUnlistedInterface(bool isLikelyVirtualByName, bool hasDefaultGateway) {
    if (!isLikelyVirtualByName) return true;
    return hasDefaultGateway;
}

#endif // NETWORK_INTERFACE_POLICY_H
