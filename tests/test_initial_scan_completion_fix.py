"""Regression tests for the initial-scan-gated-behind-backgroundScanEnabled bug.

Root cause: Server::Start() set m_initialScanComplete = true immediately
after AppMedia.ResetForRescan() (which only allocates an empty root
container) instead of after the initial MediaSources::Scan() actually
completed, and gated whether that initial scan ran at all behind the
BackgroundScanEnabled setting, which is only supposed to control
post-startup auto-rescan-on-change behavior in Server::WatchLoop().

These tests parse the two Server::Start() implementations directly rather
than compiling and running the server, matching this project's existing
Python test convention for C++ lifecycle/ordering invariants.
"""

import re
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent

PLATFORM_FILES = [
    REPO_ROOT / "src" / "server.cpp",
    REPO_ROOT / "src" / "posix_server.cpp",
]


def _read(path: Path) -> str:
    assert path.is_file(), f"expected source file missing: {path}"
    return path.read_text(encoding="utf-8")


def _extract_start_function(source_text: str) -> str:
    """Return the body text of `bool Server::Start() {` ... matching close brace."""
    match = re.search(r"bool\s+Server::Start\s*\(\s*\)\s*\{", source_text)
    assert match, "could not locate Server::Start() definition"
    start_idx = match.end()
    depth = 1
    idx = start_idx
    while depth > 0:
        if source_text[idx] == "{":
            depth += 1
        elif source_text[idx] == "}":
            depth -= 1
        idx += 1
    return source_text[start_idx:idx]


@pytest.mark.parametrize("path", PLATFORM_FILES, ids=lambda p: p.name)
def test_initial_scan_is_not_gated_by_background_scan_enabled(path: Path):
    """The initial scan trigger must not be conditional on
    cfg.backgroundScanEnabled. That flag only controls WatchLoop's
    post-startup auto-rescan-on-change behavior (see ShouldAutoRescan in
    source_watcher.cpp), not whether the first scan happens at all.
    """
    body = _extract_start_function(_read(path))
    forbidden = re.search(
        r"if\s*\(\s*cfg\.backgroundScanEnabled\s*\)\s*\{\s*StartBackgroundScan\s*\(\s*\)\s*;\s*\}",
        body,
    )
    assert forbidden is None, (
        f"{path}: Server::Start() still gates the initial scan behind "
        "cfg.backgroundScanEnabled; StartBackgroundScan() must be called "
        "unconditionally"
    )


@pytest.mark.parametrize("path", PLATFORM_FILES, ids=lambda p: p.name)
def test_initial_scan_complete_is_set_after_reset_before_http_start(path: Path):
    """m_initialScanComplete must be set to true after ResetForRescan()
    (so the root container exists) and before HTTP/SSDP start (so the
    ContentDirectory guard in Browse does not return 710). The actual
    scan's JoinBackgroundScan() runs on a detached thread; the
    m_initialScanComplete flag only gates the root container check, not
    the scan progress.
    """
    body = _extract_start_function(_read(path))

    reset_match = re.search(r"AppMedia\.ResetForRescan\s*\(\s*\)\s*;", body)
    complete_match = re.search(
        r"m_initialScanComplete\.store\s*\(\s*true\s*,", body
    )
    http_start_match = re.search(r"HttpServer::Get\(\)\.Start", body)

    assert reset_match, f"{path}: AppMedia.ResetForRescan() call not found in Start()"
    assert complete_match, (
        f"{path}: m_initialScanComplete.store(true, ...) call not found in Start()"
    )
    assert http_start_match, f"{path}: HttpServer::Get().Start() call not found in Start()"

    assert reset_match.start() < complete_match.start(), (
        f"{path}: ResetForRescan() must run before "
        "m_initialScanComplete.store(true, ...)"
    )
    assert complete_match.start() < http_start_match.start(), (
        f"{path}: m_initialScanComplete.store(true, ...) must be set before "
        "HttpServer starts, so Browse does not return 710"
    )


@pytest.mark.parametrize("path", PLATFORM_FILES, ids=lambda p: p.name)
def test_start_background_scan_is_joined_before_returning(path: Path):
    """Start() must call JoinBackgroundScan() (blocking) so the caller only
    observes success once the initial scan has actually completed. This
    protects the ContentDirectory 710 "initial scan in progress" fault path
    in contentdirectory.cpp from being bypassed.
    """
    body = _extract_start_function(_read(path))
    start_scan_match = re.search(r"StartBackgroundScan\s*\(\s*\)\s*;", body)
    join_match = re.search(r"JoinBackgroundScan\s*\(\s*\)\s*;", body)

    assert start_scan_match, f"{path}: StartBackgroundScan() call not found in Start()"
    assert join_match, f"{path}: JoinBackgroundScan() call not found in Start()"
    assert start_scan_match.start() < join_match.start(), (
        f"{path}: StartBackgroundScan() must be immediately followed by a "
        "JoinBackgroundScan() call later in the same function"
    )


def test_content_directory_still_gates_on_initial_scan_complete():
    """Sanity check that the consumer of the flag (contentdirectory.cpp)
    still contains its existing guard. This fix relies on that guard already
    existing and being reachable; it must not have been removed separately.
    """
    content_directory_path = REPO_ROOT / "src" / "contentdirectory.cpp"
    text = _read(content_directory_path)
    occurrences = text.count("DLNAServer.IsInitialScanComplete()")
    assert occurrences >= 2, (
        f"{content_directory_path}: expected IsInitialScanComplete() guards "
        "in both the Search and Browse handlers, found "
        f"{occurrences} occurrence(s)"
    )