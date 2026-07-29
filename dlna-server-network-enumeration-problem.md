# DLNA Server Network Endpoint Enumeration Failure

## Problem

The DLNA server cannot start on machines where the only active network
adapters are Hyper-V virtual Ethernet (vEthernet) adapters.  This happens
when:

- The physical Ethernet or Wi-Fi adapter is bridged through Hyper-V External
  Switch (common when WSL2, Hyper-V VMs, or Windows Sandbox are enabled)
- The physical adapter is "media disconnected" and only virtual adapters
  remain active
- Any scenario where `GetAdaptersAddresses` returns only adapters whose
  FriendlyName contains "vEthernet" (or other virtual-adapter keywords)

## Symptoms

- Server process starts but exits immediately without binding any port
- `debug.log` contains exactly two lines:
  ```
  Network endpoint enumeration failed.
  Failed to find any active network endpoint for discovery.
  ```
- No ports are opened; the HTTP server never starts
- All integration tests that launch the server fail with "Server did not
  listen on port X within 15s"

## Root Cause

The defect is in the `InterfaceAllowListPermits` function in
`src/netutils.cpp` (Windows) and `src/posix_netutils.cpp` (POSIX).

### Windows (src/netutils.cpp, line 119-121)

```cpp
bool InterfaceAllowListPermits(const std::wstring& allowList,
                               const std::wstring& friendlyName) {
    if (allowList.empty()) {
        return !IsLikelyVirtualAdapter(friendlyName);
    }
    // ... named-interface whitelist scanning follows
}
```

When no `NetworkInterfaceAllowList` is configured (the common case --
users do not typically specify a whitelist), the function applies a
hardcoded denial filter: it rejects **every** adapter whose FriendlyName
matches any entry in `kVirtualAdapterDenylist`:

```cpp
const wchar_t* const kVirtualAdapterDenylist[] = {
    L"virtualbox", L"vmware", L"hyper-v", L"virtual switch", L"wsl",
    L"docker", L"tap-windows", L"tailscale", L"zerotier", L"loopback",
    L"npcap loopback", L"vethernet",              // <-- THIS IS THE TRIGGER
};
```

On the affected machine, `GetAdaptersAddresses(AF_UNSPEC, ...)` returns
these adapters (filtered to those that are `IfOperStatusUp` and have valid
IP addresses):

| FriendlyName | IfType | Flags | IPv4 |
|---|---|---|---|
| vEthernet (Default Switch) | 6 (Ethernet) | 0x... | 172.29.112.1 |
| vEthernet (External Switch) | 6 (Ethernet) | 0x... | 192.168.100.163 |

Both contain "vEthernet" (or "vethernet" case-insensitively), so
`IsLikelyVirtualAdapter` returns `true`, `InterfaceAllowListPermits`
returns `false`, and `EnumerateNetworkEndpoints` skips them.  The
returned endpoint vector is empty, and `Server::Start` at line 260-263
treats an empty endpoint list as a fatal error:

```cpp
if (endpoints.empty()) {
    LogPrint(L"Failed to find any active network endpoint for discovery.");
    outReason = L"No active network endpoints found";
    return false;
}
```

### POSIX (src/posix_netutils.cpp, line 61-63)

The identical logic exists on the POSIX side:

```cpp
bool InterfaceAllowListPermits(const std::wstring& allowList,
                               const char* ifaceName) {
    if (allowList.empty()) {
        return !IsLikelyVirtualAdapterName(ifaceName);
    }
    // ...
}
```

With prefix-matching denylist:

```cpp
const char* const kVirtualAdapterNamePrefixes[] = {
    "docker", "veth", "vboxnet", "vmnet", "tailscale", "zt", "wg",
    "tun", "tap", "br-",
};
```

On a Linux host where the only active interfaces use virtual
(veth, docker, bridge) names, the same problem would occur.

## Why This Is Wrong

