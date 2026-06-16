/* ============================================================
 * openos - user memory access helpers
 * ============================================================ */

#ifndef KERNEL_USERMEM_H
#define KERNEL_USERMEM_H

#include <stdint.h>
#include "types.h"
#include "usermode.h"

#define USERMEM_PTR_MIN   USER_SPACE_START
#define USERMEM_PTR_MAX   USER_SPACE_END
#define USERMEM_CSTR_MAX  4096u

#define USERMEM_READ      0
#define USERMEM_WRITE     1

int user_ptr_valid(const void *ptr, uint32_t len, int write);
int copy_from_user(void *dst, const void *user_src, uint32_t len);
int copy_to_user(void *user_dst, const void *src, uint32_t len);
int strncpy_from_user(char *dst, const char *user_src, uint32_t maxlen);

#endif /* KERNEL_USERMEM_H */
