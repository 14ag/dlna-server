# Feature Spec: PDF-Derived DLNA Framework Readiness

**Source**: `../2_28_2011_Ajitabh_Prakash_Saxena.pdf`
**Feature boundary**: DLNA media server framework behavior described in chapters II, V, VI, and appendices
**Language**: N/A; PDF design source

## 1. Feature Summary

The PDF describes a DLNA media server as a UPnP device that must be discoverable, self-describing, controllable through SOAP, event-capable through GENA, and able to expose media as DIDL entries with retrievable HTTP stream URLs. The server should keep mandatory UPnP/DLNA behavior small and direct, while making optional heavier features, such as AVTransport or a persistent media database, separable upgrades.

## 2. Input Catalog

| ID | Name | Type | Source | Constraints | Required |
|----|------|------|--------|-------------|----------|
| I-01 | SSDP discovery request | UDP HTTP-like request | UPnP control point | Must include search target for useful responses | Yes |
| I-02 | Device/service description request | HTTP GET/HEAD | UPnP control point | Must target advertised URLs | Yes |
| I-03 | SOAP control request | HTTP POST XML | UPnP control point | Must target service `controlURL` and include valid XML body | Yes |
| I-04 | Event subscription request | HTTP SUBSCRIBE/UNSUBSCRIBE | UPnP control point | Initial subscribe needs `CALLBACK` and `NT: upnp:event`; unsubscribe needs `SID` | Yes for event-capable services |
| I-05 | Media source set | Folders, files, playlists, network URLs | App configuration | Must be readable or probeable | Yes |
| I-06 | Media transfer request | HTTP GET/HEAD | Renderer/client | Must target a media, subtitle, icon, or album-art URL | Yes |

## 3. Output Catalog

| ID | Name | Type | Condition | Consumer |
|----|------|------|-----------|----------|
| O-01 | SSDP advertisement/search response | UDP response | On startup and M-SEARCH | Control points |
| O-02 | Device and service XML | XML | On description URL request | Control points |
| O-03 | SOAP response or SOAP fault | XML | On control action | Control points |
| O-04 | Event subscription response | HTTP headers | On GENA event request | Event subscribers |
| O-05 | DIDL media listing | XML escaped in SOAP | On Browse/Search | Control points |
| O-06 | Media bytes and headers | HTTP response | On stream request | Renderers |

## 4. Process Map

### P-01: Advertise And Resolve Device

- **Trigger**: Server start or control point search.
- **Logic**: Advertise all search targets, include a live description URL, and return stable UPnP-shaped headers.
- **Inputs**: I-01
- **Outputs**: O-01, O-02
- **Dependencies**: UDP multicast/unicast, HTTP listener
- **Failure modes**: Missing advertised URL causes control point diagnosis failure.

### P-02: Serve Service Descriptions

- **Trigger**: Client fetches description or SCPD URLs.
- **Logic**: Return device XML, icon URLs, service URLs, required ContentDirectory actions, ConnectionManager actions, and event subscription URLs.
- **Inputs**: I-02
- **Outputs**: O-02
- **Dependencies**: HTTP listener
- **Failure modes**: Declared URLs that are not served break discovery tools.

### P-03: Handle SOAP Control

- **Trigger**: Client posts to a service control URL.
- **Logic**: Validate XML, route action, parse arguments, return SOAP result or UPnP fault.
- **Inputs**: I-03, I-05
- **Outputs**: O-03, O-05
- **Dependencies**: Content index, DLNA metadata helpers
- **Failure modes**: Invalid XML or arguments return SOAP faults.

### P-04: Accept Event Subscriptions

- **Trigger**: Client sends SUBSCRIBE or UNSUBSCRIBE to an advertised event URL.
- **Logic**: Validate GENA headers, return subscription ID and timeout for subscriptions, and accept unsubscribe by SID.
- **Inputs**: I-04
- **Outputs**: O-04
- **Dependencies**: HTTP listener
- **Failure modes**: Missing subscription headers return precondition failure.

### P-05: Stream Media Resources

- **Trigger**: Renderer requests a media URL from DIDL.
- **Logic**: Serve HEAD/GET, byte ranges, content features, subtitles, album art, and icons.
- **Inputs**: I-06
- **Outputs**: O-06
- **Dependencies**: Local filesystem or remote source reader
- **Failure modes**: Missing or invalid item IDs return HTTP errors.

