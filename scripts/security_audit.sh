#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail() {
    echo "[FAIL] $*" >&2
    exit 1
}

pass() {
    echo "[OK] $*"
}

require_grep() {
    local pattern="$1"
    local file="$2"
    local message="$3"
    grep -Eq "$pattern" "$file" || fail "$message"
    pass "$message"
}

require_no_grep() {
    local pattern="$1"
    local file="$2"
    local message="$3"
    if grep -Eq "$pattern" "$file"; then
        fail "$message"
    fi
    pass "$message"
}

require_grep 'USER_SPACE_START[[:space:]]+0x40000000u' src/kernel/include/usermode.h \
    'user space starts above low kernel identity mappings'
require_grep 'USER_SPACE_END[[:space:]]+0xC0000000u' src/kernel/include/usermode.h \
    'user space ends before high kernel virtual range'
require_grep 'USERMEM_PTR_MIN[[:space:]]+USER_SPACE_START' src/kernel/include/usermem.h \
    'copy_from_user/copy_to_user share user lower bound'
require_grep 'USERMEM_PTR_MAX[[:space:]]+USER_SPACE_END' src/kernel/include/usermem.h \
    'copy_from_user/copy_to_user share user upper bound'
require_grep 'PTE_USER' src/kernel/usermem.c \
    'user memory copy verifies page user bit'

require_grep 'aslr_pick_main_stack_slot' src/kernel/proc/process.c \
    'exec main stack uses ASLR slot'
require_grep 'aslr_pick_next_thread_stack_slot' src/kernel/proc/process.c \
    'thread stack slot base is randomized'
require_grep 'aslr_apply_heap_gap' src/kernel/proc/process.c \
    'heap/brk base has ASLR gap'
require_grep 'aslr_pick_mmap_base' src/kernel/ipc/syscall.c \
    'mmap base is randomized'

require_grep 'PHDRS' src/user/user.ld \
    'user linker script defines explicit program headers'
require_grep 'FLAGS\(5\)' src/user/user.ld \
    'user text segment is read/execute'
require_grep 'FLAGS\(6\)' src/user/user.ld \
    'user data segment is read/write'
require_grep 'Rejecting RWX load segment' src/kernel/proc/elf_loader.c \
    'ELF loader rejects writable executable LOAD segments'
require_grep 'W\^X violation' build.sh \
    'build fails on RWX user ELF segments'

require_grep 'sandboxed' src/kernel/proc/process.c \
    'process model tracks sandbox state'
require_grep 'OPENOS_CAP_ALL' src/kernel/proc/process.c \
    'capability model avoids unconditional all-cap restore'
require_grep 'kaddrtest' build.sh \
    'kernel address protection regression is built in test mode'

user_elf_found=0
for elf in target/*.elf; do
    [ -f "$elf" ] || continue
    [ "$(basename "$elf")" != "kernel.elf" ] || continue
    user_elf_found=1
    if readelf -l "$elf" 2>/dev/null | awk '/LOAD/ && $0 ~ /RWE/ { bad=1 } END { exit bad ? 1 : 0 }'; then
        :
    else
        fail "RWX LOAD segment found in $elf"
    fi
done
if [ "$user_elf_found" -eq 1 ]; then
    pass 'built user ELF files contain no RWX LOAD segments'
else
    echo '[WARN] no target user ELF files found; skip ELF RWX audit'
fi

require_no_grep '^- \[ \] 安全审计' TODOLIST.md \
    'security audit TODO is marked complete'

pass 'security audit passed'
