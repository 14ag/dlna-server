# Nested HLS Playlist Hosting: Android Reference Reverse-Engineering Insights

**Date:** 07-7-26
**Scope:** Strictly limited to how `github.com/spacebar365/temporary` (decompiled Android DLNA server APK, package `com.al.dlnaserver`, JADX output) implemented hosting of nested HLS playlists, and what that implementation implies for the `review` branch of `github.com/14ag/dlna-server`.
**Method:** `github-raw-reader` sparse-clone (Method B) against the `main` branch of `spacebar365/temporary`, followed by a `feature-spec`-style extraction (source resolution, input/output/process catalog, assumptions, open questions).

---

## 1. Source Resolution and Feature Boundary

Recovered files relevant to nested HLS/playlist hosting, located via keyword search (`m3u8`, `playlist`, `EXT-X`, `EXTINF`) across all 3,107 `.java` files in the decompiled tree:

| File | Class | Role |
|---|---|---|
| `com/al/dlnaserver/a/a.java` | `M3UParser` (obfuscated name `a`) | Parses `.m3u` / `.m3u8` text into two parallel string arrays (URLs, titles) |
| `com/al/dlnaserver/a/a$a.java` | Parser result holder | Wraps the two arrays with `a()`=count, `a(i)`=url, `b(i)`=title |
| `com/al/dlnaserver/a/b.java`, `b$a.java`, `b$a$a.java` | `.pls` parser | Same shape as the M3U parser, for `.pls` files |
| `com/al/dlnaserver/b/c.java` | `FileUtils` | Extension classification only (`d = {"m3u","m3u8","pls"}`); no playlist parsing |
| `com/al/dlnaserver/servers/DlnaMediaServer.java` | Main UPnP `ContentDirectory` populator, built on CyberLink for Java (`org.cybergarage.*`) | Calls the parsers, builds `<container>`/`<item>` DIDL entries per playlist line |
| `com/al/dlnaserver/servers/j.java` | `HttpFileServer` (name recovered from its own log tag) | Serves both local files and proxied remote/`m3u8` streams over HTTP, sets `contentFeatures.dlna.org` |

Feature boundary: the pair `M3UParser.a(InputStream)` + `DlnaMediaServer.a(List<String>)`/`DlnaMediaServer.a(container, url, title, ...)` for playlist-to-DIDL conversion, plus `HttpFileServer.a(...)` for the HTTP delivery/headers side. This is the smallest cohesive unit that reproduces "nested HLS playlist hosting."

---

## 2. Extraction

### 2.1 Input catalog

- Raw bytes of a `.m3u`/`.m3u8`/`.pls` file, from either a `FileInputStream` (local path) or `com.al.dlnaserver.b.l.a(str)` (an HTTP URL opener, used when the path starts with `"http"`)
- User settings read per playlist import: `proxy_stream`, `show_thumbs`, `show_thumbs_image`, `show_thumbs_audio` (`SharedPreferences` booleans via `com.al.dlnaserver.b.n.a`)
- A running counter `App.n` used as the numeric UPnP object-ID namespace for each playlist processed
- HTTP request headers on playback (`icy-metadata`) when a client later fetches a URL that resolved to a remote stream

### 2.2 Output catalog

- One UPnP `<container>` DIDL node per playlist (`p$<App.n>`, parent `p`)
- Zero or more `<item>` DIDL child nodes, one per parsed playlist line, each built by the (unrecoverable, see 2.4) per-entry method
- An HTTP response for each streamed URL, carrying:
  - `contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000` -- identical for every resource type
  - `Server: DLNADOC/1.50 UPnP/1.0, Cybergarage/2.1.2, NanoHTTPD/2.3.1, DLNAServer/1, Android/<release>`
  - `Accept-Ranges: bytes` or `none` depending only on whether an ETag-adjacent `str` argument was non-null, not on the resource's actual seekability
  - icy-* headers passed through from the upstream connection when present

### 2.3 Process map (plain language, no reproduced code)

