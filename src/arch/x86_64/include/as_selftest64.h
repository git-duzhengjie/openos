#ifndef OPENOS_ARCH_X86_64_AS_SELFTEST64_H
#define OPENOS_ARCH_X86_64_AS_SELFTEST64_H

/*
 * H.5b.3 step A: address-space deep-clone self-test.
 *
 * Walks the path: as_create -> as_map_user (one user page) -> write a
 * magic into that page via boot 0..4 GiB identity -> as_clone -> verify
 * that
 *   - child PML4 is a fresh frame (!= parent),
 *   - child PML4[1] is present and != parent PML4[1] (own PDPT),
 *   - the cloned leaf user page is a DIFFERENT physical frame than the
 *     parent leaf (true deep copy, not shared),
 *   - the cloned leaf contains the same magic byte pattern,
 *   - mutating child's leaf does NOT propagate back to parent (isolation),
 *   - mutating parent's leaf does NOT propagate forward to child.
 *
 * Runs entirely on the boot identity map: never activates any of the
 * created ASes (no CR3 flip needed). Returns 0 on PASS, negative on FAIL.
 */
int arch_x86_64_as_selftest_clone_run(void);

#endif /* OPENOS_ARCH_X86_64_AS_SELFTEST64_H */
