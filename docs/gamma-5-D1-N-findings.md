# γ.5-D1 / N — sporadic GP root cause report

Status: **root cause locked, fix scheduled as γ.5-P3**
Date: 2026-07-03

## TL;DR

Every `#GP(err=0x30)` that shows up during smp-selftest / hello_fork
runs is triggered by **the timer IRQ preempting user (ring-3) code,
followed by an `iretq` back to ring-3 whose iret-frame has been
corrupted by the scheduler round-trip** (the ring-3 preempt-return
path is not yet implemented). `err=0x30` decodes to
`index=6, TI=0, IDT=0, EXT=0`, i.e. **GDT[6] = TSS selector**, which
is what `iretq` loads as SS when it fails to restore a valid
`(CS,SS)` pair from the ring-3 frame.

The bug is **not** related to:

- kernel-stack under/overflow (invalidated by γ.5-D1 A canary + H1
  16 KiB stacks)
- BSP-only slot leaks (BSP is the main victim, but AP is affected too)
- TSS descriptor / GDT[6] corruption (10/10 runs show `base`,
  `limit`, `type` bit-identical)
- `slot table` race in scheduler (γ.5-D1 J probe stayed 40 % lethal
  after adding the anti-race guard)

The bug **is**:

- ring-3 preempted by LAPIC timer
- context switch happens in the timer ISR
- when the outgoing task is later resumed, its iret-frame has been
  touched (SS field ends up as 0x30, RIP/CS may also be stale)
- CPU faults on `iretq` -> `#GP(err=0x30)`
- if not caught in time, the next timer trips a `#DF (vec=8)` on a
  half-torn TSS/rsp0 combination

## Evidence chain

### 1. `err=0x30` decode

```
err = 0x30 = 0b0011_0000
             |||_ EXT = 0        (not external)
             ||__ IDT = 0        (not IDT gate)
             |___ TI  = 0        (GDT, not LDT)
   index = 0x30 >> 3 = 6         (GDT[6])
```

`GDT[6]` in this project is `OPENOS_X86_64_GDT_TSS` (see
`src/arch/x86_64/include/gdt64.h`). Selector value = `6<<3 = 0x30`.

### 2. TSS descriptor is intact

Across 10 lethal runs `.base = 0xFFFFFFFF801771E8`,
`descriptor bytes = 0x80008B1771E80067` — bit identical, so it is
**not** the descriptor / GDT slot that is corrupted.

### 3. `tss.rsp0` observations were a red herring

`0x8016F180` was decoded (via `nm -n`) to sit **inside** the
`g_rsp0_stack` area — it is a *valid* rsp0 sample for a ring-0 timer
preempt of ring-0 idle. The two clusters we saw
(`0x9001E008` and `0x8016F180`) are just the two different rsp0
baselines for BSP / AP. Nothing was written wrong here.

### 4. `hello_fork.c:116` had a prior in-code note

```
/* trying to add a spin loop to catch tick_hits.u revealed that a
 * ring-3 preempt through iret always triggers #GP(err=0x30) with
 * RIP in irq0_iret; the ring-3 preempt-return path is unimplemented.
 * this is γ.5 P3 work. */
```

An earlier session had already root-caused this. γ.5-D1 N
independently re-derived it via err-code decoding, TSS geometry
verification, and batch statistics.

### 5. `C` step — kill the deliberate ring-3 spin, batch of 25

`smp_selftest64.c` stage 30/31 (`G.7f` ring-3 LAPIC preemption verify)
was temporarily gated off with `if (0 && ap_n >= 3u)`.

| metric | value |
|---|---|
| GP hits | **7 / 25** (28 %) — still non-zero |
| runs with `tick_hits.u == 0` (cpu1) | 17, GP = 0 |
| runs with `tick_hits.u >= 1` (cpu1) | 7, GP = **7** (100 %) |

**GP ⇔ tick_hits.u ≥ 1** — a perfect 1:1 correlation between
`timer-preempt-ring3` and `#GP(0x30)`. The GP-source is *any*
ring-3 window (hello64, hello_fork children, launcher). Removing the
deliberate spin does not remove the bug; removing user code entirely
would.

## Invariants used in P3 design

- `err=0x30` is the signature — regressions must monitor for it
- selftest `tick_hits.u_cpu1` ≥ 1 must **not** implicate a GP
- `hello_fork` `wm_reap=3` and `wp_reap≥1` must survive 25 runs
- keep D1 debug scaffolding (`j_probe`, `tally_batch`,
  `A-canary`) available under `#ifdef` so we can regress against
  it during P3

## D1 deliverables that stay in tree

- `src/arch/x86_64/kernel/j_probe.{c,h}` — ring buffer + dump
- `sched64.c` A-canary bytes
- `sched64.h` `KSTACK_BYTES=16384` (H1)
- `lapic64.c` timer_handler_full `(cs, rip, rsp)` snapshot
- `isr64.S` timer stub 3-arg passing
- `idt64.c` GP handler tail `j_probe_dump`
- `scripts/run_f1_batch.sh` + `scripts/tally_batch.sh`
- this file

## Next: γ.5-P3

Design outline (to be expanded in a separate doc):

1. reserve `slot.saved_frame` (5-word iret frame + 15 GPR + fs/gs
   base) inside `struct proc_slot`
2. timer ISR (and syscall exit path) that entered from ring-3 must
   spill the full user context into `slot.saved_frame` **before**
   any scheduler call
3. before returning to ring-3 the exit path reloads the saved-frame
   into the CPU iret frame + GPRs
4. add a specific selftest stage that intentionally preempts a
   spinning ring-3 task and asserts `tick_hits.u >= 3, GP = 0`