## 5. Requirements Spec Sheet

### User Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| UR-01 | The control point shall be able to discover the server and retrieve every advertised description URL. | Must |
| UR-02 | The control point shall be able to browse, search, and inspect media compatibility metadata. | Must |
| UR-03 | The control point shall be able to subscribe or unsubscribe to advertised event URLs without protocol failure. | Should |
| UR-04 | The renderer shall be able to retrieve media bytes using HTTP GET, HEAD, and byte-range requests. | Must |

### Functional Requirements

| ID | Requirement | Satisfies |
|----|-------------|-----------|
| FR-01 | The server shall advertise rootdevice, UUID, MediaServer, ContentDirectory, and ConnectionManager targets. | UR-01 |
| FR-02 | The server shall serve every advertised SCPD, control, event, icon, media, subtitle, and album-art URL. | UR-01, UR-02 |
| FR-03 | The server shall implement ContentDirectory capability actions, Browse, and Search. | UR-02 |
| FR-04 | The server shall implement ConnectionManager protocol-info actions for direct-stream media. | UR-02 |
| FR-05 | The server shall respond to GENA SUBSCRIBE and UNSUBSCRIBE on advertised event URLs. | UR-03 |
| FR-06 | The server shall provide correct HTTP headers for stream requests, including range behavior. | UR-04 |

### Non-Functional Requirements

| ID | Category | Requirement | Evidence |
|----|----------|-------------|---------|
| NFR-01 | Compatibility | XML responses shall be UTF-8 and UPnP-shaped. | P-02, P-03 |
| NFR-02 | Reliability | Invalid SOAP and event requests shall return protocol-shaped errors instead of hanging. | P-03, P-04 |
| NFR-03 | Performance | Stream handling shall avoid whole-file buffering for media payloads. | P-05 |

## 6. Assumptions

| ID | Statement |
|----|-----------|
| ASS-01 | AUTO-IP is not implemented because this desktop app depends on the host OS network stack. |
| ASS-02 | AVTransport/RTP/RTSP remain future work because current server is direct-stream HTTP only. |
| ASS-03 | A persistent media database remains future work; current in-memory index is acceptable for the present app scale. |

## 7. Open Questions

| ID | Question |
|----|----------|
| OQ-01 | Should event subscriptions later send asynchronous NOTIFY callbacks on every SystemUpdateID change? |
| OQ-02 | Should optional AVTransport/RTSP support be added only after renderer profile detection exists? |

## 8. Cleanroom Firewall Notice

This specification was produced by analysis of a design PDF. It contains no reproduced implementation code. A second implementer working from this document alone, without access to this server source, should be able to produce a functionally equivalent protocol-readiness feature set.

## Implementation Blueprint

### Patterns & Conventions Found

- Shared protocol behavior lives in `src/contentdirectory.cpp`, `src/dlna_utils.cpp`, and `src/media_sources.cpp`.
- Windows and POSIX HTTP servers intentionally mirror routes and protocol checks in `src/httpserver.cpp` and `src/posix_httpserver.cpp`.
- Tests are source/static plus blackbox smoke; new protocol behavior needs both.

### Architecture Decision

Implement lightweight GENA event URL handling now. This closes the biggest PDF-visible gap because device XML advertises `eventSubURL` values, and diagnosis tools expect those endpoints to respond. Keep persistent subscription storage and async callback NOTIFY as future work to avoid adding fragile network state before diagnostics require it.

### Component Design

- `src/httpserver.cpp`: Accept `SUBSCRIBE` and `UNSUBSCRIBE` for `/upnp/event/content_directory` and `/upnp/event/connection_manager`.
- `src/posix_httpserver.cpp`: Mirror the same event behavior.
- `tests/verify-smoke.ps1`: Probe subscribe/unsubscribe over HTTP.
- `tests/test_ums_hardening_sources.py`: Lock static parity across Windows/POSIX.

### Build Sequence

1. Add event request parsing and response helpers.
2. Route GENA methods before normal GET/POST handling.
3. Add blackbox smoke coverage for subscription lifecycle.
4. Update docs and changelog.
5. Run full pytest, Windows build/install/smoke, POSIX build.
