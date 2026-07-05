#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Minimal pcap dissector: count ARP / ICMP / UDP(DHCP/DNS) / TCP(flags) frames.
No external deps. Used to prove M1 net selftest frames really appear on the wire.
"""
import struct
import sys


def main(path):
    with open(path, "rb") as f:
        data = f.read()
    # global header 24 bytes
    magic = data[:4]
    le = magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1")
    endian = "<" if le else ">"
    off = 24
    n = 0
    stats = {"ARP": 0, "ICMP": 0, "DHCP": 0, "DNS": 0, "UDP": 0,
             "TCP-SYN": 0, "TCP-SYNACK": 0, "TCP-ACK": 0, "TCP-other": 0, "OTHER": 0}
    while off + 16 <= len(data):
        ts_s, ts_us, incl, orig = struct.unpack(endian + "IIII", data[off:off+16])
        off += 16
        pkt = data[off:off+incl]
        off += incl
        if len(pkt) < 14:
            continue
        n += 1
        eth_type = struct.unpack(">H", pkt[12:14])[0]
        if eth_type == 0x0806:
            stats["ARP"] += 1
            continue
        if eth_type != 0x0800:
            stats["OTHER"] += 1
            continue
        ihl = (pkt[14] & 0x0f) * 4
        proto = pkt[23]
        l4 = 14 + ihl
        if proto == 1:
            stats["ICMP"] += 1
        elif proto == 17:
            if l4 + 4 <= len(pkt):
                sport, dport = struct.unpack(">HH", pkt[l4:l4+4])
                if 67 in (sport, dport) or 68 in (sport, dport):
                    stats["DHCP"] += 1
                elif 53 in (sport, dport):
                    stats["DNS"] += 1
                else:
                    stats["UDP"] += 1
            else:
                stats["UDP"] += 1
        elif proto == 6:
            if l4 + 14 <= len(pkt):
                flags = pkt[l4 + 13]
                syn = flags & 0x02
                ack = flags & 0x10
                if syn and ack:
                    stats["TCP-SYNACK"] += 1
                elif syn:
                    stats["TCP-SYN"] += 1
                elif ack:
                    stats["TCP-ACK"] += 1
                else:
                    stats["TCP-other"] += 1
            else:
                stats["TCP-other"] += 1
        else:
            stats["OTHER"] += 1
    print(f"total frames: {n}")
    for k, v in stats.items():
        if v:
            print(f"  {k:12s}: {v}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "logs/net.pcap")
