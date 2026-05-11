# Code Review: `14ag/dlna-server`

**Repository:** https://github.com/14ag/dlna-server  
**Reviewed:** All source files under `src/` (Windows and POSIX paths), `CMakeLists.txt`, and supporting scripts  
**Language:** C++17 (Win32 GUI target + POSIX headless target)  
**Severity scale:** `CRITICAL` · `HIGH` · `MEDIUM` · `LOW` · `INFO`

---

## Executive Summary

The project is a functional, lightweight DLNA/UPnP media server with two build targets: a Win32 GUI application and a POSIX headless daemon. The architecture is well-partitioned — SSDP discovery, HTTP serving, content directory, media scanning, configuration, and networking utilities each occupy their own translation unit. For a personal-use LAN tool the code is readable and mostly correct.

However several issues ranging from crash-grade bugs to subtle race conditions were found. Most critical are the unguarded `std::stoi` calls on attacker-controlled network data and a race condition on the `m_running` flag in the SSDP thread. These need to be addressed before any broader deployment.

---

## 1  Security Findings

### 1.1 `CRITICAL` — Unguarded `std::stoi` on network input causes worker thread crash

**File:** `src/httpserver.cpp`  
**Location:** `HandleClient`, media path branch

```cpp
// path is built directly from the HTTP request line
int fileId = std::stoi(path.substr(7));
```

`std::stoi` throws `std::invalid_argument` if the string is not a valid integer and `std::out_of_range` if it overflows `int`. Neither is caught. A request such as `GET /media/ HTTP/1.1` (empty ID) or `GET /media/abc HTTP/1.1` silently terminates the thread-pool worker that services the connection. Under sustained attack this exhausts the thread pool.

**Fix:** wrap in try/catch and return 400 on failure.

```cpp
// parse fileId safely and return 404 on bad input
int fileId = -1;
try { fileId = std::stoi(path.substr(7)); } catch (...) {}
if (fileId < 0) { /* send 404 */ return; }
```

---

### 1.2 `CRITICAL` — `std::stoi` on `Content-Length` header also uncaught

**File:** `src/httpserver.cpp`  
**Location:** `HandleClient`, POST branch

```cpp
int contentLength = std::stoi(contentLengthHeader);
```

A client sending `Content-Length: 9999999999999` causes `std::out_of_range`. Same crash vector as 1.1.

**Fix:** use `TryParseInt` (already defined in `contentdirectory.cpp`) or add try/catch with an upper bound check.

---

### 1.3 `HIGH` — Single `recv` for full HTTP request headers is truncation-silent

**File:** `src/httpserver.cpp`  
**Location:** `HandleClient`

```cpp
char buf[HTTP_BUF_SIZE]; // 8192 bytes
int bytesRead = recv(clientSocket, buf, HTTP_BUF_SIZE - 1, 0);
```

HTTP headers plus a large POST body are read in one shot. If the client sends headers larger than 8 KB (unlikely but valid), the request is silently truncated. More practically, `bytesRead` is used without checking whether `\r\n\r\n` was received — the header/body split logic may operate on incomplete data. The POST body loop does continue reading, but the header parse is already committed to a potentially incomplete buffer.

**Fix:** read in a loop until `\r\n\r\n` is found or a max-header-size is exceeded.

---

### 1.4 `HIGH` — No receive timeout enables slow-read / Slowloris DoS

**File:** `src/httpserver.cpp`

No `SO_RCVTIMEO` is set on accepted client sockets. A client that connects and never sends data — or sends one byte at a time — holds a thread-pool slot indefinitely. With the pool capped at 8 threads (`SetThreadpoolThreadMaximum(m_threadPool, 8)`), eight such connections stall all legitimate requests.

**Fix:**

```cpp
// set 10 second receive timeout on accepted socket
DWORD timeout = 10000;
setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
           reinterpret_cast<const char*>(&timeout), sizeof(timeout));
```

---

### 1.5 `HIGH` — Race condition: `m_running` is non-atomic on Windows

**File:** `src/ssdp.h`, `src/ssdp.cpp`, `src/httpserver.h`, `src/httpserver.cpp`

