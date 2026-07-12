import sys, os, time, socket, threading, shutil, tempfile
from pathlib import Path
from http.server import HTTPServer, BaseHTTPRequestHandler

sys.path.insert(0, 'tests')
from conftest import _free_port, _launch_server, _teardown_server, ServerClient

# Clean up old debug log first
debug_log = Path('build_winx64/Debug/debug.log')
if debug_log.exists():
    debug_log.unlink()

tmp = tempfile.mkdtemp()
media_dir = Path(tmp) / 'media'
media_dir.mkdir()

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(('127.0.0.1', 0))
port = s.getsockname()[1]
s.close()

root_pl = media_dir / 'broadband.m3u'
root_pl.write_text('#EXTM3U\nnested_0.m3u8\nnested_1.m3u8\n', encoding='utf-8')

for i in range(2):
    pl = media_dir / f'nested_{i}.m3u8'
    pl.write_text(f'#EXTM3U\n#EXTINF:10,Seg {i}\nhttp://127.0.0.1:{port}/seg_{i}.ts\n#EXTINF:10,Seg {i}_1\nseg_{i}_1.ts\n', encoding='utf-8')
    (media_dir / f'seg_{i}_1.ts').write_bytes(b'')

print(f"Files in media_dir:")
for f in media_dir.iterdir():
    print(f"  {f.name} ({f.stat().st_size} bytes)")

class H(BaseHTTPRequestHandler):
    def do_GET(self):
        time.sleep(0.05)
        self.send_response(200)
        self.send_header('Content-Length', '0')
        self.end_headers()
    def log_message(self, *a): pass

httpd = HTTPServer(('127.0.0.1', port), H)
t = threading.Thread(target=httpd.serve_forever, daemon=True)
t.start()

port2 = _free_port()
binary = 'build_winx64/Debug/DLNA Server.exe'
proc, connected, old_config, config_ini = _launch_server(binary, port2, media_dir)
print(f'Server connected: {connected}')

if connected:
    client = ServerClient(f'http://127.0.0.1:{port2}', Path(binary).parent)
    
    # Wait longer for scan to complete (2 nested playlists x 50ms delay)
    time.sleep(3.0)
    
    # Check debug log for scan messages
    if debug_log.exists():
        content = debug_log.read_text(encoding='utf-8')
        scan_lines = [l for l in content.splitlines() if 'scan' in l.lower() or 'Scan' in l or 'Skipping' in l or 'media source' in l.lower() or 'Finished' in l]
        print(f'\nScan-related debug.log lines ({len(scan_lines)}):')
        for line in scan_lines[-30:]:
            print(f'  {line}')
        print(f'\nFull log last 20 lines:')
        for line in content.splitlines()[-20:]:
            print(f'  {line}')
    else:
        print(f'\ndebug.log does not exist at {debug_log}')
    
    # Now browse
    result = client.soap_browse('0')
    print(f'\nBrowse: NumberReturned={result["NumberReturned"]}, TotalMatches={result["TotalMatches"]}')
    uid = client.soap_get_system_update_id()
    print(f'SystemUpdateID: {uid}')

    proc.terminate()
    time.sleep(1)
else:
    proc.wait(timeout=5)
    stderr = proc.stderr.read().decode('utf-8', errors='replace') if proc.stderr else ''
    stdout = proc.stdout.read().decode('utf-8', errors='replace') if proc.stdout else ''
    print(f'Stdout: {stdout}')
    print(f'Stderr: {stderr}')

_teardown_server(proc, old_config, config_ini)
httpd.shutdown()
shutil.rmtree(tmp)
