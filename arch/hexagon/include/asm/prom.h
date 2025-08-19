#ifndef __ASM_PROM_H
#define __ASM_PROM_H

#include <asm/setup.h>

extern char cmd_line[COMMAND_LINE_SIZE];

#ifdef CONFIG_OF_FLATTREE

extern struct machine_desc *setup_machine_fdt(void *dt_phys);

#else

static inline struct machine_desc *setup_machine_fdt(void *dt_phys)
{
        return NULL;
}

#endif
#endif /* __ASM_PROM_H */

