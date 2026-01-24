#ifndef __ASM_HVX_H
#define __ASM_HVX_H

/*
 * Stub HVX definitions when CONFIG_HEXAGON_HVX is not set.
 * Full HVX support provides context switching and vector processing.
 */

struct HVX_Vectors;

struct hvx_threadinfo {
	int cnum;
	int prev_cnum;
	struct HVX_Vectors *vregs;
	unsigned long generation;
};

#endif /* __ASM_HVX_H */
