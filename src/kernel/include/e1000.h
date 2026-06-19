#ifndef E1000_H
#define E1000_H

#include "types.h"

void e1000_init(void);
void e1000_poll(void);
uint32_t e1000_device_count(void);

#endif /* E1000_H */
