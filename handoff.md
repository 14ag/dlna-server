# Handoff: HLS DLNA/VLC Verification

## Current Goal

Make the PC DLNA server output indistinguishable HLS data from the working Android DLNA server path, then prove VLC can access nested HLS playlist content and play the live video feed.

The user is strict on this point: both sides must expose identical playable data. Shape-only parity is not enough.

## Repo Rules To Keep

- Always use caveman skill in this repo.
- Use `curl.exe` for HTTP traversal evidence.
- Use `git` for git tasks and `gh` for GitHub tasks.
- Do not read code unless needed for implementation/debug.
- Do not revert unrelated user changes.
- If a required command/dependency is missing, verify via PowerShell first.
- Final verification should include tests and real output checks.

## What Was Done

### Diagnosis Artifacts

Several report files were created during diagnosis:

- `hls-playlist-findings.txt`
- `hls_access_comparison_report.txt`
- `android-vs-pc-dlna-structure-comparison.txt`
- `hls-side-by-side-comparison.txt`

The key latest artifact is `hls-side-by-side-comparison.txt`. It records the final direct-vs-server resolved URL, byte count, SHA256, and child playlist parity.

### Code Fix

Changed `src/contentdirectory.cpp` in `BuildDIDL`.

Before:

```cpp
bool isHls = (it.mimeType == L"application/vnd.apple.mpegurl");
const bool exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !cfg.proxyStreams && !ShouldProxyRemoteUrl(it.path) && !isHls;
```

After:

```cpp
const bool exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !cfg.proxyStreams && !ShouldProxyRemoteUrl(it.path);
```

Reason:

- The server was rewriting remote HLS resources to `http://localhost:8200/media/<id>`.
- That made the HLS master produce localhost child URLs like `http://localhost:8200/media/playlist_1280x720.m3u8`.
- Those child routes returned `400`, so nested HLS playback failed.
- Android advertises the remote CDN master URL directly in the `res` element.
- PC server now also advertises the same CDN master URL directly when proxying is off and the URL is safe to expose.

### Smoke Test Fix

`tests/verify-hls-structure-smoke.ps1` default local wrapper path changed:

```powershell
[string]$LocalWrapperPath = '.\test media\test-hls-playlist.m3u8'
```

This matches running the script from `tests\`.

### Build/Runtime State

Built fresh Windows release target:

```powershell
cmake --build build-release-winx64 --config Release --target dlna-server
```

Copied fresh binary into:

```text
output\winx64\DLNA Server.exe
```

Restarted headless server from `output\winx64`, so it used:

```text
output\winx64\config.ini
```

That config points to:

```text
C:\Users\philip\sauce\dlna-server\tests\test media\test-hls-playlist.m3u8
```

## Verified Passing

Unit/source-contract tests:

```powershell
python -m pytest -q --tb=no tests/test_hls_manifest_proxy.py tests/test_hls_protocolinfo_and_scanfolder_fixes.py
```

Result:

```text
22 passed
```

HLS structure smoke test:

```powershell
cd C:\Users\philip\sauce\dlna-server\tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\verify-hls-structure-smoke.ps1
```

Result:

```text
All checks passed.
```

Important smoke test output:

- Server resource resolves to `https://aegis-cloudfront-1.tubi.video/ec903a48-3638-4d0b-ac89-813e147bca58/playlist.m3u8`
- Master tag structure identical.
- Master child URI set identical.
- All 5 child playlists identical by structure.
- Each child has 10 segments.
- Naive route `http://localhost:8200/media/playlist_1280x720.m3u8` still rejects with `400`, which is expected because clients should no longer construct that URL.

## Android Control Server Discovery

The Android server at `192.168.100.33` has two relevant ports:

- `18192`: presentation/file-browser HTTP page. It returns HTML at `/`, but rejects `/description.xml`, `/media/*`, and guessed control paths with `403`.
- `38520`: real UPnP/DLNA control server discovered by SSDP.

SSDP on Wi-Fi interface `192.168.100.163` found Android descriptor:

```text
Location: http://192.168.100.33:38520/description.xml
friendlyName: 14ag
controlURL: /service/ContentDirectory_control
presentationURL: http://192.168.100.33:18192/
```

