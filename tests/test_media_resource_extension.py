import subprocess


def test_extension_suffix_present_for_mkv(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-resource-url-suffix", "C:/movies/Foo.MKV"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == ".mkv"


def test_extension_suffix_empty_when_no_extension(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-media-resource-url-suffix", "https://example.com/stream"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == ""


def test_strip_resource_id_extension(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-strip-resource-id-extension", "123.mkv"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "123"


def test_strip_resource_id_extension_no_suffix(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-strip-resource-id-extension", "123"],
        capture_output=True, text=True,
    )
    assert result.stdout.strip() == "123"
