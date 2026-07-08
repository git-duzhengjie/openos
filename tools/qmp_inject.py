#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
QMP USB-HID 注入验证器
------------------------------------------------
连接 QEMU 的 QMP TCP 端口，注入鼠标/键盘输入事件，
用于在 headless 环境下验证 xHCI 中断 IN 端点（epid 3）能否
真正产生数据传输（配合 -trace usb_xhci_xfer_success 抓包）。

用法:
    python qmp_inject.py --host 127.0.0.1 --port 4444 [--wait 6]

注入序列:
    1. 等待 guest 完成 xHCI 枚举（--wait 秒）
    2. 注入鼠标相对移动 (btn 抖动 + 多次 rel 移动)
    3. 注入键盘按键序列 (a / b / enter)
    4. 每步之间留间隔，让中断端点有机会上报
"""
import argparse
import json
import socket
import sys
import time


class QmpClient:
    """极简 QMP 客户端：负责握手 + 命令收发。"""

    def __init__(self, host: str, port: int, timeout: float = 10.0):
        self._host = host
        self._port = port
        self._timeout = timeout
        self._sock = None
        self._buf = b""

    def connect(self, retries: int = 30, delay: float = 1.0) -> None:
        last_err = None
        for _ in range(retries):
            try:
                self._sock = socket.create_connection(
                    (self._host, self._port), timeout=self._timeout
                )
                self._sock.settimeout(self._timeout)
                break
            except OSError as err:
                last_err = err
                time.sleep(delay)
        else:
            raise RuntimeError(f"无法连接 QMP {self._host}:{self._port}: {last_err}")
        # 读取欢迎报文
        self._read_json()
        # 进入命令模式
        self._send({"execute": "qmp_capabilities"})
        self._read_json()
        print("[qmp] capabilities negotiated", flush=True)

    def _send(self, obj: dict) -> None:
        data = (json.dumps(obj) + "\r\n").encode("utf-8")
        self._sock.sendall(data)

    def _read_json(self) -> dict:
        while True:
            idx = self._buf.find(b"\n")
            if idx >= 0:
                line = self._buf[:idx]
                self._buf = self._buf[idx + 1:]
                line = line.strip()
                if not line:
                    continue
                try:
                    return json.loads(line.decode("utf-8"))
                except json.JSONDecodeError:
                    continue
            chunk = self._sock.recv(4096)
            if not chunk:
                raise RuntimeError("QMP 连接被关闭")
            self._buf += chunk

    def execute(self, command: str, arguments: dict = None) -> dict:
        req = {"execute": command}
        if arguments:
            req["arguments"] = arguments
        self._send(req)
        # 跳过异步事件，等到 return / error
        while True:
            msg = self._read_json()
            if "return" in msg or "error" in msg:
                return msg

    def close(self) -> None:
        if self._sock:
            self._sock.close()
            self._sock = None


def inject_mouse(qmp: QmpClient) -> None:
    """注入鼠标相对移动 + 按键抖动。"""
    print("[qmp] injecting mouse events ...", flush=True)
    for i in range(8):
        events = [
            {"type": "rel", "data": {"axis": "x", "value": 12}},
            {"type": "rel", "data": {"axis": "y", "value": 8}},
        ]
        qmp.execute("input-send-event", {"events": events})
        time.sleep(0.15)
    # 左键按下 + 抬起
    qmp.execute("input-send-event", {"events": [
        {"type": "btn", "data": {"button": "left", "down": True}}]})
    time.sleep(0.1)
    qmp.execute("input-send-event", {"events": [
        {"type": "btn", "data": {"button": "left", "down": False}}]})
    print("[qmp] mouse events done", flush=True)


def inject_keyboard(qmp: QmpClient) -> None:
    """注入键盘按键序列。"""
    print("[qmp] injecting keyboard events ...", flush=True)
    for key in ["a", "b", "c", "ret"]:
        qmp.execute("input-send-event", {"events": [
            {"type": "key", "data": {"key": {"type": "qcode", "data": key},
                                     "down": True}}]})
        time.sleep(0.08)
        qmp.execute("input-send-event", {"events": [
            {"type": "key", "data": {"key": {"type": "qcode", "data": key},
                                     "down": False}}]})
        time.sleep(0.12)
    print("[qmp] keyboard events done", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="QMP USB-HID 注入验证器")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4444)
    parser.add_argument("--wait", type=float, default=6.0,
                        help="注入前等待 guest 枚举完成的秒数")
    parser.add_argument("--rounds", type=int, default=3,
                        help="鼠标+键盘注入循环轮数")
    args = parser.parse_args()

    qmp = QmpClient(args.host, args.port)
    try:
        qmp.connect()
    except RuntimeError as err:
        print(f"[qmp] ERROR: {err}", file=sys.stderr, flush=True)
        return 2

    print(f"[qmp] waiting {args.wait}s for guest enumeration ...", flush=True)
    time.sleep(args.wait)

    for r in range(args.rounds):
        print(f"[qmp] === round {r + 1}/{args.rounds} ===", flush=True)
        inject_mouse(qmp)
        inject_keyboard(qmp)
        time.sleep(0.5)

    print("[qmp] all events injected; querying status", flush=True)
    st = qmp.execute("query-status")
    print(f"[qmp] status = {st.get('return')}", flush=True)
    qmp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
