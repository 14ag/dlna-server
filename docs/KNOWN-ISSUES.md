# Known Issues and Scoped-Out Findings

Status as tracked against the `review` branch. Update this file as items are resolved or reprioritized — it's meant to reflect current state, not a historical log (see `CHANGELOG.md` for that once one exists).

## Resolved

**Nested HLS playlists returned an empty container.** `.m3u8` was absent from the `kFormats` extension table (`dlna_utils.cpp`), which caused `AddMediaFile` to silently drop every nested HLS manifest reference instead of erroring or logging. Fixed by routing playlist files through `FetchPlaylistOnce()` and `IsHlsManifestText()` before extension lookup ever runs, and by keeping `.m3u8` out of `kFormats` intentionally — see `docs/MEDIA-SCANNING.md`. The double-fetch pattern that used to exist (one fetch to classify, a second to parse) was also removed in favor of the current fetch-once contract.

## Active

**SSDP detection regression on `review`.** The server fails to appear on DLNA/UPnP clients after startup in some configurations. Areas under investigation, per the six-task workflow tracked separately:

1. Multicast join failure on specific adapter configurations
2. Interface name/address confusion — passing an adapter GUID or friendly name where a resolved IPv4 address or numeric interface index is required (the `UpnpInit2`-class bug; see `docs/SSDP-DISCOVERY.md` for how this codebase resolves interfaces)
3. Port conflicts on 1900/UDP with other UPnP stacks on the same host
4. Firewall rule gaps (see the port-conflict item below — the same rule-detection logic underpins the TCP side)
5. Incorrect `LOCATION` address selection on multi-adapter machines — see `SelectBestEndpoint` in `docs/SSDP-DISCOVERY.md`

Relevant files: `src/ssdp.cpp`, `src/netutils.cpp` (Windows); `src/posix_ssdp.cpp`, `src/posix_netutils.cpp` (POSIX); jitter/timing helpers in `src/dlna_utils.cpp`.

**HTTP port conflict / smoke test failure.** Root cause identified: `CreateListenSocket` in `src/httpserver.cpp` previously set `SO_REUSEADDR` without `SO_EXCLUSIVEADDRUSE` on Windows, which allows a second process to silently bind over an already-held port with no defined winner for incoming connections (see `docs/HTTP-SERVER.md`). `src/posix_httpserver.cpp` is explicitly excluded from this fix — POSIX `SO_REUSEADDR` doesn't share the Windows hijack behavior, so applying the same change there would be a no-op at best. Tracked separately: a regression test (`tests/test_httpserver_listen_socket_hardening.py`), PowerShell smoke-test hardening, and end-to-end re-verification.

**A missing closing brace in `posix_httpserver.cpp`** previously made `/subtitle/{id}` and `/albumart/{id}` permanently unreachable and caused `HEAD` requests to send two responses on the same socket. Verify structurally (a brace-depth-tracking script is more reliable than visual inspection in a file this size) before assuming the current tree is clean.

## Scoped out — pending product decisions

**Unreachable top-level sources still appear as empty folders.** Nested playlist fetch failures are correctly recorded as scan errors rather than rendered as empty containers (see `docs/MEDIA-SCANNING.md`), but a top-level `MediaSources` entry that's unreachable at scan time (network share down, FTP host unreachable) still shows up as a container with zero children — indistinguishable, from a renderer's point of view, from a source that scanned successfully and genuinely has nothing in it. Needs a product decision on how to surface source-level failures (hide the container entirely? label it? both?) before implementation.

**Time-of-check/time-of-use race in `Rescan()`/`Start()`.** The watch loop's change-detection hash (`ComputeMediaSourceSignature` in `source_watcher.cpp`) is computed against filesystem state at poll time; the actual scan that follows reads the filesystem again, separately. Between the two, a source can change again without triggering a second detected change until the next 5-second poll. Not correctness-critical for typical local-folder use (the next poll catches it), but worth a decision on whether to reduce the poll interval, hash within the scan itself, or accept the current bound.

## Non-blocking findings tracked separately

**HLS proxy header timing.** `CURLOPT_HEADERFUNCTION` (`network_sources.cpp`, `CurlHeaderStream`) fires on intermediate redirect responses as well as the final response, which can commit response headers to the client prematurely if a redirect chain is involved. Documented as non-blocking; not yet scheduled.

**`spoofSamsung` 1 GiB `Content-Length` placeholder is not a bug.** When a remote item's size is unknown and the requesting `User-Agent` is empty or starts with `SEC_HHP_`, `/media/{id}` sends a 1,073,741,824-byte placeholder `Content-Length` instead of omitting the header (`src/httpserver.cpp`, `src/posix_httpserver.cpp`). This is an intentional, preserved compatibility behavior for Samsung DLNA clients, which refuse to play a stream with no `Content-Length` at all. Do not "fix" this by removing the placeholder without a compatibility regression check against real Samsung hardware.

## Duplicated logic flagged for consolidation

`ItemProtocolInfo`/HLS content-features construction is duplicated across `src/contentdirectory.cpp`, `src/httpserver.cpp`, and `src/posix_httpserver.cpp` (`DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=...` appears as a literal string in more than one place). Per the project's no-redundant-code standard, this is a candidate for extraction into a single shared helper in `dlna_utils.cpp` (a `BuildHlsProtocolInfo()`/`BuildHlsContentFeatures()` pair already exists there — the remaining call sites that still inline the literal should be pointed at it instead).