1. `FileUtils` classifies a source path as a playlist purely by trailing extension string match (`m3u`, `m3u8`, `pls`); there is no content sniffing at this stage.
2. `M3UParser.a` reads the whole stream into one string, strips only the literal `#EXTM3U` header line, splits on newline, and for every remaining line:
   - if the line contains the substring `#EXTINF`, it strips the `#EXTINF:<duration>,` prefix and appends the remainder to a title-accumulator string,
   - otherwise, if the line does not start with `#`, it appends the line verbatim to a URL-accumulator string,
   - both accumulators use the literal token `####` as a separator.
   The two accumulators are split back into arrays at the end and returned as parallel arrays with no cross-validation of length.
3. `DlnaMediaServer.a(List<String>)` iterates every accepted playlist path, opens it, dispatches to the M3U or PLS parser by extension, and for every index `i` up to the URL-array length it calls a private overload `a(container, url, title, proxySetting, thumbSettings..., generation, index)` to materialize one DIDL entry.
4. That per-entry overload is the one method that would decide, for each URL, things like protocol info, MIME type, and whether the entry is itself a nested playlist/manifest requiring recursive expansion. **JADX could not decompile this method's body** ("Method not decompiled ... Code decompiled incorrectly"); only its signature survives.
5. Independently, `HttpFileServer` contains one dead statement, `str3.endsWith("m3u8");`, whose boolean result is computed and discarded -- it does not feed any `if`, ternary, loop condition, or field write anywhere in the surrounding method. The only observable effect this class has on m3u8-derived streams is the generic remote-proxy path: open a `URLConnection`, forward `icy-*` headers, and hand the InputStream to the same NanoHTTPD response constructor used for a live radio stream, then stamp the same static `contentFeatures.dlna.org` string described in 2.2 regardless of resource type or known length.

### 2.4 Assumptions and unrecoverable regions (recorded per feature-spec Phase 2/4)

- **Unrecoverable:** `DlnaMediaServer.a(org.cybergarage.d.e.a.a.b.a.a, String, String, boolean, boolean, boolean, boolean, int, int)` -- the method that turns one playlist line into a DIDL `<item>` (and would be the natural place to special-case a nested HLS manifest vs. a plain media URL) is not present in decompiled form. Any claim about master/media-playlist detection, per-item protocol info, or recursive manifest expansion happening inside this method is **not verifiable from this source** and is not asserted here.
- **Assumption:** `com.al.dlnaserver.b.l.a(str)` is an HTTP `InputStream` opener (name pattern and call site are consistent with this, but its own body was not pulled into this review since it is outside the stated feature boundary).
- No file in the repository references `#EXT-X-` (the RFC 8216 tag prefix that marks a Media or Master Playlist as HLS rather than a plain compilation playlist). This absence was confirmed by an exhaustive case-sensitive and case-insensitive search across all `.java` files, not just the six files listed above.

---

## 3. Findings: what the Android reference actually does with nested HLS

**Finding 1 -- No Master Playlist vs. Media Playlist distinction exists in the recoverable parser.**
Per RFC 8216 SS2 and SS4.3.4, a Playlist is either a Media Playlist (a list of Media Segments meant to be concatenated into one stream) or a Master Playlist (a list of variant Media Playlist URIs, introduced by `EXT-X-STREAM-INF`, meant for adaptive bitrate switching). `M3UParser.a` (SS2.3, step 2) treats every non-`#` line identically, whether that line is a genuine standalone media file, a Media Segment `.ts` chunk, or a variant `.m3u8` URI from a Master Playlist. There is no `EXT-X-STREAM-INF`/`EXT-X-TARGETDURATION` awareness anywhere in the recovered source. If a Master Playlist URL were ever handed to this parser, each variant stream URI inside it would be enumerated as if it were its own independent playable file.

**Finding 2 -- The one visible "is this HLS" check is dead code.**
`str3.endsWith("m3u8");` in `HttpFileServer` computes a value that is never used. This is consistent with either (a) a discarded branch that jadx could not reconstruct control flow for, or (b) the original developer starting to special-case `.m3u8` proxy responses (e.g. to avoid declaring a fixed `Content-Length`, or to switch buffering strategy) and never finishing the branch. Either way, the net observable behavior today is that a nested/remote `.m3u8` is proxied through the exact same code path as a plain internet radio stream, with the same static headers.

