/* Arch extensions - stub when HVX is not available */
#include <asm/hmx.h>
#include <asm/hvx.h>

struct extinfo {
	struct hvx_threadinfo *hvx;
	struct hmx_threadinfo *hmx;
};
