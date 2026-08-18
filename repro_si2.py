import subprocess, os, time, socket, shutil
env = dict(os.environ)
env["XDG_CONFIG_HOME"] = "/tmp/si4/config"
env["HOME"] = "/tmp/si4/config"
env["XDG_RUNTIME_DIR"] = "/tmp/si4/run"
env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
BIN = "/mnt/c/Users/philip/sauce/dlna-server/output/linux/dlna-server"
shutil.rmtree("/tmp/si4", ignore_errors=True)
os.makedirs("/tmp/si4/config/media", exist_ok=True)
os.makedirs("/tmp/si4/run", exist_ok=True)
os.system("rm -rf /tmp/dlna-server-1000")
a = subprocess.Popen([BIN, "--debug", "--source", "/tmp/si4/config/media"],
                     stdout=open("/tmp/si4/a.log","w"), stderr=subprocess.STDOUT, env=env)
sock = "/tmp/dlna-server-1000/dlna-server.sock"
for _ in range(100):
    if os.path.exists(sock):
        break
    if a.poll() is not None:
        print("A exited early rc=", a.returncode)
        raise SystemExit(1)
    time.sleep(0.1)
print("socket ready")
start = time.time()
b = subprocess.run([BIN, "--no-debug", "--source", "/tmp/si4/config/media"],
                   capture_output=True, text=True, timeout=10, env=env)
elapsed = time.time() - start
print("B rc=", b.returncode, "elapsed=", round(elapsed,2))
print("B stdout=", b.stdout[:200])
print("B stderr=", b.stderr[:200])
try:
    rc = a.wait(timeout=10)
    print("A_EXIT=", rc)
except subprocess.TimeoutExpired:
    print("A did not exit in 10s")
    a.kill()
print("---A LOG TAIL---")
print(open("/tmp/si4/a.log").read()[-1500:])
