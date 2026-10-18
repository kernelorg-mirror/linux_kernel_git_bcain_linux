// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module loader for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <asm/module.h>
#include <linux/elf.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/vmalloc.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

/* Which bits of an instruction hold the 6-bit part of an extended operand. */
struct insn_mask {
	uint32_t cmp;
	uint32_t reloc;
};

static const struct insn_mask r6_masks[] = {
	{0x38000000, 0x0000201f}, {0x39000000, 0x0000201f},
	{0x3e000000, 0x00001f80}, {0x3f000000, 0x00001f80},
	{0x40000000, 0x000020f8}, {0x41000000, 0x000007e0},
	{0x42000000, 0x000020f8}, {0x43000000, 0x000007e0},
	{0x44000000, 0x000020f8}, {0x45000000, 0x000007e0},
	{0x46000000, 0x000020f8}, {0x47000000, 0x000007e0},
	{0x6a000000, 0x00001f80}, {0x7c000000, 0x001f2000},
	{0x9a000000, 0x00000f60}, {0x9b000000, 0x00000f60},
	{0x9c000000, 0x00000f60}, {0x9d000000, 0x00000f60},
	{0x9f000000, 0x001f0100}, {0xab000000, 0x0000003f},
	{0xad000000, 0x0000003f}, {0xaf000000, 0x00030078},
	{0xd7000000, 0x006020e0}, {0xd8000000, 0x006020e0},
	{0xdb000000, 0x006020e0}, {0xdf000000, 0x006020e0},
};

#define INSN_PARSE_BITS	0x0000c000

/* Duplexes have no parse bits, so none of their bits are zero in both. */
static bool insn_is_duplex(uint32_t insn)
{
	return !(insn & INSN_PARSE_BITS);
}

/* Scatter the low bits of val over the set bits of mask, lowest first. */
static uint32_t apply_mask(uint32_t mask, uint32_t val)
{
	uint32_t result = 0;
	uint32_t bit;

	for (bit = 1; mask; bit <<= 1) {
		if (!(mask & bit))
			continue;
		if (val & 1)
			result |= bit;
		val >>= 1;
		mask &= ~bit;
	}
	return result;
}

static int find_mask_r6(uint32_t insn, uint32_t *mask)
{
	int i;

	if (insn_is_duplex(insn)) {
		*mask = 0x03f00000;
		return 0;
	}
	for (i = 0; i < ARRAY_SIZE(r6_masks); i++) {
		if ((insn & 0xff000000) == r6_masks[i].cmp) {
			*mask = r6_masks[i].reloc;
			return 0;
		}
	}
	return -ENOEXEC;
}

static uint32_t find_mask_r8(uint32_t insn)
{
	if (insn_is_duplex(insn))
		return 0x03f00000;
	if ((insn & 0xff000000) == 0xde000000)
		return 0x00e020e8;
	if ((insn & 0xff000000) == 0x3c000000)
		return 0x0000207f;
	return 0x00001fe0;
}

static uint32_t find_mask_r11(uint32_t insn)
{
	if (insn_is_duplex(insn))
		return 0x03f00000;
	if ((insn & 0xff000000) == 0xa1000000)
		return 0x060020ff;
	return 0x06003fe0;
}

