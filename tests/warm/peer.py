#!/usr/bin/env python3
"""Misbehaving peers for the warm process checks (scripts/warm-peers-approot.sh).

  peer.py client KIND ADDRESS    a client that is not a run; prints what the warm process did
  peer.py server KIND ADDRESS    something else listening where a warm process should be

ADDRESS is @NAME, a socket in the abstract namespace.
"""
import array
import os
import socket
import struct
import sys
import time

MAGIC = 0x31575242


def address(text):
    return "\0" + text[1:]


def closed_within(sock, seconds):
    """Seconds until the other side closed the connection, or None."""
    sock.settimeout(seconds)
    start = time.monotonic()
    try:
        while True:
            if sock.recv(4096) == b"":
                return time.monotonic() - start
    except socket.timeout:
        return None
    except OSError:
        return time.monotonic() - start


def client(kind, where):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(address(where))
    if kind == "garbage":
        s.sendall(os.urandom(64))
    elif kind == "huge":
        s.sendall(struct.pack("<II", MAGIC, 1 << 30))
    elif kind == "slow":
        s.sendall(struct.pack("<II", MAGIC, 4096)[:3])
    elif kind == "half":
        s.sendall(struct.pack("<II", MAGIC, 4096) + b"x" * 100)
    elif kind == "descriptors":
        fds = [os.open("/dev/null", os.O_RDONLY) for _ in range(8)]
        s.sendmsg([struct.pack("<II", MAGIC, 4096)],
                  [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))])
    elif kind == "nothing-in-it":
        fds = [os.open("/dev/null", os.O_RDWR) for _ in range(4)]
        body = b"\0" * 4096
        s.sendmsg([struct.pack("<II", MAGIC, len(body))],
                  [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))])
        s.sendall(body)
    elif kind == "crowd":
        others = []
        for _ in range(40):
            o = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            o.setblocking(False)
            try:
                o.connect(address(where))
            except OSError:
                pass
            others.append(o)
        time.sleep(4)
        print("crowd done")
        return
    elif kind != "silent":
        raise SystemExit("unknown kind")
    took = closed_within(s, 6)
    print("%s: %s" % (kind, "still open" if took is None else "closed after %.1f s" % took))


def server(kind, where):
    """Listens for six seconds, then ends."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(address(where))
    s.listen(8)
    print("listening", flush=True)
    kept = []
    end = time.monotonic() + 6
    while time.monotonic() < end:
        s.settimeout(max(0.1, end - time.monotonic()))
        try:
            c, _ = s.accept()
        except socket.timeout:
            break
        if kind == "garbage":
            c.recv(65536)
            c.sendall(b"Zzz")
            c.close()
        else:
            # silent: the connection is kept and never answered
            kept.append(c)


if __name__ == "__main__":
    {"client": client, "server": server}[sys.argv[1]](sys.argv[2], sys.argv[3])
