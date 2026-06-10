# Feature Spec: PDF-Derived DLNA Framework Readiness

## Summary

This document captures clean-room requirements for DLNA framework hardening. Implementation Blueprint tasks must use project code patterns and must not copy GPL implementation code.

## Requirements

- HTTP request handling reads complete headers before dispatch.
- GET and HEAD share content length, range, album art, subtitle, and media response rules.
- SOAP control dispatch validates ContentDirectory and ConnectionManager actions by action element.
- Event subscription request handling returns UPnP-compatible status codes for subscribe and unsubscribe paths.

## Implementation Blueprint

- Build shared protocol utilities before route-specific changes.
- Keep platform socket code in Windows and POSIX files while moving common protocol rules into `dlna_utils`.
- Add source-contract tests for helper ownership and blackbox tests for HTTP/SOAP behavior where runtime setup permits.