The POSIX build uses `std::atomic<bool> m_running` (correct). The Windows build uses plain `bool m_running`. The flag is written by `Stop()` on the UI/main thread and read in `ThreadWorker` / `AcceptThreadWorker` on separate threads without any synchronisation primitive. This is a data race — undefined behavior per the C++ standard.

**Fix:** use `std::atomic<bool>` unconditionally, or gate the Win32 path behind a `volatile` + memory barrier at minimum. The cleanest solution removes the `#ifdef` and uses `std::atomic<bool>` in both builds.

---

### 1.6 `MEDIUM` — SSDP `Sleep` blocks discovery listener for up to 5 seconds

**File:** `src/ssdp.cpp`  
**Location:** `HandleSearchRequest`

```cpp
DWORD delayMs = ComputeDelayMilliseconds(mx);
if (delayMs > 0) {
    Sleep(delayMs); // blocks the only SSDP receive thread
}
```

The MX delay is UPnP-spec-compliant in principle, but implementing it as a blocking `Sleep` on the single listener thread means no M-SEARCH packets are serviced during the delay window. An adversary on the local network can force the full 5-second stall by setting `MX: 5` repeatedly.

**Fix:** queue responses with a timer or move response dispatch to a separate thread.

---

### 1.7 `MEDIUM` — IP whitelist uses exact string comparison; IPv6 zone IDs not stripped consistently

**File:** `src/ipwhitelist.cpp`  
**Location:** `IsAllowed`

`NormalizeIpLiteral` strips brackets and zone IDs. However, the incoming `clientIP` string in `AcceptThreadWorker` goes through `NormalizeIpLiteral(SockaddrToLiteral(...))`. `SockaddrToLiteral` calls `getnameinfo` with `NI_NUMERICHOST` which on Windows returns IPv6 addresses without brackets but sometimes with a `%ifindex` scope suffix. If the stored whitelist entry was entered without a scope suffix, the comparison will fail unexpectedly, silently blocking legitimate hosts.

**Fix:** audit the full normalisation path end-to-end with a unit test covering link-local IPv6 addresses.

---

### 1.8 `MEDIUM` — SOAP action dispatched by body substring, not SOAPAction header

**File:** `src/contentdirectory.cpp`  
**Location:** `HandleBrowse`

```cpp
if (req.find("GetSystemUpdateID") != std::string::npos) {
    // returns SystemUpdateID response
}
```

This matches any SOAP body containing that string — including a `Browse` request that references `GetSystemUpdateID` in a comment or a filter argument. The correct approach is to check the `SOAPAction` HTTP header before dispatching.

---

### 1.9 `LOW` — `SendNotifyBurst` races with `ThreadWorker` for socket ownership

**File:** `src/ssdp.cpp`  
**Location:** `SSDP::Start`

```cpp
m_hThread = CreateThread(NULL, 0, ThreadWorker, this, 0, NULL);
// ...
SendNotifyBurst("ssdp:alive", 3, 100);
```

`ThreadWorker` also calls `SetOutboundInterface` on the same sockets. There is no synchronisation between the burst and the newly started thread, so both can be in `setsockopt` / `sendto` on the same socket simultaneously. In practice this is benign (the burst happens before the thread processes any inbound packet) but is technically a race.

---

## 2  Correctness / Robustness Findings

### 2.1 `HIGH` — `WSAStartup` without matching `WSACleanup`

**File:** `src/server.cpp`  
**Location:** `Server::Server()`

```cpp
Server::Server() : m_running(false) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}
```

There is no `Server::~Server()` and no `WSACleanup` call anywhere in the codebase. Because `Server` is a function-static singleton this only matters at process exit, but it is still incorrect and prevents clean shutdown if the server is ever embedded in a larger process.

---

### 2.2 `HIGH` — `HttpServer::Stop` uses `WaitForSingleObject(handle, INFINITE)`

**File:** `src/httpserver.cpp`

If the accept thread is blocked on `select` when `Stop()` is called and the socket close does not unblock it (e.g. on some VPN drivers), the wait hangs forever, freezing the UI. A bounded wait with a fallback `TerminateThread` is safer.

---

### 2.3 `MEDIUM` — `ScanFolder` is unbounded recursive; deep trees overflow the stack

**File:** `src/media_sources.cpp`  
**Location:** `ScanFolder`

