#!/usr/bin/env python3
"""B1 capture proxy: a transparent TCP forwarder that logs every byte exchanged.

Listens on a chosen port and forwards to the dlna-server on localhost:19876.
Each client connection is logged as a labeled session with a unique id,
plus a timestamped dump of every chunk read from either side.

The goal is to be wire-transparent (the byte stream is faithfully forwarded
in both directions) so the client behaviour is unchanged, while giving us
a precise, replayable record of what was actually sent on the wire.

Usage:
    python3 b1_capture_proxy.py --listen 19877 --forward 127.0.0.1:19876 \
        --out b1-capture.log
"""

import argparse
import datetime
import socket
import threading
import os


def log_line(log_fp, msg):
    line = "[%s] %s\n" % (datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3], msg)
    log_fp.write(line)
    log_fp.flush()


def hexdump(data):
    return " ".join("%02x" % b for b in data)


def pipe(src_sock, dst_sock, label, log_fp, session_id, idle_timeout=0.5):
    try:
        while True:
            chunk = src_sock.recv(4096)
            if not chunk:
                log_line(log_fp, "session=%d %s EOF" % (session_id, label))
                break
            log_line(log_fp, "session=%d %s %d bytes: %s" % (
                session_id, label, len(chunk), hexdump(chunk[:64]) + ("..." if len(chunk) > 64 else "")))
            dst_sock.sendall(chunk)
    except (ConnectionResetError, BrokenPipeError, OSError) as e:
        log_line(log_fp, "session=%d %s closed: %s" % (session_id, label, e))
    finally:
        try:
            dst_sock.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def handle_client(client_sock, client_addr, forward_host, forward_port, log_fp, session_counter):
    session_id = next(session_counter)
    log_line(log_fp, "session=%d ACCEPT from %s:%d" % (session_id, client_addr[0], client_addr[1]))
    upstream = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        upstream.connect((forward_host, forward_port))
        log_line(log_fp, "session=%d UPSTREAM connected to %s:%d" % (session_id, forward_host, forward_port))
    except OSError as e:
        log_line(log_fp, "session=%d UPSTREAM connect failed: %s" % (session_id, e))
        client_sock.close()
        return
    t1 = threading.Thread(target=pipe, args=(client_sock, upstream, "C->S", log_fp, session_id), daemon=True)
    t2 = threading.Thread(target=pipe, args=(upstream, client_sock, "S->C", log_fp, session_id), daemon=True)
    t1.start()
    t2.start()
    t1.join()
    t2.join()
    try:
        client_sock.close()
    except OSError:
        pass
    try:
        upstream.close()
    except OSError:
        pass
    log_line(log_fp, "session=%d CLOSED" % session_id)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--listen", type=int, default=19877)
    p.add_argument("--forward", default="127.0.0.1:19876")
    p.add_argument("--out", default="b1-capture.log")
    args = p.parse_args()

    fwd_host, fwd_port = args.forward.split(":")
    fwd_port = int(fwd_port)

    log_fp = open(args.out, "a", buffering=1)
    log_line(log_fp, "==== b1_capture_proxy STARTED listen=%d forward=%s:%d pid=%d ====" % (
        args.listen, fwd_host, fwd_port, os.getpid()))

    counter = iter(range(1, 10_000_000))

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.listen))
    srv.listen(64)
    log_line(log_fp, "==== LISTENING on 0.0.0.0:%d ====" % args.listen)

    try:
        while True:
            client_sock, client_addr = srv.accept()
            threading.Thread(
                target=handle_client,
                args=(client_sock, client_addr, fwd_host, fwd_port, log_fp, counter),
                daemon=True,
            ).start()
    except KeyboardInterrupt:
        log_line(log_fp, "==== b1_capture_proxy STOPPED (KeyboardInterrupt) ====")
    finally:
        srv.close()
        log_fp.close()


if __name__ == "__main__":
    main()
