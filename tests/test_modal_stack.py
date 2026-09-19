import subprocess


EXPECTED = [
    "empty=1",
    "after-null-push-depth=0",
    "top=3 depth=3",
    "repush-top=1 depth=3",
    "after-remove-top=3 depth=2",
    "remove-absent-depth=2",
    "cleared-top=0 empty=1",
]


def test_modal_stack_lifecycle(dlna_binary):
    result = subprocess.run(
        [str(dlna_binary), "--print-modal-stack-lifecycle"],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    assert lines == EXPECTED
