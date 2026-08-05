# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for hexagon-specific library files.
#
obj-y = checksum.o memmove.o memcpy.o memset.o memcpy_likely_aligned.o \
         divsi3.o modsi3.o udivsi3.o  umodsi3.o

# Hand-written assembly plus the checksum helpers; nothing to check here,
# and __memcpy/__memset must stay the raw versions KASAN's wrappers call.
KASAN_SANITIZE := n

# Without this the compiler recognises the copy loop in memmove.c and rewrites
# it into a call to memmove() -- i.e. into infinite recursion.
CFLAGS_memmove.o := -ffreestanding -fno-builtin
