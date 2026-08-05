/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_STRING_H_
#define _ASM_STRING_H_

#ifdef __KERNEL__
#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *__to, __const__ void *__from, size_t __n);
extern void *__memcpy(void *__to, __const__ void *__from, size_t __n);

/*  ToDo:  use dczeroa, accelerate the compiler-constant zero case  */
#define __HAVE_ARCH_MEMSET
extern void *memset(void *__to, int c, size_t __n);
extern void *__memset(void *__to, int c, size_t __n);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *__to, __const__ void *__from, size_t __n);
extern void *__memmove(void *__to, __const__ void *__from, size_t __n);

/*
 * Under KASAN the plain names resolve to the checked wrappers in
 * mm/kasan/shadow.c; the __-prefixed aliases are the raw assembly, for code
 * that must not be checked (and for KASAN's own wrappers to call).
 */
#if defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__)
#define memcpy(dst, src, len) __memcpy(dst, src, len)
#define memset(s, c, n) __memset(s, c, n)
#define memmove(dst, src, len) __memmove(dst, src, len)

#ifndef __NO_FORTIFY
#define __NO_FORTIFY /* FORTIFY_SOURCE uses __builtin_memcpy, etc. */
#endif
#endif
#endif


#endif /* _ASM_STRING_H_ */
