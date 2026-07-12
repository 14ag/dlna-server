import pytest


class TestContentDirectorySoap:
    def test_browse_root_before_any_scan_result_is_well_formed(
            self, running_server):
        """Browse(ObjectID=0) on an empty source dir returns 200 OK with
        NumberReturned=0, not a UPnP 710 fault."""
        result = running_server.soap_browse(object_id="0")
        err = result.get("errorCode", 0)
        assert err != 710, (
            f"Unexpected UPnP error 710: "
            f"{result.get('errorDescription')}")
        assert result["NumberReturned"] == 0, (
            f"Expected 0 items, got {result['NumberReturned']}")
        assert result["TotalMatches"] == 0

    def test_search_during_scan_does_not_fault_710(
            self, slow_playlist_source, running_server):
        """Search during an active scan does not return UPnP error 710."""
        result = running_server.soap_search(
            container_id="0", search_criteria="")
        err = result.get("errorCode", 0)
        assert err != 710, (
            f"Search returned UPnP error 710: "
            f"{result.get('errorDescription')}")