Android ContentDirectory traversal:

- Root ObjectID `0` contains container `p`, title `Playlists`.
- Container `p` contains container `p$0`, title `test-hls-playlist`.
- Container `p$0` contains one item:

```text
title: ABC 13 Asheville NC (WLOS) (1080p)
class: object.item.videoItem
protocolInfo: http-get:*:video/mpegurl:DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000
res: https://aegis-cloudfront-1.tubi.video/ec903a48-3638-4d0b-ac89-813e147bca58/playlist.m3u8
```

The PC server now advertises the same `res` URL, but with MIME `application/vnd.apple.mpegurl`.

## Current Setback

The user asked to prove VLC playback:

1. Start with Android server as control.
2. Use that result as baseline.
3. Test PC server.

Work was interrupted after Android ContentDirectory traversal found the playable `res` URL and before VLC was launched and logs were captured.

No VLC playback verification has been completed yet.

## Next Actions

1. Confirm VLC exists:

```powershell
powershell Get-Command vlc.exe -ErrorAction SilentlyContinue
```

Known local install found earlier:

```text
C:\Program Files\VideoLAN\VLC\vlc.exe
```

2. Android control playback test:

Use Android-advertised playable resource:

```text
https://aegis-cloudfront-1.tubi.video/ec903a48-3638-4d0b-ac89-813e147bca58/playlist.m3u8
```

Run VLC in dummy mode with logs, enough time to fetch master, child playlist, and at least one segment:

```powershell
$vlc = 'C:\Program Files\VideoLAN\VLC\vlc.exe'
$url = 'https://aegis-cloudfront-1.tubi.video/ec903a48-3638-4d0b-ac89-813e147bca58/playlist.m3u8'
$log = 'vlc-android-control.log'
& $vlc -I dummy --play-and-exit --run-time=20 --verbose=2 --file-logging --logfile=$log $url vlc://quit
```

Check log for successful HLS demux and segment fetch. Search terms:

```powershell
rg -n "hls|m3u8|ts|playing|error|failed|HTTP|segment" vlc-android-control.log
```

3. PC DLNA playback test:

Use VLC against the PC server via UPnP if automation can browse VLC UPnP, or use the PC ContentDirectory-advertised `res` URL if testing equivalent playable data is acceptable. The user wants VLC access to nested HLS content; safest evidence is:

- ContentDirectory Browse from PC returns the same CDN `res`.
- VLC plays that `res`.
- VLC logs show nested child playlist and `.ts` segment access.

PC ContentDirectory item route:

```text
http://localhost:8200/upnp/control/content_directory
ObjectID 1000008 -> item 1000009 -> res CDN master URL
```

Run same VLC command with the PC-advertised `res` URL.

4. If user requires VLC to browse PC DLNA itself, use VLC UI/CLI UPnP discovery or a playlist file generated from ContentDirectory result. Capture the exact route VLC opened. Do not claim DLNA-browse playback unless logs prove VLC used the DLNA item.

5. Update/create a VLC verification report:

Suggested file:

```text
hls-vlc-playback-verification.txt
```

Include:

- Android descriptor/control URL.
- Android item path and `res`.
- Android VLC log summary.
- PC descriptor/control URL.
- PC item path and `res`.
- PC VLC log summary.
- Segment fetch evidence.
- Exact verdict: identical playable URL and VLC playback result.

## Suggested Skills

- `caveman`: required by repo rules; keep user-facing output terse.
- `playwright` only if VLC UI needs browser-like automation is not useful; likely not needed.
- `review` if asked to review the final diff before commit.
- `technical-writer` if asked to turn VLC findings into release notes or a PR summary.

## Cautions

- Do not rely on `192.168.100.33:18192` for UPnP control. It is presentation/file-browser surface.
- Use `192.168.100.33:38520` from SSDP for Android descriptor/control.
- Android control port can change across restarts; rediscover via SSDP rather than hardcoding if it fails.
- HLS is live. Exact segment filenames and hashes can change between fetches. For direct-vs-server identity, compare resolved URL identity and same-fetch body/hashes where possible.
- Current PC server in `output\winx64` has been restarted with the new binary. If process changed, rebuild/copy/restart again.
