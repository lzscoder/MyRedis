//一些移植性方面的宏。
#ifndef _REDIS_FMACROS_H
#define _REDIS_FMACROS_H

#define _BSD_SOURCE

#if defined(__linux__)
#define _GNU_SOURCE
#endif // defined


#if defined(__linux__) || defined(__OpenBSD__)
#define _XOPEN_SOURCE 700
#elif !defined(__NetBSD__)
#define _XOPEN_SOURCE
#endif // defined

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#endif // _REDIS_FMACROS_H
