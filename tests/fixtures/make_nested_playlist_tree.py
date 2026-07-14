import socket
import threading
import time
from contextlib import contextmanager
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path


def make_nested_playlist_tree(root_dir, num_nested=20, delay_ms=100):
    root_dir = Path(root_dir)
    root_dir.mkdir(parents=True, exist_ok=True)

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()

    root_playlist = root_dir / "broadband.m3u"
    lines = ["#EXTM3U"]
    for i in range(num_nested):
        lines.append(f"nested_{i}.m3u8")
    root_playlist.write_text("\n".join(lines) + "\n", encoding="utf-8")

    for i in range(num_nested):
        pl_lines = [
            "#EXTM3U",
            f"#EXTINF:10,Segment {i}",
            f"http://127.0.0.1:{port}/segment_{i}.ts",
            f"#EXTINF:10,Segment {i}_1",
            f"segment_{i}_1.ts",
            f"#EXTINF:10,Segment {i}_2",
            f"segment_{i}_2.ts",
        ]
        (root_dir / f"nested_{i}.m3u8").write_text(
            "\n".join(pl_lines) + "\n", encoding="utf-8"
        )

    for i in range(num_nested):
        for j in range(1, 3):
            (root_dir / f"segment_{i}_{j}.ts").write_bytes(b"")

    class _Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if delay_ms > 0:
                time.sleep(delay_ms / 1000.0)
            rel = self.path.lstrip("/")
            target = root_dir / rel
            if target.exists() and target.is_file():
                data = target.read_bytes()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Content-Type",
                                 "video/MP2T" if rel.endswith(".ts")
                                 else "application/octet-stream")
                self.end_headers()
                self.wfile.write(data)
            else:
                self.send_response(404)
                self.send_header("Content-Length", "0")
                self.end_headers()

        def log_message(self, *a):
            pass

    server = HTTPServer(("127.0.0.1", port), _Handler)

    @contextmanager
    def serve():
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield
        finally:
            server.shutdown()
            thread.join(timeout=5)

    return {
        "root_playlist_path": str(root_playlist),
        "nested_count": num_nested,
        "port": port,
        "serve": serve,
    }
