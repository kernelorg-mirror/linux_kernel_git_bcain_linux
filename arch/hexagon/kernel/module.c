/*
 * Kernel module loader for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <asm/module.h>
#include <linux/elf.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/vmalloc.h>
#include "reloc.h"

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

/*
 * module_frob_arch_sections - tweak got/plt sections.
 * @hdr - pointer to elf header
 * @sechdrs - pointer to elf load section headers
 * @secstrings - symbol names
 * @mod - pointer to module
 */
int module_frob_arch_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				char *secstrings,
				struct module *mod)
{
	unsigned int i;
	int found = 0;

	/* Look for .plt and/or .got.plt and/or .init.plt sections */
	for (i = 0; i < hdr->e_shnum; i++) {
		DEBUGP("Section %d is %s\n", i,
		       secstrings + sechdrs[i].sh_name);
		if (strcmp(secstrings + sechdrs[i].sh_name, ".plt") == 0)
			found = i+1;
		if (strcmp(secstrings + sechdrs[i].sh_name, ".got.plt") == 0)
			found = i+1;
		if (strcmp(secstrings + sechdrs[i].sh_name, ".rela.plt") == 0)
			found = i+1;
	}

	/* At this time, we don't support modules comiled with -shared */
	if (found) {
		printk(KERN_WARNING
			"Module '%s' contains unexpected .plt/.got sections.\n",
			mod->name);
		/*  return -ENOEXEC;  */
	}

	return 0;
}


static inline uint32_t is_pc_relative(uint32_t reloc) {
   return ((reloc == R_HEX_B22_PCREL)   ||
	   (reloc == R_HEX_B15_PCREL)   ||
	   (reloc == R_HEX_B32_PCREL_X) ||
	   (reloc == R_HEX_B22_PCREL_X) ||
	   (reloc == R_HEX_B15_PCREL_X) ||
	   (reloc == R_HEX_B13_PCREL_X) ||
	   (reloc == R_HEX_B9_PCREL_X)  ||
	   (reloc == R_HEX_B7_PCREL_X)  ||
	   (reloc == R_HEX_6_PCREL_X)   ||
	   (reloc == R_HEX_B9_PCREL)    ||
	   (reloc == R_HEX_B7_PCREL)    ||
	   (reloc == R_HEX_B13_PCREL));
}
/*
 * apply_relocate_add - perform rela relocations.
 * @sechdrs - pointer to section headers
 * @strtab - some sort of start address?
 * @symindex - symbol index offset or something?
 * @relsec - address to relocate to?
 * @module - pointer to module
 *
 * Perform rela relocations.
 */
int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
			unsigned int symindex, unsigned int relsec,
			struct module *module)
{
	unsigned int i;
	Elf32_Sym *sym;
	uint32_t *location;
	uint32_t value;
	unsigned int nrelocs = sechdrs[relsec].sh_size / sizeof(Elf32_Rela);
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;
	Elf32_Word sym_info = sechdrs[relsec].sh_info;
	Elf32_Sym *sym_base = (Elf32_Sym *) sechdrs[symindex].sh_addr;
	void *loc_base = (void *) sechdrs[sym_info].sh_addr;

	DEBUGP("Applying relocations in section %u to section %u base=%p\n",
	       relsec, sym_info, loc_base);

	for (i = 0; i < nrelocs; i++) {

		/* Symbol to relocate */
		sym = sym_base + ELF32_R_SYM(rela[i].r_info);

		/* Where to make the change */
		location = loc_base + rela[i].r_offset;

		/* `Everything is relative'. */
		value = sym->st_value + rela[i].r_addend;

		if (is_pc_relative(ELF32_R_TYPE(rela[i].r_info)))
			value = value - (uint32_t) location;

		DEBUGP("%d: value=%08x loc=%p reloc=%d symbol=%s\n",
		       i, value, location, ELF32_R_TYPE(rela[i].r_info),
		       sym->st_name ?
		       &strtab[sym->st_name] : "(anonymous)");
		DEBUGP("%d: Contents before reloc: %08x\n", i, *location);

		*location = do_reloc(ELF32_R_TYPE(rela[i].r_info),
				    *location, value);
		DEBUGP("%d: Contents after reloc: %08x\n", i, *location);

		if (*location == 0)
			return -ENOEXEC;
	}
	return 0;
}
