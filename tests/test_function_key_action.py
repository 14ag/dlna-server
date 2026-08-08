import subprocess


def run(dlna_binary, vk, running, busy, scanning):
    args = [dlna_binary, "--print-function-key-action", str(vk), running, busy, scanning]
    result = subprocess.run(args, capture_output=True, text=True, timeout=10)
    return result.stdout.strip()


def test_f1_always_shows_help(dlna_binary):
    assert run(dlna_binary, "112", "0", "0", "0") == "show-help"
    assert run(dlna_binary, "112", "1", "1", "1") == "show-help"


def test_f5_rescans_when_running(dlna_binary):
    assert run(dlna_binary, "116", "1", "0", "0") == "rescan"


def test_f5_refreshes_list_when_stopped(dlna_binary):
    assert run(dlna_binary, "116", "0", "0", "0") == "refresh-source-list"


def test_f5_does_nothing_when_busy_or_scanning(dlna_binary):
    assert run(dlna_binary, "116", "1", "1", "0") == "none"
    assert run(dlna_binary, "116", "1", "0", "1") == "none"


def test_unrelated_key_does_nothing(dlna_binary):
    assert run(dlna_binary, "65", "0", "0", "0") == "none"
