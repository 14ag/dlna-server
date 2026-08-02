import subprocess


def _run(dlna_binary, cwd):
    return subprocess.run(
        [dlna_binary, "--print-media-database-id-reuse-lifecycle"],
        capture_output=True, text=True, timeout=10, cwd=str(cwd),
    )


def test_media_database_reuses_freed_ids(dlna_binary, tmp_path):
    result = _run(dlna_binary, tmp_path)
    assert result.returncode == 0, result.stderr

    lines = result.stdout.strip().splitlines()
    assert lines, f"hook produced no output: {result.stdout!r}"

    values = {}
    for line in lines:
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip()

    initial = values["initial"].split(",")
    assert len(initial) == 3, f"expected three initial ids, got {initial}"
    id_a, id_b, id_c = (int(v) for v in initial)

    assert int(values["pruned"]) == 1, (
        "exactly one record (key-b) should have been pruned in the second pass")

    id_d = int(values["reused"])

    # key-d is brand new in the third pass, and key-b's ID was freed in the
    # second pass -- so the free-list must be the source of id_d, i.e. it
    # must equal key-b's old id (never a fresh ID beyond id_c).
    assert id_d == id_b, (
        f"expected key-d to reuse key-b's freed id {id_b}, got {id_d} "
        f"(initial ids: {id_a},{id_b},{id_c})")


def test_media_database_fresh_ids_are_strictly_increasing(dlna_binary, tmp_path):
    result = _run(dlna_binary, tmp_path)
    assert result.returncode == 0, result.stderr
    lines = result.stdout.strip().splitlines()
    values = dict(line.split("=", 1) for line in lines)
    id_a, id_b, id_c = (int(v) for v in values["initial"].split(","))
    assert id_a == 1000000
    assert id_b == id_a + 1
    assert id_c == id_b + 1
