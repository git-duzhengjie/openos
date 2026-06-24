#include "../openos_tcc_compat.h"
typedef void (*sighandler_t)(int);
#define SIGINT 2
#define SIGTERM 15
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
static inline sighandler_t signal(int sig, sighandler_t h) { (void)sig; return h; }
