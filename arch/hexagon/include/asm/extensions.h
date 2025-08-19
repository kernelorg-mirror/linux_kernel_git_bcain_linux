#include <asm/hvx.h>

/* Arch extensions that might need some housekeeping to be references per-thread */

struct extinfo {
	//  start making these CONFIG options?
	struct hvx_threadinfo *hvx;
};
