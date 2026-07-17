"""
Minimal HTTP server that drip-feeds an M3U playlist body slowly, so a
client mid-download stays "in progress" for a controllable duration.
Used to reproduce and verify the fix for: Server::Stop() hanging while a
media scan is still fetching a slow/huge remote playlist.
"""
import http.server
import socketserver
import threading
import time


class SlowDripHandler(http.server.BaseHTTPRequestHandler):
    TOTAL_DRIP_SECONDS = 20
    DRIP_CHUNKS = 20

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "audio/x-mpegurl")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        self._write_chunk(b"#EXTM3U\n")
        for i in range(self.DRIP_CHUNKS):
            time.sleep(self.TOTAL_DRIP_SECONDS / self.DRIP_CHUNKS)
            try:
                self._write_chunk(
                    f"#EXTINF:-1,drip-{i}\nhttp://127.0.0.1:1/unused-{i}.mp3\n".encode()
                )
            except (BrokenPipeError, ConnectionResetError):
                return
        self._write_chunk(b"")

    def _write_chunk(self, data: bytes):
        length_line = f"{len(data):x}\r\n".encode()
        self.wfile.write(length_line + data + b"\r\n")
        self.wfile.flush()


class SlowPlaylistServer:
    def __init__(self):
        self.httpd = socketserver.ThreadingTCPServer(
            ("127.0.0.1", 0), SlowDripHandler
        )
        self.httpd.daemon_threads = True
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)

    @property
    def port(self) -> int:
        return self.httpd.server_address[1]

    @property
    def playlist_url(self) -> str:
        return f"http://127.0.0.1:{self.port}/slow.m3u8"

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.httpd.shutdown()
        self.httpd.server_close()