The function calls itself for every sub-directory with no depth limit. On Windows the default thread stack is 1 MB. A directory hierarchy more than ~300–400 levels deep (reparse-point loops, network shares with circular mounts) will overflow. The `FILE_ATTRIBUTE_REPARSE_POINT` flag is not filtered.

**Fix:** add a depth counter parameter and cap it, and skip reparse points with `FILE_ATTRIBUTE_REPARSE_POINT`.

---

### 2.4 `MEDIUM` — `GetChildren` is O(n) per Browse call

**File:** `src/media_sources.cpp`

```cpp
std::vector<MediaItem> MediaSources::GetChildren(int parentId) { /* linear scan */ }
```

Every `Browse` request scans the entire flat `m_items` vector. For libraries with tens of thousands of items, individual container Browse requests that display child counts (`AppMedia.GetChildren(it.id).size()` called per container) trigger O(n²) work per response.

**Fix:** build a `std::unordered_map<int, std::vector<int>>` child index during `Scan()` and invalidate it on rescan.

---

### 2.5 `MEDIUM` — Content-Length in Browse response uses `browseResp.length()` (byte count vs char count)

**File:** `src/httpserver.cpp`

```cpp
"Content-Length: " + std::to_string(browseResp.length())
```

`std::string::length()` returns the number of `char` elements, which equals the byte count for UTF-8 — this is correct. However `headers.length()` is also used for the HTTP header block of GET responses in the same pattern. Double-check that no wide-string-to-UTF8 conversion silently changes lengths after this point. Currently appears safe but fragile.

---

### 2.6 `MEDIUM` — Range request: `startByte` is never validated as non-negative

**File:** `src/httpserver.cpp`  
**Location:** `HandleClient`, Range handling

```cpp
if (!startStr.empty()) startByte = std::stoll(startStr);
```

`std::stoll` throws on non-numeric input (same issue as 1.1). Additionally the spec allows suffix-ranges (`bytes=-500`), where `startStr` would be empty and `endStr` would be `"500"` meaning "last 500 bytes". This case is parsed as `endByte = 500` — incorrect.

---

### 2.7 `LOW` — `TrimAscii` / `ToLowerAscii` / `FindHeaderValueCaseInsensitive` duplicated in 4 TUs

**Files:** `ssdp.cpp`, `httpserver.cpp`, `config.cpp`, `netutils.cpp`

Identical helper functions are copy-pasted into anonymous namespaces in four translation units. Any bug fix must be applied four times. Move shared utilities to `netutils.h/cpp` (already the designated utility module).

---

### 2.8 `LOW` — `Config` exposes all fields as public members

**File:** `src/config.h`

All configuration values (`port`, `serverName`, `mediaSources`, etc.) are direct public member variables on `Config`. Any code can mutate them without triggering a save or a validity check. This has already led to the pattern where `Server::Start` calls `AppConfig.Load()` as a guard — implying the caller does not trust the state of the singleton.

---

### 2.9 `LOW` — `Config::Load` calls `Config::Save` inside a load path

**File:** `src/config.cpp`  
**Location:** `Config::Load`

```cpp
if (deviceUUID.empty()) {
    deviceUUID = GenerateUUID();
    Save(); // writes file during a Load call
}
```

If `Load` is called from multiple threads (e.g. a hot-reload feature added later), this triggers a concurrent write. Currently safe because `Load` is only called from the UI thread, but worth isolating.

---

### 2.10 `INFO` — `Server::Stop` orders `SSDP::Stop()` before `HttpServer::Stop()`

**File:** `src/server.cpp`

```cpp
SSDP::Get().Stop();     // sends ssdp:byebye, then closes sockets
HttpServer::Get().Stop(); // closes listen sockets, waits for workers
```

This is the correct order: byebye before HTTP shutdown so renderers stop discovering. However, `SSDP::Stop` waits up to 2 seconds for its thread (`WaitForSingleObject(m_hThread, 2000)`). If that times out the thread is leaked — a handle leak. The return value of `WaitForSingleObject` is not checked.

---

## 3  Protocol Compliance

