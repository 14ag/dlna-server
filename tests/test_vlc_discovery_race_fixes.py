import subprocess
from pathlib import Path

import pytest

# Source path resolution follows this suite's convention see
# test_initial_scan_completion_fix py and test_desktop_integration_static py
# which both build REPO_ROOT as parent parent of the test file
REPO_ROOT = Path(__file__).resolve().parents[1]
POSIX_SSDP_SOURCE = REPO_ROOT / "src" / "posix_ssdp.cpp"


def run_binary(binary_path, args):
    result = subprocess.run([binary_path, *args], capture_output=True, text=True, timeout=30)
    return result


class TestSsdpResponseDelayMargin:
    def test_max_delay_at_mx_five_has_safety_margin(self, dlna_binary):
        result = run_binary(dlna_binary, ["--print-ssdp-response-delay-bound", "5"])
        assert result.returncode == 0
        assert result.stdout.strip() == "4000"

    def test_max_delay_at_mx_one_is_zero(self, dlna_binary):
        result = run_binary(dlna_binary, ["--print-ssdp-response-delay-bound", "1"])
        assert result.returncode == 0
        assert result.stdout.strip() == "0"

    def test_max_delay_at_mx_zero_is_zero(self, dlna_binary):
        result = run_binary(dlna_binary, ["--print-ssdp-response-delay-bound", "0"])
        assert result.returncode == 0
        assert result.stdout.strip() == "0"

    def test_max_delay_never_reaches_full_mx_window(self, dlna_binary):
        # regression lock for the exact bug this task fixes a bound
        # equal to or above the full mx window would recreate the
        # race against a pupnp derived client's own listen timeout
        result = run_binary(dlna_binary, ["--print-ssdp-response-delay-bound", "5"])
        bound_ms = int(result.stdout.strip())
        assert bound_ms < 5000


@pytest.mark.posix_only
class TestPosixSsdpUnicastInterfaceSelection:
    def test_source_no_longer_uses_removed_helper(self):
        text = POSIX_SSDP_SOURCE.read_text(encoding="utf-8")
        assert "SetOutboundInterface(" not in text
        assert "SetMulticastOutboundInterface(" in text
        assert "SetUnicastOutboundInterface(" in text

    def test_unicast_reply_path_does_not_call_multicast_helper(self):
        text = POSIX_SSDP_SOURCE.read_text(encoding="utf-8")
        reply_fn_start = text.index("void SSDP::SendDelayedSearchResponse")
        next_fn_start = text.index("void SSDP::ResponseWorker")
        reply_fn_body = text[reply_fn_start:next_fn_start]
        assert "SetUnicastOutboundInterface" in reply_fn_body
        assert "SetMulticastOutboundInterface" not in reply_fn_body

    def test_notify_path_still_uses_multicast_helper(self):
        text = POSIX_SSDP_SOURCE.read_text(encoding="utf-8")
        notify_fn_start = text.index("void SSDP::SendNotifyRound")
        notify_fn_end = text.index("void SSDP::SendNotifyBurst")
        notify_fn_body = text[notify_fn_start:notify_fn_end]
        assert "SetMulticastOutboundInterface" in notify_fn_body


class TestServerStartReentrancy:
    def test_concurrent_start_never_double_binds(self, dlna_binary, media_source_dir):
        result = run_binary(dlna_binary, [
            "--source", str(media_source_dir),
            "--print-concurrent-start-start-safety",
        ])
        assert result.returncode == 0
        lines = dict(
            line.split("=", 1) for line in result.stdout.strip().splitlines() if "=" in line
        )
        a_ok = lines["a-ok"] == "1"
        b_ok = lines["b-ok"] == "1"
        # exactly one caller must have won the race and actually started
        # the loser must report the specific reentrancy reason string
        assert a_ok != b_ok
        loser_reason = lines["a-reason"] if not a_ok else lines["b-reason"]
        assert "already starting" in loser_reason
        assert lines["is-running"] == "1"
        assert lines["after-stop-running"] == "0"

    def test_concurrent_start_is_stable_across_repeated_runs(self, dlna_binary, media_source_dir):
        for _ in range(20):
            result = run_binary(dlna_binary, [
                "--source", str(media_source_dir),
                "--print-concurrent-start-start-safety",
            ])
            assert result.returncode == 0