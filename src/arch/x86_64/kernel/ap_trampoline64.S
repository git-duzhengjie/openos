/* Step G.4.2 — AP trampoline blob (placeholder; no SIPI yet).
 *
 * This object emits a position-independent byte blob that will eventually
 * be copied to OPENOS_X86_64_SMP_TRAMPOLINE_PHYS (0x8000) and run by APs
 * after INIT-SIPI-SIPI. In G.4.2 we only verify the blob can be installed
 * and read back; no IPI is issued, so AP cores never actually execute
 * these bytes.
 *
 * Layout (offset 0):
 *   +0  : 4 bytes magic 'A','P','T','R'
 *   +4  : 1 byte version
 *   +5  : 3 bytes pad
 *   +8  : 16-bit code: cli; 1: hlt; jmp 1b
 *
 * The blob is kept in .rodata so the kernel can reference its bytes via
 * the C-visible symbols __ap_trampoline_blob_start / _end.
 */

    .section .rodata
    .global __ap_trampoline_blob_start
    .global __ap_trampoline_blob_end
    .balign 16
__ap_trampoline_blob_start:
    .byte 'A', 'P', 'T', 'R'        /* magic */
    .byte 0x01                       /* version */
    .byte 0x00, 0x00, 0x00           /* pad */
    .byte 0xFA                       /* cli */
1:  .byte 0xF4                       /* hlt */
    .byte 0xEB, 0xFD                 /* jmp short -3 (back to hlt) */
    .balign 16
__ap_trampoline_blob_end:
