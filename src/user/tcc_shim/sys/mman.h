#include "../../openos_tcc_compat.h"
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void*)-1)
static inline void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) { (void)addr; (void)prot; (void)flags; (void)fd; (void)offset; return openos_malloc((int)length); }
static inline int munmap(void *addr, size_t length) { (void)length; openos_free(addr); return 0; }
static inline int mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return 0; }
