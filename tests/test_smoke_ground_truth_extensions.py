import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

EXCLUDED = {".m3u", ".m3u8", ".pls", ".ts"}


def test_kformats_and_ground_truth_media_extensions_are_synchronized():
    kformats_source = (ROOT / "src/dlna_utils.cpp").read_text(encoding="utf-8")

    kformats_exts = set()
    for match in re.finditer(r'\{ L"(\.[a-z0-9]+)"', kformats_source):
        ext = match.group(1)
        if ext not in EXCLUDED:
            kformats_exts.add(ext)

    smoke_source = (ROOT / "tests/verify-smoke.ps1").read_text(encoding="utf-8")
    array_match = re.search(
        r'\$script:kGroundTruthMediaExtensions\s*=\s*@\(([^)]+)\)',
        smoke_source
    )
    assert array_match is not None, (
        "$script:kGroundTruthMediaExtensions array not found in verify-smoke.ps1"
    )
    array_body = array_match.group(1)
    smoke_exts_no_dot = set()
    for token in re.finditer(r"'([a-z0-9]+)'", array_body):
        smoke_exts_no_dot.add(token.group(1))

    kformats_no_dot = {e.lstrip(".") for e in kformats_exts}

    assert kformats_no_dot == smoke_exts_no_dot, (
        f"kFormats extensions (minus excluded={EXCLUDED}): {sorted(kformats_no_dot)}\n"
        f"Ground truth extensions: {sorted(smoke_exts_no_dot)}\n"
        f"Only in kFormats: {sorted(kformats_no_dot - smoke_exts_no_dot)}\n"
        f"Only in ground truth: {sorted(smoke_exts_no_dot - kformats_no_dot)}"
    )