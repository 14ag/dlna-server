# Threading and Concurrency

## Config (`Config`, `config.h`/`.cpp`, `posix_config.cpp`)

- Guarded by a `std::shared_mutex`. `Snapshot()` takes a shared (read) lock and returns a `ConfigSnapshot` by value — callers get an immutable, self-consistent copy instead of holding a lock while doing unrelated work.
- Any write to a `Config` field that can happen after `Server::Start()` — i.e., after the background scan thread and HTTP worker threads exist — must go through `Config::Mutate(fn)`, which takes the same lock `Snapshot()`/`Load()`/`Save()` use. Writing a public field directly from another thread at that point is a data race; the class comment in `config.h` states this explicitly. One-time command-line parsing before `Server::Start()` may write fields directly, since no other thread exists yet to race with it.

## MediaSources (`media_sources.h`/`.cpp`, `posix_media_sources.cpp`)

- Guarded by a plain `std::mutex` (`m_mutex`) protecting `m_items`, `m_idToIndex`, `m_childrenByParent`.
- Scans build an entirely separate `MediaIndexState` with no shared references into the live state, then swap it in under the lock in one call (`SwapScannedState`) — the lock is held only for the `std::move` assignments, not for the scan itself, which can take arbitrarily long against a slow FTP source.
- `GetChildren`, `TryGetChildren`, `GetDescendants`, `GetItem`, `GetChildCounts` all copy data out under the lock rather than returning references, so callers never hold the lock past the call.
- Only one scan thread runs at a time — `StartBackgroundScan()` joins any previous scan thread (via a moved-out `std::thread` handle, joined outside `m_scanMutex`) before starting a new one.

## HTTP server threading

- **Windows**: `PTP_POOL` thread pool (4–64 threads), one `PTP_WORK` item per connection. `HttpServer::Stop()` calls `CloseThreadpoolCleanupGroupMembers` before closing the pool, which blocks until in-flight work finishes.
- **POSIX**: one accept thread per listen socket (IPv4, IPv6), one detached-but-tracked `std::thread` per connection, capped at `kMaxClientThreads` (64). `m_clientThreads` is guarded by `m_clientMutex`; finished threads are reaped opportunistically from the accept loop rather than via a dedicated reaper thread, avoiding a third kind of thread just for cleanup bookkeeping. `Stop()` joins every remaining client thread before returning.
- Both sides check `m_running.load()` inside long-running send loops (file/remote streaming) so a `Stop()` call interrupts an in-progress transfer rather than waiting for it to finish naturally.

## SSDP threading

- One receive/notify thread (`select()`-based, 1-second timeout so it can observe `m_running` going false promptly) plus one delayed-response worker thread (`ResponseWorker`).
- Delayed `M-SEARCH` responses are queued under `m_responseMutex` and delivered by the response worker using a condition variable wait keyed to the earliest `dueAt` timestamp across all queued responses — not a fixed poll interval, so response latency tracks the computed jitter delay exactly rather than being rounded up to the next poll tick.
- All outbound sends (`sendto`) go through `m_socketMutex`, since interface selection (`IP_MULTICAST_IF`/`IP_UNICAST_IF`) is a per-socket, not per-send, property — two threads calling `sendto` with different target interfaces at the same time would race on which interface actually gets used.

## Server lifecycle (`Server`, `server.cpp`/`posix_server.cpp`)

- `m_running` and `m_stopping` are separate atomics. `m_stopping` is set before any teardown begins, and `ShouldStartScan()` checks both — this stops a rescan triggered by the watch loop from starting a new background scan while `Stop()` is already tearing things down.
- The watch loop (`WatchLoop`, `source_watcher.cpp`) polls every 5 seconds and computes an FNV-1a hash over source metadata (path, mtime, size for local sources; existence for remote ones — see `docs/KNOWN-ISSUES.md` for the time-of-check/time-of-use gap between this check and `Rescan()`'s actual read). A hash change triggers `StartBackgroundScan()`.
- `Server::Stop()` order: stop watch loop → stop SSDP → stop HTTP server → join background scan → clear endpoint state. SSDP is stopped before the HTTP server so a client that gets a late SSDP response doesn't attempt to fetch `/description.xml` from a socket that's already gone.

## GENA notify delivery (`UpnpEventManager`, `upnp_eventing.cpp`)

- One worker thread drains a `std::deque<NotifyJob>` under `m_mutex`/`m_cv`. Each job carries a `generation` counter; `ClearSubscriptions()` (called on `HttpServer::Stop()`) increments the generation and clears the queue, and the worker discards any job whose generation doesn't match current — this prevents an in-flight HTTP callback to a stale subscriber from being sent after the server has logically restarted eventing.
- Queue is bounded (`kMaxQueuedNotifyJobs = 256`); at capacity, the oldest job is dropped rather than blocking the caller (`NotifySystemUpdateId`, called from the scan-completion path).

## General patterns worth knowing before touching this code

- **Snapshot, don't lock-and-read**: every shared-state read (config, media tree, endpoint list) copies out under a lock and returns by value. If you're adding a new accessor, follow this pattern rather than returning a reference or iterator into locked state.
- **Build off to the side, swap under lock**: both `Config::Load()` (single-pass parse before taking the write lock at final assignment) and `MediaSources::Scan()` follow this. Avoid holding a lock across I/O.
- **Separate the "is a teardown in progress" flag from the "is it currently torn down" flag** where a background loop needs to distinguish "don't start new work" from "already stopped" — see `m_running`/`m_stopping` in `Server`.
