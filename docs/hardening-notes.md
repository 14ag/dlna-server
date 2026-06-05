# DLNA Server Hardening Notes

This hardening pass removes shell-based remote source fetching, validates ports before binding or advertising, and makes shutdown paths safer under active HTTP traffic.

## Security And Reliability Changes

- Remote playlist, network directory, HEAD, and stream reads use an optional `libcurl` backend instead of spawning a `curl` child process per request.
- Remote source failures log clearer authentication, empty-listing, and unavailable-backend messages; directory listings handle simple name lists and basic Unix-style entries.
- HTTP and SSDP start before the background media scan completes, so large or remote libraries do not block initial discovery.
- Media scans build indexed state off-lock, then swap it into place; item lookup, child lookup, and descendant traversal avoid repeated full-list scans.
- Existing media organization settings now apply during scan and Browse: flat folder scans recurse into the current container, playlist order remains stable unless title sorting is enabled, and remote items can be advertised directly when stream proxying is disabled.
- Companion album art lookup covers common Android-style `thumb` names and Browse advertises album art only when the backing file is still present.
- SSDP M-SEARCH responses are delayed on a response worker so MX jitter does not block the receive loop.
- IP whitelist entries support exact IPs and CIDR ranges such as `192.168.1.0/24` and `fd00::/8`.
- POSIX HTTP caps active client threads and reaps completed handlers; POSIX logs keep a rolling buffer.
- IP whitelist reloads are synchronized so HTTP workers can safely check clients while settings are saved.
- POSIX HTTP now tracks client threads and joins them during shutdown; both Windows and POSIX sockets use receive/send timeouts.
- POSIX SSDP now advertises and responds on IPv6 and refreshes `ssdp:alive` before the 1800-second cache expiry.
- Port settings are constrained to `1..65535`; invalid config values fall back to defaults and invalid GUI/CLI values are rejected.
- Stale `.bak` source files were removed and future backups are ignored.
