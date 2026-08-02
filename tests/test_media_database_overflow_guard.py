import subprocess


def test_media_database_id_overflow_guard(dlna_binary, tmp_path):
    tsv = tmp_path / "media-cache-overflow.tsv"
    tsv.write_text(
        "# dlna-server media-cache.tsv v1\n"
        "2147483646\tpre-existing-key\t\n",
        encoding="utf-8",
    )

    result = subprocess.run(
        [dlna_binary, "--print-media-database-id-overflow-guard", str(tsv)],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr

    lines = [line.strip() for line in result.stdout.strip().splitlines() if line.strip()]
    assert len(lines) == 2, f"expected two id lines, got {lines}"

    id_a = int(lines[0])
    id_b = int(lines[1])

    # Load() sets m_nextId = 2147483647 (highest persisted id + 1). The first
    # new key must get that id (2147483647 = INT_MAX, the last valid signed
    # int). The second new key must NOT overflow UB; instead it falls back to
    # kPersistentMediaIdBase (1000000) via the guard.
    assert id_a == 2147483647, f"expected id_a == INT_MAX (2147483647), got {id_a}"
    assert id_b == 1000000, f"expected id_b to fall back to base (1000000), got {id_b}"
