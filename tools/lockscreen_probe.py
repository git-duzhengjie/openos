#!/usr/bin/env python3
# 锁屏输入诊断：headless 起 QEMU + QMP，等系统进锁屏后注入按键，抓串口 [lock-dbg]
import socket, json, subprocess, time, os, sys, threading

ROOT = r"E:\openos"
QEMU = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
OVMF_CODE = r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"
SER = ROOT + r"\logs\qprobe.ser"
QMP_PORT = 4445

os.makedirs(ROOT + r"\logs", exist_ok=True)
if os.path.exists(SER):
    os.remove(SER)

# OVMF code path fallback
if not os.path.exists(OVMF_CODE):
    for c in [r"C:\Program Files\qemu\share\OVMF_CODE.fd",
              r"C:\Program Files\qemu\share\edk2-i386-code.fd"]:
        if os.path.exists(c):
            OVMF_CODE = c; break

def winpath(p):
    # /mnt/e/openos -> E:\openos
    if p.startswith("/mnt/"):
        d = p[5]
        return d.upper() + ":" + p[6:].replace("/", "\\")
    return p

args = [
    QEMU,
    "-machine", "pc", "-cpu", "qemu64", "-smp", "2", "-m", "512",
    "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=" + winpath(OVMF_CODE),
    "-drive", "if=pflash,format=raw,unit=1,file=" + winpath(ROOT + "/target/OVMF_VARS.fd"),
    "-drive", "file=" + winpath(ROOT + "/target/openos-uefi.img") + ",format=raw,media=disk,if=ide,index=0",
    "-drive", "file=" + winpath(ROOT + "/target/openos-data.img") + ",format=raw,media=disk,if=ide,index=2",
    "-drive", "file=" + winpath(ROOT + "/target/openos-fat.img") + ",format=raw,media=disk,if=ide,index=3",
    "-device", "ich9-ahci,id=ahci0",
    "-drive", "file=" + winpath(ROOT + "/target/openos-ahci.img") + ",format=raw,if=none,id=sata0",
    "-device", "ide-hd,drive=sata0,bus=ahci0.0",
    "-drive", "file=" + winpath(ROOT + "/target/openos-nvme.img") + ",format=raw,if=none,id=nvm0",
    "-device", "nvme,serial=openos-nvme0,id=nvme0", "-device", "nvme-ns,drive=nvm0,bus=nvme0",
    "-device", "qemu-xhci,id=xhci0",
    "-device", "usb-kbd,bus=xhci0.0",
    "-device", "usb-mouse,bus=xhci0.0",
    "-serial", "file:" + winpath(SER),
    "-display", "none",
    "-qmp", "tcp:127.0.0.1:%d,server,nowait" % QMP_PORT,
    "-d", "guest_errors,trace:usb_xhci_ep_kick,trace:usb_xhci_xfer_start,trace:usb_xhci_xfer_success,trace:usb_xhci_slot_address,trace:usb_hid_kbd_queue,trace:usb_kbd_queue_full,trace:usb_hid_set_idle,trace:usb_hid_get_report",
    "-D", winpath(ROOT + r"\logs\qtrace.log"),
    "-no-reboot", "-no-shutdown",
]

print("[probe] launching QEMU headless with QMP...")
print("[probe] QEMU exe exists:", os.path.exists(QEMU))
for a in args:
    if a.startswith("file=") or ("file=" in a and "pflash" in a):
        pass
ERRLOG = ROOT + r"\logs\qprobe.err"
errf = open(ERRLOG, "wb")
proc = subprocess.Popen(args, stderr=errf, stdout=errf)

def qmp_connect():
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", QMP_PORT), timeout=1)
            return s
        except OSError:
            time.sleep(0.2)
    return None

s = qmp_connect()
if not s:
    print("[probe] QMP connect FAILED")
    errf.flush()
    try:
        with open(ERRLOG,"rb") as eh:
            print("[probe] QEMU stderr:\n" + eh.read().decode("utf-8","replace")[:2000])
    except Exception as e:
        print("[probe] stderr read err:", e)
    print("[probe] proc.poll()=", proc.poll())
    proc.kill(); sys.exit(1)

f = s.makefile("rwb")
def recv():
    line = f.readline()
    return json.loads(line) if line else None
def recv_return():
    # 跳过异步 event，只等 return/error 应答
    for _ in range(20):
        m = recv()
        if m is None: return None
        if "return" in m or "error" in m: return m
    return None
def send(cmd):
    f.write((json.dumps(cmd) + "\n").encode()); f.flush()

recv()  # greeting
send({"execute": "qmp_capabilities"}); recv_return()
print("[probe] QMP ready. waiting 45s for boot -> lockscreen...")
time.sleep(45)

# 注入按键序列：多轮 a/b/c + 回车，每个键 down+up
keys = ["a", "b", "c", "a", "b", "c", "ret"]
for rnd in range(3):
    for k in keys:
        send({"execute": "send-key",
              "arguments": {"keys": [{"type": "qcode", "data": k}]}})
        r = recv_return()
        print("[probe] send-key %s -> %s" % (k, r))
        time.sleep(0.3)

time.sleep(2)
send({"execute": "quit"})
time.sleep(1)
proc.kill()

# 分析串口
print("\n[probe] ===== serial [lock-dbg] / kbd lines =====")
try:
    with open(SER, "rb") as fh:
        data = fh.read().replace(b"\x00", b"")
    for ln in data.decode("utf-8", "replace").splitlines():
        if any(x in ln for x in ["lock-dbg", "lockscreen", "usb-hid", "kbd", "keyboard", "xhci"]):
            print("  " + ln)
except Exception as e:
    print("  read err:", e)
print("[probe] done. full log:", SER)
