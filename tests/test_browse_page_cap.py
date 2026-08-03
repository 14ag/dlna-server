import subprocess
import time
import xml.etree.ElementTree as ET

import pytest

NS = {"d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"}


def _root_containers(running_server):
    root = running_server.soap_browse(object_id="0", requested_count=100)
    assert root["errorCode"] == 0
    didl = root["Result"]
    return ET.fromstring(didl).findall("d:container", NS) if didl else []


@pytest.fixture
def populated_media(media_source_dir):
    # Populate BEFORE the running_server fixture launches the binary, so
    # the initial scan actually finds these files. running_server depends
    # on media_source_dir and starts the server after all its dependencies
    # have been set up, so requesting populated_media before running_server
    # in the test signature guarantees this runs first.
    for i in range(25):
        (media_source_dir / f"track-{i:02d}.mp3").write_bytes(b"\x00" * 512)
    return media_source_dir


@pytest.mark.parametrize("requested,available,expected", [
    (0, 50, 50),               # RequestedCount=0, small container: uncapped, returns everything
    (0, 30000, 16000),         # RequestedCount=0, huge container: capped at kMaxBrowseResponseItems
    (10, 30000, 10),           # explicit RequestedCount below cap: unaffected
    (20000, 30000, 16000),     # explicit RequestedCount above cap: still capped
    (0, 16000, 16000),         # exact boundary
    (0, 16001, 16000),         # one past boundary
])
def test_clamp_browse_requested_count(dlna_binary, requested, available, expected):
    result = subprocess.run(
        [dlna_binary, "--print-clamp-browse-requested-count", str(requested), str(available)],
        capture_output=True, text=True, timeout=10,
    )
    assert int(result.stdout.strip()) == expected


def test_browse_requested_count_zero_returns_all_small_container(populated_media, running_server):
    # Functional sanity check at small scale (the 16000-item boundary is
    # covered deterministically by the pure-function test above, without
    # needing a huge synthetic library). A RequestedCount=0 Browse against
    # a small container must return every item, so NumberReturned equals
    # TotalMatches.
    #
    # Root container "0" holds the media-source wrapper container; the
    # seeded files live inside that source container (same layout the
    # existing pagination tests use). Discover it, then browse it with
    # RequestedCount=0.
    #
    # The initial scan publishes the wrapper asynchronously after the
    # server's TCP listener is up, so poll (bounded) until the wrapper
    # appears instead of asserting against the first browse, or we lose
    # the startup race on whichever platform scans more slowly.
    containers = []
    deadline = time.time() + 15
    while time.time() < deadline:
        containers = _root_containers(running_server)
        if containers:
            break
        time.sleep(0.1)
    assert containers, "expected a media-source wrapper container at root"
    source_id = containers[0].get("id")

    # The source container's child count is populated as the scan of the
    # 25 seeded files completes; poll the source browse until all of them
    # are cataloged (again, don't race the async scan).
    deadline = time.time() + 15
    total_matches = 0
    numbered_returned = -1
    while time.time() < deadline:
        response = running_server.soap_browse(object_id=source_id, requested_count=0)
        assert response["errorCode"] == 0
        numbered_returned = response["NumberReturned"]
        total_matches = response["TotalMatches"]
        if total_matches == 25:
            break
        time.sleep(0.1)
    assert numbered_returned == total_matches
    assert total_matches == 25, (
        "expected all 25 seeded files to be cataloged, got "
        f"{total_matches}")