static int find_mask_r16(uint32_t insn, uint32_t *mask)
{
	if (insn_is_duplex(insn)) {
		*mask = 0x03f00000;
		return 0;
	}
	insn &= ~INSN_PARSE_BITS;

	switch (insn & 0xff000000) {
	case 0x48000000:
		*mask = 0x061f20ff;
		return 0;
	case 0x49000000:
		*mask = 0x061f3fe0;
		return 0;
	case 0x78000000:
		*mask = 0x00df3fe0;
		return 0;
	case 0xb0000000:
		*mask = 0x0fe03fe0;
		return 0;
	}
	switch (insn & 0xff802000) {
	case 0x74000000:
	case 0x74002000:
	case 0x74800000:
	case 0x74802000:
		*mask = 0x00001fe0;
		return 0;
	}
	return find_mask_r6(insn, mask);
}

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
	uint32_t value, mask;
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

		DEBUGP("%d: value=%08x loc=%p reloc=%d symbol=%s\n",
		       i, value, location, ELF32_R_TYPE(rela[i].r_info),
		       sym->st_name ?
		       &strtab[sym->st_name] : "(anonymous)");

		switch (ELF32_R_TYPE(rela[i].r_info)) {
		case R_HEXAGON_B22_PCREL: {
			int dist = (int)(value - (uint32_t)location);
			if ((dist < -0x00800000) ||
			    (dist >= 0x00800000)) {
				printk(KERN_ERR
				       "%s: %s: %08x=%08x-%08x %s\n",
				       module->name,
				       "R_HEXAGON_B22_PCREL reloc out of range",
				       dist, value, (uint32_t)location,
				       sym->st_name ?
				       &strtab[sym->st_name] : "(anonymous)");
				return -ENOEXEC;
			}
			DEBUGP("B22_PCREL contents: %08X.\n", *location);
			*location &= ~0x01ff3fff;
			*location |= 0x00003fff & dist;
			*location |= 0x01ff0000 & (dist<<2);
			DEBUGP("Contents after reloc: %08x\n", *location);
			break;
		}
		case R_HEXAGON_HI16:
			value = (value>>16) & 0xffff;
			fallthrough;
		case R_HEXAGON_LO16:
			*location &= ~0x00c03fff;
			*location |= value & 0x3fff;
			*location |= (value & 0xc000) << 8;
			break;
		case R_HEXAGON_32:
			*location = value;
			break;
		case R_HEXAGON_32_PCREL:
			*location = value - (uint32_t)location;
			break;
		/*
		 * An operand that is too big for its instruction gets a
		 * constant extender, which holds all but the low 6 bits.
		 */
		case R_HEXAGON_32_6_X:
			*location |= apply_mask(0x0fff3fff, value >> 6);
			break;
		case R_HEXAGON_B32_PCREL_X:
			value -= (uint32_t)location;
			*location |= apply_mask(0x0fff3fff, value >> 6);
			break;
		case R_HEXAGON_B22_PCREL_X:
			value -= (uint32_t)location;
			*location |= apply_mask(0x01ff3ffe, value & 0x3f);
			break;
		case R_HEXAGON_B15_PCREL_X:
			value -= (uint32_t)location;
			*location |= apply_mask(0x00df20fe, value & 0x3f);
			break;
		case R_HEXAGON_B9_PCREL_X:
			value -= (uint32_t)location;
			*location |= apply_mask(0x003000fe, value & 0x3f);
			break;
		case R_HEXAGON_6_X:
			if (find_mask_r6(*location, &mask))
				goto bad_insn;
			*location |= apply_mask(mask, value);
			break;
		case R_HEXAGON_8_X:
			*location |= apply_mask(find_mask_r8(*location), value);
			break;
		case R_HEXAGON_9_X:
			*location |= apply_mask(0x00003fe0, value & 0x3f);
			break;
		case R_HEXAGON_10_X:
			*location |= apply_mask(0x00203fe0, value & 0x3f);
			break;
		case R_HEXAGON_11_X:
			*location |= apply_mask(find_mask_r11(*location),
						value & 0x3f);
			break;
		case R_HEXAGON_12_X:
			*location |= apply_mask(0x000007e0, value);
			break;
		case R_HEXAGON_16_X:
			if (find_mask_r16(*location, &mask))
				goto bad_insn;
			*location |= apply_mask(mask, value & 0x3f);
			break;
		case R_HEXAGON_PLT_B22_PCREL:
		case R_HEXAGON_GOTOFF_LO16:
		case R_HEXAGON_GOTOFF_HI16:
			printk(KERN_ERR "%s: GOT/PLT relocations unsupported\n",
			       module->name);
			return -ENOEXEC;
		default:
			printk(KERN_ERR "%s: unknown relocation: %u\n",
			       module->name,
			       ELF32_R_TYPE(rela[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;

bad_insn:
	printk(KERN_ERR "%s: unrecognized instruction %08x for relocation %u\n",
	       module->name, *location, ELF32_R_TYPE(rela[i].r_info));
	return -ENOEXEC;
}
