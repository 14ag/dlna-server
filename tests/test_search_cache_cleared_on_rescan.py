import subprocess


def test_search_cache_cleared_on_rescan(dlna_binary, media_source_dir):
    result = subprocess.run(
        [dlna_binary, "--source", str(media_source_dir),
         "--print-search-cache-cleared-on-rescan"],
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, (
        f"returncode={result.returncode:#x}\nstderr={result.stderr}"
    )
    assert "before-rescan-cache-size=" in result.stdout, result.stdout
    assert "after-rescan-cache-size=0" in result.stdout, result.stdout