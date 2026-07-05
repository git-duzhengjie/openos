#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stdio echo for QEMU guestfwd cmd mode.
QEMU accepts the guest TCP connection inside SLIRP (completes 3-way handshake),
then pipes the byte stream to this process's stdin/stdout. We just echo back.
"""
import sys


def main():
    inb = sys.stdin.buffer
    outb = sys.stdout.buffer
    while True:
        chunk = inb.read(1)
        if not chunk:
            break
        outb.write(chunk)
        outb.flush()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
