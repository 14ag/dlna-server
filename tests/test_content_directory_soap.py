import pytest


class TestContentDirectorySoap:
    def test_search_during_scan_does_not_fault_710(
            self, slow_playlist_source, running_server):
        """Search during an active scan does not return UPnP error 710."""
        result = running_server.soap_search(
            container_id="0", search_criteria="")
        err = result.get("errorCode", 0)
        assert err != 710, (
            f"Search returned UPnP error 710: "
            f"{result.get('errorDescription')}")
