from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_windows_listen_socket_uses_exclusive_flag_not_reuseaddr():
    source = read("src/httpserver.cpp")

    assert "SO_EXCLUSIVEADDRUSE" in source
    assert "SOL_SOCKET, SO_REUSEADDR" not in source


def test_windows_listen_socket_logs_bind_and_listen_failures():
    source = read("src/httpserver.cpp")

    assert "HTTP listen socket creation failed" in source
    assert "HTTP listen bind failed" in source
    assert "HTTP listen call failed" in source
    assert source.count("WSAGetLastError()") >= 4


def test_http_server_start_logs_partial_bind_failures():
    source = read("src/httpserver.cpp")

    start_body_index = source.index("bool HttpServer::Start(int port) {")
    start_body_end = source.index("void HttpServer::Stop()")
    start_body = source[start_body_index:start_body_end]

    assert "bound ipv6 only on port" in start_body
    assert "bound ipv4 only on port" in start_body


def test_posix_httpserver_is_unchanged_by_this_workflow():
    source = read("src/posix_httpserver.cpp")

    assert "SO_EXCLUSIVEADDRUSE" not in source
