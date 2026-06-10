# DLNA Server Hardening Notes

## Security And Reliability Changes

- Remote playlist and network-share IO uses libcurl-backed requests instead of shell-based remote source fetching.
- HTTP request handling bounds headers and SOAP body size before routing requests.
- Media scanning runs in the background and swaps completed indexes atomically.
- IP whitelist handling supports exact addresses and CIDR ranges.
- SSDP discovery uses delayed response scheduling, strict MX parsing, and safer shutdown ordering.
- Album art and subtitle discovery avoid advertising broken resources where the scanner cannot confirm a companion file.
- Scan, HTTP, SSDP, logging, and description paths read config snapshots instead of mutable fields during worker execution.

## 2026-06-06 Review Follow-Up

- SOAP control dispatch now extracts the action element instead of searching arbitrary body substrings.
- Media browse hot paths batch child-count lookup, cache album-art directory probes, and avoid type-erased descendant recursion.
- Remote directory listing no longer treats every extensionless entry as a directory.
- POSIX logging keeps a bounded deque with timestamps matching the Windows log format.
- Windows debug logging uses a persistent UTF-8 append handle.
- Windows file, subtitle, and album-art responses use scoped file handles instead of `goto` cleanup labels.
