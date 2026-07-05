import struct, sys
d = open(sys.argv[1] if len(sys.argv) > 1 else 'logs/net.pcap', 'rb').read()
off = 24
n = 0
while off + 16 <= len(d):
    ts, tu, incl, orig = struct.unpack('<IIII', d[off:off+16])
    off += 16
    pkt = d[off:off+incl]
    off += incl
    n += 1
    if len(pkt) < 14:
        print('%3d len=%4d (short)' % (n, len(pkt)))
        continue
    eth = struct.unpack('>H', pkt[12:14])[0]
    info = 'eth=0x%04x' % eth
    if eth == 0x0806:
        info = 'ARP'
    elif eth == 0x0800 and len(pkt) >= 34:
        proto = pkt[23]
        src = '.'.join(str(x) for x in pkt[26:30])
        dst = '.'.join(str(x) for x in pkt[30:34])
        if proto == 1:
            info = 'ICMP %s->%s type=%d' % (src, dst, pkt[34])
        elif proto == 17:
            sp = struct.unpack('>H', pkt[34:36])[0]
            dp = struct.unpack('>H', pkt[36:38])[0]
            info = 'UDP %s:%d->%s:%d' % (src, sp, dst, dp)
        else:
            info = 'IP proto=%d %s->%s' % (proto, src, dst)
    print('%3d len=%4d %s' % (n, len(pkt), info))
print('TOTAL', n)
