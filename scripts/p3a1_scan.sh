#!/usr/bin/env bash
cd "$(dirname "$0")/.."
echo "===== iret frame construction sites ====="
grep -rn 'USER_CODE\|user_code_selector\|user_data_selector\|GDT_USER' \
  src/arch/x86_64/kernel/ 2>/dev/null | grep -v selftest | head -40
echo
echo "===== spawn / initial iret frame ====="
grep -rn 'spawn_uthread\|make_user_ctx\|iret_frame\|initial.*rip\|ring3.*enter' \
  src/arch/x86_64/kernel/ 2>/dev/null | head -30
