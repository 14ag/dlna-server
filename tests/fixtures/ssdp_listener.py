import socket
import struct
import threading


SSDP_ADDR = "239.255.255.250"
SSDP_PORT = 1900


class SsdpListener:
    def __init__(self, timeout=5.0):
        self.timeout = timeout
        self._sock = None
        self._thread = None
        self._alive = threading.Event()
        self._running = False
        self._joined = False

    def start(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM,
                                   socket.IPPROTO_UDP)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except AttributeError:
            pass
        self._sock.bind(("", SSDP_PORT))
        mreq = struct.pack("4sl", socket.inet_aton(SSDP_ADDR),
                           socket.INADDR_ANY)
        try:
            self._sock.setsockopt(socket.IPPROTO_IP,
                                  socket.IP_ADD_MEMBERSHIP, mreq)
            self._joined = True
        except OSError:
            self._joined = False
        self._sock.settimeout(1.0)
        self._running = True
        self._thread = threading.Thread(target=self._listen, daemon=True)
        self._thread.start()

    def _listen(self):
        while self._running:
            try:
                data, _ = self._sock.recvfrom(65535)
                if b"ssdp:alive" in data:
                    self._alive.set()
            except socket.timeout:
                continue
            except OSError:
                break

    def wait_for_alive(self, timeout=3.0):
        return self._alive.wait(timeout)

    @property
    def has_multicast(self):
        return self._joined

    def stop(self):
        self._running = False
        if self._sock:
            try:
                mreq = struct.pack("4sl", socket.inet_aton(SSDP_ADDR),
                                   socket.INADDR_ANY)
                self._sock.setsockopt(socket.IPPROTO_IP,
                                      socket.IP_DROP_MEMBERSHIP, mreq)
            except OSError:
                pass
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        if self._thread:
            self._thread.join(timeout=2)
