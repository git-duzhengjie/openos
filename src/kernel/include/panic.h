#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <stdint.h>
#include "idt.h"

#define PANIC_DUMP_MAGIC 0x50444d50u /* "PDMP" */
#define PANIC_DUMP_VERSION 1u
#define PANIC_TEXT_LEN 32u

typedef struct panic_frame {
    const char *arch;
    const char *reason;
    const registers_t *regs;
    uint32_t fault_addr;
    uint32_t has_fault_addr;
    uint32_t pid;
} panic_frame_t;

typedef struct panic_dump {
    uint32_t magic;
    uint32_t version;
    uint32_t valid;
    char arch[PANIC_TEXT_LEN];
    char reason[PANIC_TEXT_LEN];
    uint32_t pid;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t fault_addr;
    uint32_t has_fault_addr;
    uint32_t cr3;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t saved_esp;
    uint32_t user_esp;
    uint32_t user_ss;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
} panic_dump_t;

const panic_dump_t *panic_get_last_dump(void);
uint32_t panic_dump_is_valid(const panic_dump_t *dump);
void panic_capture_dump(const panic_frame_t *frame);
void panic_log_exception(const panic_frame_t *frame);
void panic_halt(void) __attribute__((noreturn));
void panic_log_and_halt(const panic_frame_t *frame) __attribute__((noreturn));

#endif /* KERNEL_PANIC_H */
