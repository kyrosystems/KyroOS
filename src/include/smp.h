#ifndef SMP_H
#define SMP_H

#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

void smp_init(void);
uint32_t smp_get_cpu_count(void);
uint32_t smp_get_current_cpu_id(void);

#endif // SMP_H
