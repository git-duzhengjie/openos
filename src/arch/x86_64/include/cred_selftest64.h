#ifndef OPENOS_ARCH_X86_64_CRED_SELFTEST64_H
#define OPENOS_ARCH_X86_64_CRED_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.11.1 credential selftest. Exercises the per-process POSIX credential
 * set (real/effective/saved uid+gid) and the setuid/setgid/seteuid/setegid
 * privilege rules directly against the current PCB:
 *
 *   - root (euid==0) setuid(N) collapses ruid=euid=suid=N (one-way drop),
 *   - an unprivileged process may only seteuid() to its real or saved uid,
 *     any other target returns -EPERM,
 *   - the group-id mutators mirror the uid rules.
 *
 * The test snapshots the current PCB credentials, drives the API through a
 * scripted sequence of privileged and unprivileged transitions, verifies
 * every outcome, and then restores the original credentials so the running
 * kernel PCB (slot 0 = root) is left untouched. Returns true on PASS.
 */
bool arch_x86_64_cred_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_CRED_SELFTEST64_H */
