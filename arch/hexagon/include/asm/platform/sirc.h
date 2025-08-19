/*
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*  Derived from arch/arm/mach-msm/include/mach/sirc.h  */

#ifndef __SIRC_H
#define __SIRC_H

/*
 * Some, but not all, hexagon-based systems use a macro wrapped
 * around the hexagon core that cascades multiple interrupt
 * registers to several CPU irq's.  The structures here are
 * used to define that routing.
 */
struct sirc_regs {
	void __iomem   *int_status;
	void __iomem   *int_polarity;
	void __iomem   *int_type;
	void __iomem   *int_enable;
	void __iomem   *int_enable_set;
	void __iomem   *int_enable_clear;
	void __iomem   *int_clear;
};

struct sirc_save {
	unsigned int int_enable;
	unsigned int wake_enable;
	unsigned int type;
	unsigned int polarity;
};

struct sirc_cascade_regs {
	unsigned int    cascade_irq;
	unsigned int    sirq_base;
	unsigned int    group_size;
	unsigned int    base_addr;
	struct sirc_regs regs;
	struct sirc_save save;
	struct irq_domain *irqd;  //  weasel
};

void hexss_init_sirc(struct sirc_cascade_regs *, int);
void hexss_sirc_enter_sleep(void);
void hexss_sirc_exit_sleep(void);


int __init hexss_init_sirc_of(struct device_node *node,
			  struct device_node *parent);

#endif
