#!/usr/bin/env bash
cd /mnt/e/openos
for i in 1 2 4 5; do
  f="build/log/ser_f1_r$i.clean.txt"
  echo "=== run$i ==="
  grep -E 'wait-multi|waitpid|A2\.P5|reap|bogus' "$f" | tail -20
done
