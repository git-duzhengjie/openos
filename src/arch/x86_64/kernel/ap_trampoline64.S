/* Step G.4.3b-2a — AP trampoline blob (executable in 16-bit real mode).
 *
 * After INIT-SIPI-SIPI, an AP starts executing at CS:IP = (vector<<8):0000,
 * i.e. physical 0x8000 in our case (SIPI vector = 0x08). It is in real mode
 * with undefined segment registers (DS/ES/SS may not be zero).
 *
 * This blob therefore starts with executable code at offset 0 and pushes
 * the magic/version footer to offset +0x10 so the BSP can still verify the
 * install via byte-pattern check.
 *
 * AP behavior:
 *   cli                          ; mask IRQs
 *   xor ax, ax                   ; ax = 0
 *   mov ds, ax                   ; ds = 0  -> [imm] addresses linear phys
 *   lock inc byte [0x9000]       ; atomic bump of alive counter
 *   hlt                          ; sleep
 *   jmp -3                       ; loop on spurious wake
 *
 * Layout (offset 0):
 *   +0x00  FA                    cli
 *   +0x01  31 C0                 xor ax, ax
 *   +0x03  8E D8                 mov ds, ax
 *   +0x05  F0 FE 06 00 90        lock inc byte [0x9000]
 *   +0x0A  F4                    hlt
 *   +0x0B  EB FD                 jmp short -3   (back to hlt at +0x0A)
 *   +0x0D  00 00 00              pad
 *   +0x10  'A','P','T','R'       magic
 *   +0x14  0x02                  version (bumped from 0x01 in G.4.2)
 *   +0x15  ...                   pad to 16-byte align end
 */

    .section .rodata
    .global __ap_trampoline_blob_start
    .global __ap_trampoline_blob_end
    .balign 16
__ap_trampoline_blob_start:
    /* Real-mode code at offset 0. */
    .byte 0xFA                       /* cli */
    .byte 0x31, 0xC0                 /* xor ax, ax */
    .byte 0x8E, 0xD8                 /* mov ds, ax */
    .byte 0xF0, 0xFE, 0x06, 0x00, 0x90 /* lock inc byte [0x9000] */
1:  .byte 0xF4                       /* hlt */
    .byte 0xEB, 0xFD                 /* jmp short -3 (back to 1b) */
    .byte 0x00, 0x00, 0x00           /* pad to 0x10 */
    /* Magic + version at +0x10. */
    .byte 'A', 'P', 'T', 'R'         /* magic */
    .byte 0x02                       /* version (G.4.3b-2a) */
    .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    .balign 16
__ap_trampoline_blob_end:
