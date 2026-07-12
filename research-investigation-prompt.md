# DLNA Server SSDP Scan Blocking Investigation - Research Request

## Executive Summary

A DLNA server implementation has a critical bug where the background media scan is never launched when the server starts. The scan should be triggered immediately after SSDP initialization completes, but it is blocked. This causes HLS media items to be unavailable during Browse operations, breaking black-box tests.

## Problem Statement

**Observed Behavior:**
- Tests time out waiting for HLS media items to appear
- Initial scan is never launched after `SSDP::Start()` returns
- HTTP server starts and responds to SOAP requests, but `Browse` returns empty container (only the playlist.m3u8 container appears, no child items)

**Expected Behavior:**
- After `SSDP::Start()` returns, `Server::Start()` should call `StartBackgroundScan()`
- Scan should populate media items into the container
- HLS items should be discoverable via `Browse` operation

## Architecture Overview

### Component Flow
```
main() -> Server::Start() -> HTTP server starts -> SSDP::Start() -> StartBackgroundScan()
```

**SSDP::Start()** (in `src/ssdp.cpp`):
1. Creates IPv4 and IPv6 UDP sockets
2. Binds to SSDP port 1900
3. Joins multicast groups on all network endpoints
4. Sets `m_running.store(true)` (atomic flag)
5. Creates worker thread via `CreateThread()` (Windows API)
6. Creates response thread via `std::thread(&SSDP::ResponseWorker)`
7. Returns `true`

**Server::Start()** (in `src/server.cpp`):
1. Resets media for rescan
2. Sets `m_initialScanComplete` and `m_initialScanInProgress` flags
3. Starts HTTP server (succeeds)
4. Calls `SSDP::Get().Start()` (should return quickly)
5. Sets `m_running` flag
6. Calls `StartBackgroundScan()`
7. Detaches thread to `JoinBackgroundScan()` and `StartWatchMode()`

### Thread Synchronization
- `m_running` is `std::atomic<bool>` - acts as singleton guard in SSDP
- `m_initialScanComplete` and `m_initialScanInProgress` are `std::atomic<bool>` flags
- The detached thread at line 243-247 handles scan completion and watch mode

## Evidence Collected

### 1. Test Failure Evidence
```
AssertionError: No HLS media item found via Browse
```
The Browse response shows:
```xml
<container id="1000000" parentID="0" childCount="1">
  <dc:title>playlist.m3u8</dc:title>
  <upnp:class>object.container.storageFolder</upnp:class>
</container>
```
**Key observation:** `childCount="1"` but no items are visible. This suggests either the container is not fully populated or the scan never ran.

### 2. Logging Evidence
The `debug.log` file (generated because `DebugLog=1` in config.ini) shows:
```
SSDP IPv4 multicast join ok: if=XX addr=YYY
SSDP IPv6 multicast join ok: if=XX addr=YYY
```
**But there is NO output from LogPrint statements I added after `m_running.store(true)` in SSDP::Start()** including:
- `[diag:ssdp] After CreateThread`
- `[diag:ssdp] After std::thread(&SSDP::ResponseWorker)`
- `[diag:server] After SSDP::Get().Start returned`
- `[diag:server] After m_running.store - about to call StartBackgroundScan`

### 3. Binary Inspection
- Built binary exists at `build_winx64\Debug\DLNA Server.exe` (5.8 MB, modified 13/07/2026 01:57:14)
- Searched for string constants like `diag:ssdp` and `Scan entered` in binary - **NOT FOUND**
- MSVC optimization may strip unreferenced string constants

### 4. Process Investigation
- Checked for zombie processes on UDP port 1900 via `Get-NetUDPEndpoint` - svchost processes found but not DLNA Server.exe
- No zombie DLNA Server.exe processes holding the port

## Diagnostic Attempts

### Attempt 1: LogPrint Diagnostics in SSDP
Added LogPrint calls at critical points in `SSDP::Start()`:
```cpp
m_running.store(true);
LogPrint(L"[diag:ssdp] After m_running.store(true)");
m_hThread = CreateThread(...);
LogPrint(L"[diag:ssdp] After CreateThread");
m_responseThread = std::thread(&SSDP::ResponseWorker, this);
LogPrint(L"[diag:ssdp] After std::thread(&SSDP::ResponseWorker)");
// ... async burst thread ...
LogPrint(L"[diag:ssdp] SSDP::Start about to return true");
```

**Result:** Multicast join logs appear, but ALL post-`m_running.store(true)` LogPrint statements are missing from log.

### Attempt 2: File Write Diagnostics in SSDP
Added forced file write to bypass potential MSVC string optimization:
```cpp
m_running.store(true);
{
    FILE* fp = _wfopen(L"diag_mrunning.txt", L"a");
    if (fp) { fwprintf(fp, L"m_running.store(true) executed\n"); fclose(fp); }
}
```

**Result:** File `diag_mrunning.txt` was NOT created in `build_winx64\Debug\diag_mrunning.txt`

### Attempt 3: LogPrint Diagnostics in Server
Added LogPrint in `Server::Start()`:
```cpp
if (!SSDP::Get().Start(...)) {
    LogPrint(L"Failed to start SSDP.");
    // ...
}
LogPrint(L"[diag:server] After SSDP::Get().Start returned - about to store m_running");
m_running.store(true, ...);
LogPrint(L"[diag:server] After m_running.store - about to call StartBackgroundScan");
```

**Result:** LogPrint calls after `SSDP::Start()` also NOT in log file.

## Hypotheses Analyzed

### Hypothesis A: `m_running.load()` Early Return in SSDP
**Location:** `src/ssdp.cpp` line 94:
```cpp
if (m_running.load()) return true;
```

**Rationale:** If SSDP singleton was already running from a previous test, this would return immediately without executing the scan launch code path.

**Evidence against:** Multicast join logs appear in debug.log, which only happens in the body of SSDP::Start(), BEFORE `m_running.store(true)` at line 198. This means the early return is NOT happening.

### Hypothesis B: Binary Not Being Rebuilt or Stale Binary Used
**Rationale:** Tests might be using an old binary that doesn't contain my diagnostic changes.

**Evidence for investigation needed:** 
- Confirm SHA256 hash of `build_winx64\Debug\DLNA Server.exe` before and after rebuild
- Verify test fixture `dlna_binary` resolves the correct path
- Check if there are other build outputs (e.g., in `output/` directory) that could be used instead

**What I need to verify:**
1. Does the `dlna_binary` fixture in `tests/conftest.py` correctly resolve to the rebuilt binary?
2. Is there a stale binary elsewhere with a different path?
3. Are the MSVC build artifacts in the expected location?

### Hypothesis C: `CreateThread` or `std::thread` Blocking or Throwing
**Rationale:** Thread creation could hang or throw an exception that terminates the process.

**Evidence against:** 
- HTTP server continues to respond to requests (test progress prints show this)
- SSDP multicast burst runs asynchronously (notifications are sent)
- No process crash is observed

## Specific Questions for Research

### 1. Windows MSVC Debug Build Optimization
**Question:** How does MSVC handle string constants in Debug builds, particularly for wide string (`L""`) formatted output in `LogPrint` calls? Are they stripped by the optimizer even in Debug configuration?

**Why I need this:** I suspect my diagnostic LogPrint strings are being optimized away, but I need to understand the exact mechanism to add diagnostics that will actually appear.

**What to search for:**
- MSVC `_vsnwprintf_s` optimization behavior
- Debug build `/Od` vs `/O2` flag effects on string literals
- Whether `LogPrint` macro/function could cause strings to be inlined away

### 2. Windows Thread Creation Failure Modes
**Question:** What are the specific error conditions and return values for `CreateThread` and `std::thread` constructors on Windows? Under what conditions would thread creation fail silently or cause the calling thread to block?

**Why I need this:** I need to understand if SSDP thread creation could block the calling thread in a way that wouldn't crash the process but would prevent forward progress.

**What to search for:**
- `CreateThread` return value on failure (returns NULL, GetLastError codes)
- `std::thread` constructor exception behavior
- Windows thread stack size limits (`STACK_SIZE_PARAM_IS_A_RESERVATION`)

### 3. Windows UDP Socket Multicast Join Blocking Conditions
**Question:** Under what network conditions can `setsockopt(IP_ADD_MEMBERSHIP)` or `setsockopt(IPV6_ADD_MEMBERSHIP)` block indefinitely or fail in ways that wouldn't be logged?

**Why I need this:** The multicast joins succeed (logs show this), but I need to understand edge cases where the join succeeds but subsequent operations hang.

**What to search for:**
- Windows multicast join blocking in test environments
- Network interface enumeration returning ghost/administrative interfaces
- IGMP/MLD join race conditions on localhost

### 4. Process Working Directory and File I/O in Windows Services
**Question:** When a process is launched via `subprocess.Popen()` in Python without specifying `cwd`, what is the working directory? How does this affect relative file paths like `diag_mrunning.txt`?

**Why I need this:** My file write diagnostic may have been writing to the wrong directory.

**What to search for:**
- Python subprocess working directory inheritance on Windows
- Windows GetCurrentDirectory() behavior for spawned processes
- How the test fixture `_launch_server` sets up the environment

## Exact Information Needed

### For each question above:
1. **Primary source documentation** (Microsoft docs, cppreference, etc.)
2. **Reproduction code snippets** for the failure modes
3. **Debugging techniques** to verify which path is taken at runtime
4. **Alternative diagnostic approaches** that cannot be optimized away

### Specifically:
- MSVC documentation on Debug build string constant handling
- Windows SDK documentation on `CreateThread` failure modes
- Windows socket multicast behavior in isolated/test environments
- How to force a string constant to remain in a Debug build (volatile reference? inline assembly? specific #pragma?)

## Additional Context

- Platform: Windows 11, MSVC 19.x
- Build command: `cmake -S . -B build_winx64 -A x64 -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static && cmake --build build_winx64 --config Debug`
- Test framework: pytest with Python 3.x
- The codebase uses C++17 and Windows-specific APIs (WinSock2, CreateThread)

## What I Will Do With This Information

1. Add diagnostics that are guaranteed to appear in logs
2. Implement thread creation error handling if needed
3. Fix the actual bug causing the scan to not launch
4. Verify fix with the failing tests:
   - `test_hls_manifest_proxy.py::TestHlsServedManifestHasAbsoluteUris::test_served_manifest_has_absolute_uris`
   - `test_hls_manifest_proxy.py::TestHlsFetchFailureReturns502::test_hls_fetch_failure_returns_502_not_a_hang`