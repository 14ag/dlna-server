import subprocess


def test_default_title_includes_lowercase_extension(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-display-title", "0", "", "C:/movies/Foo.MKV"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "Foo.mkv"


def test_default_title_no_extension_when_source_has_none(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-display-title", "0", "", "https://example.com/stream"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "stream"


def test_show_file_names_keeps_original_case_extension(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-display-title", "1", "", "C:/movies/Foo.MKV"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "Foo.MKV"


def test_title_override_wins_over_default_and_is_unmodified(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-display-title", "0", "Playlist Track Title", "C:/movies/Foo.MKV"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "Playlist Track Title"


def test_show_file_names_wins_over_title_override(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-display-title", "1", "Playlist Track Title", "C:/movies/Foo.MKV"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "Foo.MKV"
