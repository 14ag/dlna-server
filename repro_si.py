import subprocess, os, time, socket
env = dict(os.environ)
env["XDG_CONFIG_HOME"] = "/tmp/si3/config"
env["HOME"] = "/tmp/si3/config"
env["XDG_RUNTIME_DIR"] = "/tmp/si3/run"
env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
BIN = "/mnt/c/Users/philip/sauce/dlna-server/output/linux/dlna-server"
a = subprocess.Popen([BIN, "--debug", "--source", "/tmp/si3/media"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
sock = "/tmp/dlna-server-1000/dlna-server.sock"
for _ in range(100):
    if os.path.exists(sock):
        break
    if a.poll() is not None:
        print("A exited early rc=", a.returncode)
        raise SystemExit(1)
    time.sleep(0.1)
print("socket ready")
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
s.sendall(b"kill\n")
s.close()
print("kill sent")
rc = a.wait(timeout=10)
print("A_EXIT=", rc)