**Finding 3 -- `contentFeatures.dlna.org` is a hardcoded constant, not resource-derived.**
Every streamed resource -- local file, remote file, or `.m3u8` manifest -- receives the literal `DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000` string. Per the mature open-source implementations surveyed for this project (Serviio's forum documentation of its own `DLNA.ORG_OP` usage, and `anacrolix/dms`'s `dlna.go`, which documents the two `DLNA.ORG_OP` bits as `time-seek-range-supp` and `bytes-range-header-supp`), `OP=01` specifically asserts byte-range seeking is supported. Advertising this for a manifest-backed or otherwise non-byte-addressable stream is a known real-world failure mode: an MX Player user thread shows the identical header combination (`DLNA.ORG_OP=01;DLNA.ORG_CI=0`) alongside a server-guessed `Content-Length` on a proxied stream, and seeking simply did not work for that content. The Android reference app "handling HLS well" in the user's recollection is therefore more likely explained by client-side tolerance (many renderers ignore `contentFeatures.dlna.org` seek claims for `.m3u8`/`application/vnd.apple.mpegurl` MIME content and defer entirely to their own HLS player) than by anything protocol-correct happening server-side.

**Finding 4 -- Parallel-array design shares the exact fragility already found and documented in the `review` branch.**
`a$a` stores URLs and titles in two separately-built arrays whose lengths are assumed, never verified, to match. A dangling `#EXTINF` with no following URI, or a URI line with no preceding `#EXTINF`, shifts the two arrays out of alignment for every subsequent entry, and the consuming loop indexes both arrays by the same counter (bounded by the URL array's length only). This is the same class of defect independently identified and fixed for the C++ project's `.m3u`/`.m3u8` parser (dangling `#EXTINF` handling, per the existing `dlna-server-nested-hls-empty-fix-workflow-06-7-26.md`). The Android reference does not show a more defensive pattern here; it shows the same one.

---

## 4. Verification of the stated hypothesis about the `review` branch

You stated the current failure is that `ContentDirectory::ItemProtocolInfo` forces every HLS item through the proxy because `exposeRemoteDirect` is `false` "whenever `isHls`," and that the code "unconditionally advertises `DLNA.ORG_OP=01`."

**That is not correct as stated**, based on the `review`-branch source already in this conversation.

- Sources: `src/contentdirectory.cpp` (`ItemProtocolInfo`, `ShouldProxyRemoteUrl`, `BuildDIDL`), `src/dlna_utils.cpp` (`ProtocolTail`, `BuildProtocolInfoForExtension`), `src/media_sources.h` (`MediaItem` struct).
- States: `MediaItem` (in `src/media_sources.h`) has no `isHls` field or equivalent at all -- its members are `id, parentId, path, title, isFolder, mimeType, upnpClass, sizeBytes, subtitlePath, albumArtPath, albumArtMime`. `ItemProtocolInfo` reads: `return BuildProtocolInfoForExtension(SourceExtension(item.path), item.mimeType, item.sizeBytes > 0);` -- it branches only on file extension and on whether `sizeBytes > 0`, never on any HLS-related flag. `ShouldProxyRemoteUrl` branches only on URL scheme (`http`/`https` vs. other) and on whether the URL contains embedded credentials (an `@` before the path); it likewise never inspects HLS-ness. The proxy decision in `BuildDIDL` is `const bool exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !cfg.proxyStreams && !ShouldProxyRemoteUrl(it.path);` -- again, no HLS-specific term appears.
- On the `DLNA.ORG_OP` claim: `AddHlsStreamItem` (in `src/media_sources.cpp` / `src/posix_media_sources.cpp`) sets `hlsItem.sizeBytes = 0` for every HLS manifest item, with the comment explaining this is deliberate because "adaptive/live HLS manifests do not have a meaningful fixed byte length." `ProtocolTail` (in `src/dlna_utils.cpp`) computes `const bool seek = info.byteSeek && hasKnownSize;` and then emits `DLNA.ORG_OP=01` only if `seek` is true, `DLNA.ORG_OP=00` otherwise. Since `.m3u8` has no entry in the `kFormats` table, `BuildProtocolInfoForExtension` falls back to `info.byteSeek = hasKnownSize` (i.e. `false`, because `sizeBytes` is 0), so `seek` evaluates to `false` and the header rendered today is `DLNA.ORG_OP=00`, not `01`.

If the actual observed symptom is "the DLNA client won't seek in nested HLS content" or "playback stalls/fails," the root cause is not in the two functions named in the hypothesis. Worth checking instead, based on what this reverse-engineering pass surfaced as a real cross-implementation risk (Finding 3): whether any client-facing code path in your HTTP layer (`src/httpserver.cpp` / `src/posix_httpserver.cpp`) or elsewhere computes `DLNA.ORG_OP`/`contentFeatures.dlna.org` a second time from a different, non-`BuildContentFeaturesForExtension` code path for the `/media/<id>` streaming response -- that would be the only way a `01` could appear for an HLS item, and it would not be explained by anything in `contentdirectory.cpp`.

---

## 5. Recommendations (research-backed, insight level only -- not a task-workflow document)

1. **Detect Master vs. Media Playlist by content, not by extension**, per RFC 8216 SS4.3.4 (`EXT-X-STREAM-INF` marks a Master Playlist). This is exactly what `IsHlsManifestText` in `src/network_sources.cpp` already does by scanning for any `#EXT-X-` prefixed line -- which is a stronger, spec-correct test than anything recoverable from the Android reference. Keep it; the Android app's flat parser is not a model to imitate here.
2. **Never hardcode `DLNA.ORG_OP`/`DLNA.ORG_FLAGS` across resource types.** Serviio's own maintainer statement (forum thread cited above) is explicit: "transcoded streams support time based seek, native byte range seek" -- i.e. the flag must track the actual delivery mechanism per item, not be a single constant. `anacrolix/dms` models this directly as two independent booleans (`SupportTimeSeek`, `SupportRange`) computed per resource before the header string is assembled, which is a cleaner mature-project pattern than a single opaque literal.
3. **Treat sizeBytes==0 as authoritative for suppressing byte-range seek claims**, which your `ProtocolTail`/`AddHlsStreamItem` pairing already does correctly for HLS items specifically; the risk area is any second, independent place in the codebase that might construct `contentFeatures.dlna.org` without going through that same function (see SS4 above).
4. **Do not adopt the Android reference's array-pairing playlist model.** It has the same dangling-`EXTINF` fragility your project already found and fixed; treating parsed entries as a single list of `{location, title, subtitlePath}` records (as `network_sources.h`'s `PlaylistEntry` already does) avoids the class of bug entirely by construction.
5. **If proxying remote HLS is ever required for clients that cannot dereference the manifest URL directly**, note that proxying an M3U8 manifest through a generic byte-stream HTTP handler (as the Android reference's `HttpFileServer` does for all remote content, m3u8 included) does not make the manifest's segments dereferenceable by the client through the same proxy -- the client still needs to resolve relative segment URIs against the manifest's own base URI. Mature DLNA servers generally either serve the manifest directly with correct scheme/host (letting the client's own HLS-capable player pull segments), or do not attempt DLNA-level seeking metadata for HLS at all; none of the reference implementations checked here (Serviio, UniversalMediaServer, anacrolix/dms) attempt segment-level proxying with rewritten byte ranges for HLS specifically.

---

## 6. Cleanroom Firewall Notice

This document is a reverse-engineered specification and finding set derived from a public GitHub repository the requester identifies as their own historical Android application, decompiled via JADX after original source loss. No source code from `spacebar365/temporary` is reproduced verbatim anywhere above; all descriptions are process-level paraphrase, and any literal strings quoted (HTTP header values, log tag names, extension lists already public in the requester's own project) are data/protocol constants, not creative expression. Two regions of the original feature are explicitly marked unrecoverable in SS2.4 and must not be treated as verified in any downstream implementation decision.
