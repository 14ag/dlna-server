from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_media_sources_has_no_removed_dead_methods():
    header = read("src/media_sources.h")
    assert "GetChildCount(int parentId)" not in header
    assert "GetAllItems()" not in header