| Requirement | Status | Notes |
|---|---|---|
| UPnP 1.0 device description at `/description.xml` | ✅ Correct | |
| ContentDirectory SCPD at `/ContentDirectory.xml` | ✅ Correct | |
| ConnectionManager SCPD | ✅ Correct | |
| SSDP M-SEARCH response with MX delay | ⚠️ Partial | MX delay implemented but blocks listener (see 1.6) |
| SSDP ssdp:alive on start, ssdp:byebye on stop | ✅ Correct | |
| Periodic ssdp:alive every 15 minutes | ✅ Correct (900 000 ms) | |
| DLNA streaming headers | ✅ Present | `transferMode.dlna.org`, `contentFeatures.dlna.org` |
| HTTP range requests (206 Partial Content) | ⚠️ Partial | Suffix-range not supported (see 2.6) |
| SOAP GetSystemUpdateID | ⚠️ Fragile | Body substring match instead of SOAPAction header (see 1.8) |
| SOAP Browse BrowseMetadata | ✅ Basic | |
| SOAP Browse BrowseDirectChildren | ✅ Basic | |
| Event subscription (SUBSCRIBE/NOTIFY) | ❌ Not implemented | Required by UPnP spec for full compliance |
| ConnectionManager GetProtocolInfo | ❌ Stub only | XML served but no control endpoint handler |

---

## 4  Build System

### 4.1 `INFO` — No CI pipeline

The repository ships several PowerShell verification scripts (`verify-smoke.ps1`, `verify-posix-ssh.ps1`, `verify-android-smoke.ps1`) but no GitHub Actions workflow. The `.github/ISSUE_TEMPLATE` directory is present but no workflow YAML exists. Adding a basic build-and-test CI run would catch regressions automatically.

### 4.2 `INFO` — Static runtime (`MultiThreaded`) baked into release

```cmake
set_property(TARGET WinDLNAServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
```

Linking the MSVC CRT statically is a valid choice for a standalone executable but inflates binary size and means CRT security patches require a full rebuild. Document this decision in the README.

### 4.3 `LOW` — POSIX target shares `contentdirectory.cpp` and `ipwhitelist.cpp` with Windows

This is intentional (noted in `CMakeLists.txt`) and works. However `contentdirectory.cpp` includes `netutils.h` which has a `#ifdef _WIN32` block using `winsock2.h`. On POSIX this falls through to the `sys/socket.h` path correctly, but the `SOCKADDR` alias defined there (`using SOCKADDR = sockaddr`) conflicts if any future Windows-only header is accidentally included.

---

## 5  Code Quality

| Area | Observation |
|---|---|
| Naming | Consistent `m_` prefix for members, `AppXxx` macros for singletons — clear and readable |
| Error handling | Network API return values checked in most places; `fread` return value not checked in `config.cpp` |
| Memory management | No dynamic allocation beyond STL containers; no raw `new`/`delete` except `WorkData` in the HTTP pool (properly paired) |
| Thread safety | Mutex guards on `MediaSources` are present but `m_running` races are real (see 1.5) |
| Comments | Sparse — key algorithms (prefix matching, endpoint scoring, UUID generation) have no explanation |
| Magic numbers | `8192`, `65536`, `4096`, `900000`, `1800` appear inline; prefer named constants |

---

## 6  Prioritised Fix List

| Priority | Issue | File |
|---|---|---|
| 1 | Wrap all `std::stoi`/`std::stoll` on network data in try/catch | `httpserver.cpp` |
| 2 | Make `m_running` `std::atomic<bool>` on Windows | `ssdp.h`, `httpserver.h` |
| 3 | Add `SO_RCVTIMEO` on accepted client sockets | `httpserver.cpp` |
| 4 | Read HTTP headers in a loop until `\r\n\r\n` found | `httpserver.cpp` |
| 5 | Fix SSDP MX delay to not block the listener thread | `ssdp.cpp` |
| 6 | Add depth limit and reparse-point skip to `ScanFolder` | `media_sources.cpp` |
| 7 | Index children by parent ID in `MediaSources` | `media_sources.cpp` |
| 8 | Deduplicate `TrimAscii` / `ToLowerAscii` / `FindHeaderValueCaseInsensitive` | `netutils.cpp` |
| 9 | Add `WSACleanup` in `Server` destructor | `server.cpp` |
| 10 | Add GitHub Actions CI workflow | repo root |

---

*Review produced from static analysis of commit `main` branch as of May 2025.*
