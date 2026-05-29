# DLNA Server Hardening Notes

This hardening pass removes shell-based remote source fetching, validates ports before binding or advertising, and makes shutdown paths safer under active HTTP traffic.

## Security And Reliability Changes

- Remote playlist, network directory, HEAD, and stream reads still use `curl`, but the process is launched directly with argv instead of through a shell.
- IP whitelist reloads are synchronized so HTTP workers can safely check clients while settings are saved.
- POSIX HTTP now tracks client threads and joins them during shutdown; both Windows and POSIX sockets use receive/send timeouts.
- POSIX SSDP now advertises and responds on IPv6 and refreshes `ssdp:alive` before the 1800-second cache expiry.
- Port settings are constrained to `1..65535`; invalid config values fall back to defaults and invalid GUI/CLI values are rejected.
- Stale `.bak` source files were removed and future backups are ignored.