The intent of the virtual-adapter filter is to prevent the server from
advertising a link-local or container-internal address that is not
reachable from other LAN devices.  However, a Hyper-V External Switch
adapter **is** reachable from the LAN -- it has the same MAC, same IP
subnet, and same default gateway as the physical adapter it bridges.
The switch itself is transparent to the network layer.  Filtering it
out leaves no reachable endpoint, making the server completely
unusable on one of the most common Windows virtualization
configurations (Windows 10/11 Pro with WSL2 or Hyper-V enabled).

The check is overly broad: it uses substring containment rather than
heuristics that distinguish "virtual adapter that provides actual LAN
connectivity" from "virtual adapter that only provides host-local or
container-internal connectivity."

## Environment Details

- Windows 11 (or 10, untested but likely same)
- Hyper-V enabled (unavoidable with WSL2, Docker Desktop WSL2 backend,
  Windows Sandbox, or Windows Subsystem for Android)
- Physical network adapter bridged through Hyper-V External Switch,
  replaced by a vEthernet adapter with the same MAC and IP config
- DLNA Server fails to start at all

## What I Tried During Investigation

1. Ran server with `DLNA_SERVER_SKIP_FIREWALL=1` and `--headless`:
   Confirmed the failure is not firewall-related.

2. Added `NetworkInterfaceAllowList=vEthernet` to config.ini:
   This **works** as a workaround if the empty-allowlist default is
   changed or if the user explicitly names the adapter.  This confirms
   the root cause is the empty-allowlist path taking the virtual-adapter
   rejection route.

3. Verified adapter status with .NET `NetworkInterface` API:
   ```
   --- vEthernet (Default Switch) ---
     OperationalStatus: Up
     SupportsMulticast: True
     Speed: 10000 Mbps
     IPv4: 172.29.112.1

   --- vEthernet (External Switch) ---
     OperationalStatus: Up
     SupportsMulticast: True
     Speed: 72 Mbps
     IPv4: 192.168.100.163
   ```
   Both adapters are up, support multicast (required for SSDP), and have
   valid routable IPs.  .NET correctly considers them usable.

4. Verified `GetAdaptersAddresses` with C++ call:
   The C++ API returns the same adapters with `IfType=6` (Ethernet) and
   `OperStatus=IfOperStatusUp`.  The `Flags` field does NOT have
   `IP_ADAPTER_NO_MULTICAST` set.

5. Checked if the `IP_ADAPTER_NO_MULTICAST` flag is set:
   It is not.  The only filter that triggers is the name-based virtual-
   adapter denylist.

## What I Need from You

I need a solution that distinguishes between:

- **Virtual adapters that provide real LAN connectivity** (Hyper-V
  External Switch, bridged adapters) -- should be INCLUDED.
- **Virtual adapters that provide only host-local or container-internal
  connectivity** (Docker nat, WSL internal switch, Hyper-V Default
  Switch, Tailscale, ZeroTier, loopback) -- should continue to be
  EXCLUDED.

Potential approaches to evaluate (you decide which is best):

1. **Remove "vethernet" from the denylist** and instead use a more
   precise filter (e.g., check if the adapter has a non-link-local,
   non-APIPA IPv4 address and a default gateway -- Hyper-V Default
   Switch has no gateway, External Switch does).

2. **Check for a default gateway on the adapter** as a heuristic:
   Hyper-V External Switch inherits the physical gateway; the Default
   Switch has no gateway.  An adapter with an IPv4 address AND a
   default gateway is likely providing real LAN connectivity.

3. **Change the empty-allowlist default behavior**: instead of denying
   virtual adapters by default, include them if they have a routable
   (non-link-local, non-APIPA) IPv4 address that is not in a
   known-container subnet.

4. **Add a config setting** to control whether virtual adapters are
   automatically excluded or included, defaulting to included (safer
   for the common Hyper-V case).

Please provide a patch or detailed implementation steps.  I will
apply them to both `src/netutils.cpp` (Windows) and
`src/posix_netutils.cpp` (POSIX).
