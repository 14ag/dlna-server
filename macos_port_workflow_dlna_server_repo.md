# macOS Port Workflow for the `14ag/dlna-server` Repository

Repository:
[14ag/dlna-server](https://github.com/14ag/dlna-server?utm_source=chatgpt.com)

This workflow assumes the project is currently Windows-focused or Linux-first and needs a production-grade macOS port with reliable DLNA discovery, media serving, and compatibility with clients such as VLC, Windows Media Player, Kodi, BubbleUPnP, Samsung TVs, LG TVs, and Apple ecosystem devices.

The workflow is structured to minimize platform-specific divergence and avoid maintaining separate networking implementations for each OS.

---

# 1. Initial Repository Audit

Before touching code, establish the exact architecture boundaries.

Create a platform inventory document covering:

- Networking layer
- SSDP discovery implementation
- HTTP server implementation
- XML generation
- File system abstraction
- Socket APIs
- Multicast handling
- Media indexing
- MIME detection
- Threading model
- Build system
- Dependency graph
- IPC boundaries
- Service lifecycle handling
- Logging subsystem
- Interface enumeration logic

Focus especially on:

- Any direct WinSock usage
- Windows-only interface enumeration APIs
- Windows firewall assumptions
- Windows path separators
- UTF-16 filesystem handling
- Windows threading primitives
- COM usage
- Named pipes
- Registry usage
- Service manager dependencies
- Case-insensitive path assumptions

Goal:

Separate the codebase into:

1. Pure cross-platform logic
2. OS abstraction layer
3. Platform-specific backend implementations

---

# 2. Build System Migration

If the project uses:

- Visual Studio solution files only
- MSBuild-only configuration
- Windows SDK assumptions

Then migrate to:

- CMake recommended
- Meson acceptable
- Premake acceptable

Recommended structure:

- core/
- platform/
  - windows/
  - macos/
  - linux/
- net/
- ssdp/
- upnp/
- media/
- http/
- third_party/
- tests/

CMake goals:

- Single unified target graph
- Per-platform compile definitions
- Optional feature toggles
- Shared compiler warnings
- CI-friendly builds
- Universal binary support

macOS targets:

- x86_64
- arm64

Generate:

- Universal binaries using:
  - `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"`

---

# 3. Replace Windows-Specific Networking

DLNA reliability is mostly determined by SSDP behavior.

macOS networking differences commonly break ports.

Audit every use of:

- WinSock
- IP Helper API
- `WSAStartup`
- `SO_EXCLUSIVEADDRUSE`
- `GetAdaptersAddresses`
- `WSARecvFrom`
- `closesocket`
- overlapped IO

Replace with a POSIX-compatible networking layer.

Recommended abstraction:

```cpp
class UdpSocket {
public:
    bool bind(...);
    bool sendTo(...);
    bool receiveFrom(...);
    bool joinMulticast(...);
    bool setReuse(...);
};
```

macOS specifics:

- Use BSD sockets
- Use `SO_REUSEADDR`
- Use `IP_ADD_MEMBERSHIP`
- Use `if_nametoindex()` for interface binding
- Use `getifaddrs()` for interface enumeration

Do not assume:

- Interface ordering
- Single active interface
- IPv4-only environments
- Stable interface indexes

---

# 4. SSDP Compliance Hardening

DLNA detection failures are usually SSDP failures.

Your implementation must support:

- M-SEARCH response handling
- Periodic NOTIFY advertisements
- UUID persistence
- Correct CACHE-CONTROL values
- Proper LOCATION headers
- Interface-aware multicast
- Multi-interface advertisements
- Device description accessibility

Required multicast details:

- Address: `239.255.255.250`
- Port: `1900`
- TTL usually `2`

Critical headers:

```text
CACHE-CONTROL: max-age=1800
EXT:
LOCATION: http://<ip>:<port>/description.xml
SERVER: macOS/14 UPnP/1.1 DLNAD/1.0
ST: upnp:rootdevice
USN: uuid:<uuid>::upnp:rootdevice
```

Implement:

- Retry advertisement scheduling
- Interface rebinding after network changes
- Wake-from-sleep recovery
- Graceful multicast leave handling

macOS-specific issue:

After sleep/wake:

- sockets may silently stop receiving multicast
- interface indexes may change
- bound interfaces may become invalid

Add network-change monitoring using:

- `SCNetworkReachability`
- `NWPathMonitor`
- or periodic interface polling

---

# 5. HTTP Server Compatibility

Many DLNA clients aggressively validate HTTP behavior.

Verify support for:

- HEAD requests
- Range requests
- Partial content
- Chunked transfer
- Persistent connections
- Correct Content-Length
- MIME accuracy
- UTF-8 filenames
- URL escaping

Required responses:

- `206 Partial Content`
- `Accept-Ranges: bytes`
- Correct `Content-Type`

Clients that heavily depend on this:

- VLC
- Samsung TVs
- Sony Bravia TVs
- Kodi
- Windows Media Player

Avoid:

- Incorrect keepalive handling
- Dynamic chunked responses without lengths
- Invalid XML encoding
- Slow header generation

---

# 6. XML and DIDL-Lite Validation

DLNA clients are stricter than browsers.

Validate:

- Device description XML
- SCPD XML
- DIDL-Lite responses
- SOAP responses

Common failure points:

- Missing namespaces
- Invalid escaping
- Incorrect object IDs
- Malformed containers
- Invalid XML declarations
- UTF-8 BOM issues

Test against:

- VLC
- Kodi
- Windows Media Player
- BubbleUPnP

Use XML schema validation.

Recommended:

- libxml2
- pugixml
- tinyxml2

---

# 7. File System Porting

macOS filesystem behavior differs from Windows.

Audit:

- Path separator assumptions
- Case sensitivity assumptions
- UTF-16 conversion logic
- Long path handling
- Symlink traversal
- Recursive scanning logic

Recommended:

Use `std::filesystem`.

macOS-specific considerations:

- APFS may be case-sensitive or case-insensitive
- External drives often behave differently
- Unicode normalization differs from Windows

Normalize filenames before indexing.

---

# 8. Media MIME Detection

Incorrect MIME types break playback.

Do not rely solely on extensions.

Recommended approaches:

- UTType APIs on macOS
- ffmpeg/libavformat probing
- mime-db fallback

Critical mappings:

- mp4
- mkv
- mp3
- flac
- jpeg
- png
- avi
- ts
- mpeg

Validate every response header.

---

# 9. Service Lifecycle Integration

macOS equivalents:

Windows Service -> launchd daemon

Create:

- LaunchAgent for user sessions
- LaunchDaemon for system-wide service

Files:

```text
~/Library/LaunchAgents/
/Library/LaunchDaemons/
```

Implement:

- auto-start
- graceful shutdown
- sleep handling
- network change handling
- restart-on-crash

---

# 10. Firewall and Permissions

macOS application firewall may block discovery.

Requirements:

- Signed binaries
- Proper entitlements if sandboxed
- Incoming network permission prompts

Without signing:

- multicast traffic may appear unreliable to users
- first-launch behavior becomes inconsistent

Recommended:

- Developer ID signing
- Hardened runtime
- notarization

---

# 11. Apple Silicon Compatibility

Test on:

- Intel Macs
- Apple Silicon Macs

Audit:

- pointer assumptions
- alignment assumptions
- endian-sensitive parsing
- assembly optimizations
- SIMD code

Build matrix:

```text
macOS Intel
macOS ARM64
Linux x64
Windows x64
```

---

# 12. Observability and Diagnostics

Add detailed runtime diagnostics.

Required logs:

- SSDP packets sent
- SSDP packets received
- Interface binding events
- HTTP requests
- SOAP actions
- XML generation
- Browse requests
- Streaming failures
- Range request handling

Recommended packet logging:

```text
[SSDP RX]
[SSDP TX]
[HTTP RX]
[SOAP]
[DLNA]
```

Add optional packet dumps.

---

# 13. Automated Compatibility Test Matrix

Test clients:

- VLC macOS
- VLC Windows
- VLC Android
- Windows Media Player
- Kodi
- BubbleUPnP
- Samsung TV
- LG TV
- Android TV
- Sony Bravia

Test scenarios:

- cold discovery
- server restart
- sleep/wake
- interface switch
- WiFi reconnect
- ethernet to WiFi migration
- IPv6 enabled
- multiple NICs
- large libraries
- malformed filenames
- long-duration streaming

---

# 14. Discovery Debugging Workflow

When clients fail to detect the server:

Use packet captures immediately.

Tools:

- Wireshark
- tcpdump

Filter:

```text
udp.port == 1900
```

Verify:

1. M-SEARCH arrives
2. Server receives M-SEARCH
3. Server replies unicast
4. Client receives reply
5. LOCATION URL reachable
6. description.xml valid
7. ContentDirectory service accessible

Typical failure chain:

```text
multicast works
→ response malformed
→ client silently ignores server
```

or:

```text
M-SEARCH never reaches server
→ interface binding incorrect
→ multicast membership broken
```

---

# 15. Recommended macOS Architecture

Recommended stack:

```text
Core logic
↓
Cross-platform abstractions
↓
macOS backend
↓
BSD sockets + launchd + APFS
```

Avoid:

- Objective-C dependencies in core logic
- platform-specific branching everywhere
- duplicated SSDP logic
- duplicated HTTP stack

Keep:

- one protocol implementation
- isolated platform adapters

---

# 16. CI/CD Pipeline

Use GitHub Actions.

Required jobs:

- Windows build
- Linux build
- macOS Intel build
- macOS ARM64 build
- unit tests
- integration tests
- XML validation
- protocol compliance tests

macOS runners:

```yaml
runs-on: macos-latest
```

Artifacts:

- universal binary
- signed app bundle
- notarized DMG

---

# 17. Packaging

Recommended outputs:

- `.app`
- `.dmg`
- Homebrew formula

Optional:

- menu bar app
- launch-at-login
- web UI
- Bonjour advertisement

---

# 18. Long-Term Stability Improvements

After the macOS port works:

Prioritize:

- SSDP robustness
- malformed client tolerance
- XML correctness
- streaming resilience
- reconnect handling
- media cache indexing
- transcoding abstraction

Eventually add:

- IPv6 support
- HTTP/2 optional serving
- Bonjour bridging
- AirPlay interoperability
- adaptive transcoding
- database-backed indexing

---

# 19. Reference Implementations Worth Studying

Cross-platform DLNA servers:

- entity["company","Universal Media Server",""]
- anacrolix/dms
- MiniDLNA / ReadyMedia
- Gerbera
- Macast

Useful protocol references:

- UPnP Device Architecture
- DLNA interoperability guidelines
- SSDP RFC references

---

# 20. Recommended Order of Execution

Phase 1:

- isolate platform code
- migrate build system
- abstract sockets

Phase 2:

- port SSDP
- port HTTP serving
- validate XML

Phase 3:

- implement launchd integration
- fix multicast edge cases
- add diagnostics

Phase 4:

- client compatibility testing
- sleep/wake recovery
- packaging/signing

Phase 5:

- CI/CD
- notarization
- performance tuning
- release stabilization

---

# Most Likely Detection Failure Categories

Based on common DLNA implementation failures across hobbyist and early-stage servers, the highest-probability causes are:

1. SSDP multicast binding issues
2. Incorrect M-SEARCH responses
3. Invalid LOCATION URL
4. Broken device description XML
5. Missing ContentDirectory service definition
6. Incorrect CACHE-CONTROL or USN headers
7. Server binding only to localhost
8. Firewall blocking UDP 1900
9. Interface enumeration selecting wrong NIC
10. Invalid HTTP range request handling

These are substantially more common than media indexing bugs.

---

# Recommended Immediate Validation Checklist

Before implementing the full macOS port:

1. Verify SSDP packets in Wireshark
2. Validate description.xml manually
3. Test VLC discovery
4. Test Windows Media Player discovery
5. Validate SOAP Browse responses
6. Confirm HTTP range requests work
7. Verify multiple network interfaces
8. Test sleep/wake behavior
9. Confirm multicast membership survives reconnects
10. Validate UUID persistence across restarts

