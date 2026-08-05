# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Hexagon memory management subsystem
#

obj-y := init.o uaccess.o vm_fault.o cache.o fixmap.o
obj-y += copy_to_user.o copy_from_user.o vm_tlb.o

obj-$(CONFIG_KASAN) += kasan_init.o
# Runs while the shadow is still the shared early region, and rewrites the
# page directory entries that back it.
KASAN_SANITIZE_kasan_init.o := n
