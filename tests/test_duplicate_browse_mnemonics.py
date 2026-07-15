import subprocess


def test_two_browse_labels_get_distinct_mnemonics(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-mnemonics", "Browse...,Browse..."],
        capture_output=True, text=True, timeout=10
    )
    letters = result.stdout.strip().split(",")
    assert len(letters) == 2
    assert letters[0] != ""
    assert letters[1] != ""
    assert letters[0] != letters[1]
