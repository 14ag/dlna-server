from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_posix_httpserver_defines_handle_client_exactly_once():
    source = (ROOT / "src/posix_httpserver.cpp").read_text(encoding="utf-8")
    assert source.count("void HttpServer::HandleClient(int clientSocket, const std::string& clientIp) {") == 1