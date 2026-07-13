from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")



def test_curl_remote_fetch_uses_user_agent_ftp_options_low_speed_and_redacted_logs():
    source = read_source("src/network_sources.cpp")

    for token in (
        "CURLOPT_USERAGENT",
        "DLNA-Server/",
        "CURLOPT_FTP_RESPONSE_TIMEOUT",
        "CURLOPT_FTP_USE_EPSV",
        "CURLOPT_LOW_SPEED_LIMIT",
        "CURLOPT_LOW_SPEED_TIME",
        "RedactUrlForLog",
    ):
        assert token in source

    assert "authentication failed for %ls\", RedactUrlForLog(url).c_str()" in source
    assert "Remote content truncated after %d bytes: %ls\", kMaxCurlOutputBytes, RedactUrlForLog(url).c_str()" in source
    assert "user:pass@" not in source


def test_hls_item_defaults_to_direct_exposure_when_not_proxied():
    content = read_source("src/contentdirectory.cpp")
    # exposeRemoteDirect must remain gated on proxyStreams / ShouldProxyRemoteUrl
    assert "exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !proxyStreams && !ShouldProxyRemoteUrl(it.path)" in content



