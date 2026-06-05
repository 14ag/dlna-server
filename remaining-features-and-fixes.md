# Remaining Features And Fixes

Delete this file only after every listed item is implemented, verified, and documented.

Sources reviewed:
- `../DLNAServer_2.14_APKPure`
- `../dlna-server-review.md`
- `../dlna-server-review-implementation-blueprint.md`
- workspace and repo Markdown files, including `README.md`, `CHANGELOG.md`, `docs/*.md`, `misc/*.md`, and read-only `planning.md`

Scope note: these are features or fixes not yet completed. Items that expand product scope need separate approval before implementation.

## Android-Derived Features Not Yet Implemented

- [ ] Generated thumbnail cache for video, image, and audio entries.
  - [ ] Add thumbnail generation pipeline.
  - [ ] Add thumbnail quality handling.
  - [ ] Avoid broken `albumArtURI` when thumbnail generation fails.
- [ ] Auto update / watch mode for media folders.
  - [ ] Detect local source changes without full manual restart.
  - [ ] Keep scan state stable while incremental updates run.
- [ ] WiFi/network automation equivalent.
  - [ ] Design desktop behavior for start/stop by network.
  - [ ] Support configured allowed network names or adapters only after approval.
- [ ] Power/charge automation equivalent.
  - [ ] Design desktop behavior for start/stop by AC power or sleep state.
  - [ ] Add clear logs when automation blocks serving.
- [ ] QR/share page for server URL.
  - [ ] Add web page or tray action that exposes server address.
  - [ ] Generate QR locally without external services.
- [ ] Android-style virtual category roots.
  - [ ] Add approved roots for videos, audios, images, files, folders, artists, and recently added.
  - [ ] Keep existing `DoNotShowAllMediaFolders` behavior respected.
- [ ] Removable/external source permission flow.
  - [ ] Define desktop equivalent for Android SAF-style persistent sources.
  - [ ] Preserve access failures as explicit logs, not silent empty folders.
- [ ] OS background guidance.
  - [ ] Add platform-specific diagnostics for firewall, sleep, background app limits, and autostart.
- [ ] Localization/language flow.
  - [ ] Decide whether PC app should expose language selection or only system locale.

## Review And Protocol Follow-Ups

- [ ] Async GENA `NOTIFY` updates.
  - [ ] Notify subscribed renderers when `SystemUpdateID` changes.
  - [ ] Keep notification work off request/scan hot paths.
- [ ] Persistent media database.
  - [ ] Store stable media IDs.
  - [ ] Cache scan errors and metadata.
  - [ ] Support resume/bookmark groundwork where renderer protocols allow it.
- [ ] Renderer compatibility profiles.
  - [ ] Detect device quirks from headers or description data.
  - [ ] Apply profile-specific MIME, DLNA flags, subtitle, and folder-limit behavior.
- [ ] Rich media metadata extraction.
  - [ ] Cache duration, bitrate, codec, dimensions, channels, and subtitle tracks.
  - [ ] Prefer ffprobe or equivalent only after dependency and packaging design.
- [ ] Optional FFmpeg remux/transcode path.
  - [ ] Add direct-stream decision logic.
  - [ ] Keep remux/transcode disabled unless explicitly configured.
- [ ] AVTransport / push playback / renderer remote control.
  - [ ] Add only after scope approval.
  - [ ] Separate control-plane tests from content browsing tests.
- [ ] Per-device permissions.
  - [ ] Add allow/block policy per renderer.
  - [ ] Add external access policy before exposing non-LAN use.
- [ ] Cross-device compatibility matrix.
  - [ ] Automate smoke checks for VLC Android, Android TV, Samsung/LG TVs, Kodi, and macOS clients where devices exist.
  - [ ] Record known quirks and required profile behavior.
- [ ] macOS release validation.
  - [ ] Verify Apple Silicon and Intel outputs.
  - [ ] Add notarization, signing, firewall, and first-run diagnostics when credentials/tooling exist.
- [ ] Full all-platform build verification.
  - [ ] Verify default build script path across `winx64`, `winx86`, `linux`, `macos-x64`, and `macos-arm64`.
  - [ ] Keep platform artifacts inside `output/{platform}` only.

## Documentation Cleanup

- [ ] Reconcile older roadmap and blueprint documents after implementation.
  - [ ] Mark completed items in source docs or move them to changelog notes.
  - [ ] Keep this file until every remaining item above is done.
- [ ] Update release notes for each completed backlog batch.
  - [ ] Include tests run.
  - [ ] Include platform outputs verified.
