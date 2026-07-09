#!/bin/bash
cd /mnt/e/openos
tr -d '\000\r' <logs/usb_trace.log >/tmp/t.log
echo '=== 每个 epid4 xfer_start 前最近的 fetch_trb ==='
awk '/fetch_trb/{last=$0} /xfer_start.*slotid 1, epid 4/{print "epid4start <- last_fetch: " last}' /tmp/t.log | head -8
echo '=== xfer_start epid4 前 3 行含 fetch (首个) ==='
ln=$(grep -n 'xfer_start.*slotid 1, epid 4' /tmp/t.log | head -1 | cut -d: -f1)
awk -v n=$ln 'NR>=n-4 && NR<=n+1' /tmp/t.log
