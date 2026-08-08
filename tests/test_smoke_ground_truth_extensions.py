import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

EXCLUDED = {".m3u", ".m3u8", ".pls", ".ts"}
GROUND_TRUTH_MEDIA_EXTENSIONS = {
    "mp4", "m4v", "mkv", "webm", "avi", "divx", "mov",
    "mpg", "mpeg", "mpe", "vob", "m2ts", "mts", "wmv", "flv", "3gp", "3g2",
    "mp3", "flac", "m4a", "aac", "wav", "wma", "ogg", "oga", "opus",
    "aiff", "aif", "ac3", "dts",
    "jpg", "jpeg", "png", "gif", "bmp", "tif", "tiff", "webp",
}


def test_kformats_and_ground_truth_media_extensions_are_synchronized():
    kformats_source = (ROOT / "src/dlna_utils.cpp").read_text(encoding="utf-8")

    kformats_exts = set()
    for match in re.finditer(r'\{ L"(\.[a-z0-9]+)"', kformats_source):
        ext = match.group(1)
        if ext not in EXCLUDED:
            kformats_exts.add(ext)

    kformats_no_dot = {e.lstrip(".") for e in kformats_exts}

    assert kformats_no_dot == GROUND_TRUTH_MEDIA_EXTENSIONS, (
        f"kFormats extensions (minus excluded={EXCLUDED}): {sorted(kformats_no_dot)}\n"
        f"Ground truth extensions: {sorted(GROUND_TRUTH_MEDIA_EXTENSIONS)}\n"
        f"Only in kFormats: {sorted(kformats_no_dot - GROUND_TRUTH_MEDIA_EXTENSIONS)}\n"
        f"Only in ground truth: {sorted(GROUND_TRUTH_MEDIA_EXTENSIONS - kformats_no_dot)}"
    )
