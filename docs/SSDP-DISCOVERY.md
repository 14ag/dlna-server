# SSDP and Discovery

Implemented in `src/ssdp.cpp` (Windows) and `src/posix_ssdp.cpp` (POSIX), driven by endpoints from `EnumerateNetworkEndpoints()` (`src/netutils.cpp` / `src/posix_netutils.cpp`).

## Endpoint enumeration

`EnumerateNetworkEndpoints(port, endpoints)` walks active, multicast-capable, non-loopback adapters and builds one `NetworkEndpoint` per usable address:

- **Windows** (`GetAdaptersAddresses`): skips `IF_TYPE_SOFTWARE_LOOPBACK` and adapters with `IP_ADAPTER_NO_MULTICAST`. For each adapter, every non-APIPA IPv4 unicast address is added; for IPv6, only the single best-ranked address per adapter is kept (`IPv6Rank`: routable > link-local > excluded — unspecified, loopback, multicast).
- **POSIX** (`getifaddrs`): requires `IFF_UP | IFF_MULTICAST`, excludes `IFF_LOOPBACK`, excludes IPv4 APIPA (169.254.0.0/16).
- Each `NetworkEndpoint` carries the raw `sockaddr`, interface index, on-link prefix length, and a pre-built `locationUrl` (`http://<host>:<port>/description.xml`).
- IPv6 zone/scope IDs are never embedded in the advertised `locationUrl` — RFC 6874 treats a zone ID as purely local to the originating node. The interface index needed for socket-level joins is carried separately in `NetworkEndpoint::interfaceIndex` / `sockaddr`, not in the URL text (`BuildEndpointHost` in `netutils.cpp`).

## Multicast join

- IPv4: `239.255.255.250:1900`, joined via `IP_ADD_MEMBERSHIP` per endpoint, with `imr_interface` set to that endpoint's local address — **not** an interface index. `UpnpInit2`-style implementations that pass an adapter GUID or friendly name here get silent fallback to the wrong interface; this codebase always resolves to the concrete IPv4 address first.
- IPv6: `ff02::c` (link-local, per RFC 4291 scope conventions used by SSDP over IPv6), joined via `IPV6_ADD_MEMBERSHIP`/`IPV6_JOIN_GROUP` with `ipv6mr_interface` set to the numeric interface index.
- Windows logs a join failure per adapter (`WSAGetLastError()`), and if **no** adapter joins successfully on either family, `SSDP::Start()` fails outright rather than running with a socket that can't hear discovery traffic.
- Outbound interface selection for unicast (`M-SEARCH` responses) and multicast (`NOTIFY`) is set per-send via `IP_MULTICAST_IF`/`IP_UNICAST_IF` (IPv4) or `IPV6_MULTICAST_IF`/`IPV6_UNICAST_IF` (IPv6) — a single socket serves every interface, so the outbound interface has to be pinned before each `sendto()`, guarded by `m_socketMutex`.

## Advertised targets

Five `NT`/`ST` targets are advertised per the UPnP Device Architecture, built from the device UUID (`BuildAdvertisedTargets`):

```
upnp:rootdevice
uuid:<device-uuid>
urn:schemas-upnp-org:device:MediaServer:1
urn:schemas-upnp-org:service:ContentDirectory:1
urn:schemas-upnp-org:service:ConnectionManager:1
```

## Timing

- **Startup jitter**: 0–100 ms before the first `ssdp:alive` burst (`ComputeSsdpStartupJitterMilliseconds`), per UPnP Device Architecture guidance to avoid network storms when multiple devices start together.
- **Alive re-advertisement interval**: randomized between 12 and 14.5 minutes (`ComputeSsdpNextAliveIntervalMilliseconds`). `CACHE-CONTROL: max-age=1800` (30 minutes) is advertised, and the spec calls for re-advertising at a randomly distributed interval under half that expiry — this interval satisfies that with margin, and the jitter keeps multiple restarted instances from refreshing in lockstep.
- **Startup burst**: three `ssdp:alive` NOTIFYs, 100 ms apart, sent immediately after the jitter delay.
- **Shutdown**: one `ssdp:byebye` burst before sockets close.
- **M-SEARCH responses**: delayed by a random interval derived from the request's `MX` header, capped at 5 seconds (`ComputeDelayMilliseconds`), and queued on a dedicated response worker thread (`ResponseWorker`) rather than sent inline from the receive loop. Duplicate in-flight responses to the same remote address with the same `ST`/`USN` set are coalesced (`CoalesceDelayedResponse`) so a flurry of near-duplicate `M-SEARCH` requests doesn't produce a flurry of duplicate responses.

## Endpoint selection for a specific peer

`SelectBestEndpoint(endpoints, remoteAddr)` picks which local endpoint's `LOCATION` to hand back to a given remote address — used both for `M-SEARCH` responses and for the initial device description `URLBase`. Scoring:

- Same address family as the remote address is required.
- +100 if the remote address falls within the endpoint's on-link prefix.
- +50 (Windows IPv6 path) if the remote's `sin6_scope_id` matches the endpoint's interface index.
- +10 if the endpoint isn't link-local.

If nothing scores, it falls back to any endpoint of the matching family, then to the first endpoint overall.

## Request validation

`HandleSearchRequest` requires:

- First line exactly `M-SEARCH * HTTP/1.1` (case-insensitive)
- `MAN` header equal to `"ssdp:discover"` or `ssdp:discover` (quotes optional, case-insensitive)
- `ST` either `ssdp:all` or one of the five advertised targets, case-insensitive exact match

Anything else is silently ignored — SSDP is UDP and unauthenticated, so responding to malformed or unrecognized requests only adds attack surface and log noise.
