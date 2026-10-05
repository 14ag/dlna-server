# Testing

Python-only suite under `tests/`. No C++ tests. Blackbox tests first
(HTTP, SOAP, SSDP, GENA, browse, playlists, packaging, build outputs).
Source-grep contract tests only where device behavior cannot run in CI.

## Run

From repo root:

```
pytest                       # full suite (~6 min on Windows)
pytest -q --tb=no            # quiet, no tracebacks
pytest -q tests/test_access_keys.py
pytest -q --collect-only     # count only (~500 collected, ~90 deselected on Windows)
```

Filter failures only on a full run:

```
python -m pytest -v --tb=no 2>&1 | Select-String -Pattern " FAILED | ERROR | SKIP "
```

## Conventions

- One behavior, one assertion site. Duplicate hook checks merge into a single
  `@pytest.mark.parametrize` table (see `tests/test_access_keys.py`).
- Expensive setup is shared, not repeated. `gtk4_geometry_dump` (session
  fixture in `tests/conftest.py`) dumps GUI geometry once for all geometry
  tests. `media_tree` (module fixture in
  `tests/test_vlc_discovery_browse.py`) walks the catalog once for all
  tree-content tests.
- Platform markers deselect automatically (`tests/conftest.py`):

  | marker         | runs on            |
  |----------------|--------------------|
  | `posix_only`   | Linux/macOS only   |
  | `windows_only` | Windows only       |
  | `gui_only`     | GUI builds only    |

  Tag environment-locked tests instead of skipping. No `skip`/`skipif`
  without a platform marker.

## Flaky by environment

Live-server and multicast tests vary run to run on Windows:
`tests/test_incremental_scan.py` (timing),
`tests/test_vlc_discovery_browse.py` device/browse tests (need a server
advertising ContentDirectory over multicast). A single green retry clears
them. Server-header format is pinned separately and stable:
`tests/test_ssdp_server_header.py::test_server_header_format`.
