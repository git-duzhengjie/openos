#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""M1.4 TCP 三次握手自检用 —— host 侧最简 echo server。
QEMU user 网络下，guest 发往 10.0.2.2:8888 的连接经 guestfwd 转到本机 127.0.0.1:PORT。
server 只要接受连接（回 SYN-ACK 由内核完成）即可让 guest 三次握手进入 ESTABLISHED。
"""
import socket
import sys
import threading

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8888


def handle(conn, addr):
    try:
        conn.settimeout(5)
        data = conn.recv(4096)
        if data:
            conn.sendall(data)  # echo
    except Exception:
        pass
    finally:
        conn.close()


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(8)
    print(f"[tcp-echo] listening 127.0.0.1:{PORT}", flush=True)
    while True:
        try:
            conn, addr = srv.accept()
            print(f"[tcp-echo] accepted {addr}", flush=True)
            threading.Thread(target=handle, args=(conn, addr), daemon=True).start()
        except Exception as e:
            print(f"[tcp-echo] err {e}", flush=True)
            break


if __name__ == "__main__":
    main()
