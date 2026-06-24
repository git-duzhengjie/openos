#include "../openos_tcc_compat.h"
typedef int jmp_buf[16];
typedef int sigjmp_buf[16];
static inline int setjmp(jmp_buf env) { (void)env; return 0; }
static inline void longjmp(jmp_buf env, int val) { (void)env; openos_exit(val ? val : 1); }
static inline int sigsetjmp(sigjmp_buf env, int savesigs) { (void)savesigs; return setjmp(env); }
static inline void siglongjmp(sigjmp_buf env, int val) { longjmp(env, val); }
