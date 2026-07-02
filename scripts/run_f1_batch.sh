#!/usr/bin/env bash
# γ.5-F1 batch smoke: run QEMU N times, count regressions
set +e
cd /mnt/e/openos
N=${N:-3}
for i in $(seq 1 $N); do
  timeout --preserve-status --kill-after=3s 40s qemu-system-x86_64 \
    -M q35 -m 512 -smp 3 \
    -bios /usr/share/qemu/OVMF.fd \
    -drive format=raw,file=target/openos-uefi.img,if=ide \
    -serial file:build/log/ser_f1_r$i.txt \
    -display none -no-reboot -no-shutdown >/dev/null 2>&1
  tr -d '\000' <build/log/ser_f1_r$i.txt | sed 's/\x1b\[[0-9;=]*[a-zA-Z]//g' >build/log/ser_f1_r$i.clean.txt
  gp=$(grep -c 'vector=0x000000000000000D' build/log/ser_f1_r$i.clean.txt)
  wm=$(grep -c 'wait-multi.*got pid' build/log/ser_f1_r$i.clean.txt)
  wp=$(grep -c 'waitpid.*exit=' build/log/ser_f1_r$i.clean.txt)
  pass=$(grep -c 'A2.P5.*PASS' build/log/ser_f1_r$i.clean.txt)
  tick=$(grep 'tick_hits per-CPU' build/log/ser_f1_r$i.clean.txt | tail -1 | tr -d '\r' | sed 's/^.*tick_hits/tick_hits/')
  echo "run$i: gp=$gp wm_reap=$wm wp_reap=$wp PASS=$pass"
  echo "        $tick"
done
