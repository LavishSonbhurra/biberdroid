#!/usr/bin/env python3
"""Runs a command with a pseudo terminal as its standard input, output and error,
and prints what it wrote (scripts/warm-approot.sh: output to a terminal)."""
import os
import pty
import sys

pid, fd = pty.fork()
if pid == 0:
    os.execvp(sys.argv[1], sys.argv[1:])
out = b""
while True:
    try:
        data = os.read(fd, 4096)
    except OSError:
        break
    if not data:
        break
    out += data
os.waitpid(pid, 0)
sys.stdout.write(out.decode(errors="replace").replace("\r\n", "\n"))
