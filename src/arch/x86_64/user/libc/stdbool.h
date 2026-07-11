/*
 * M5.3d libc/stdbool.h — C99 boolean support for OpenOS ring3 userland.
 */
#ifndef OPENOS_LIBC_STDBOOL_H
#define OPENOS_LIBC_STDBOOL_H

#ifndef __cplusplus
#define bool  _Bool
#define true  1
#define false 0
#endif

#define __bool_true_false_are_defined 1

#endif /* OPENOS_LIBC_STDBOOL_H */
