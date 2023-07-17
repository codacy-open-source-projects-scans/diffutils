/* Multi-byte characters, error encodings, and lengths
   Copyright 2023 Free Software Foundation, Inc.

   This file is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   This file is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

/* The mbcel_scan function lets code iterate through an array of bytes,
   supporting character encodings in practical use
   more simply than using plain mbrtoc32.

   Instead of this single-byte code:

      char *p = ..., *lim = ...;
      for (; p < lim; p++)
        process (*p);

   You can use this multi-byte code:

      char *p = ..., *lim = ...;
      for (mbcel_t g; p < lim; p += g.len)
        {
	  g = mbcel_scan (p, lim);
	  process (g);
	}

   You can select from G using G.ch, G.err, and G.len.

   Although ISO C and POSIX allow encodings that have shift states or
   that can produce multiple characters from an indivisible byte sequence,
   POSIX does not require support for these encodings,
   they are not in practical use on GNUish platforms,
   and omitting support for them simplifies the API.  */

#ifndef _MBCEL_H
#define _MBCEL_H 1

/* This file uses _GL_INLINE_HEADER_BEGIN, _GL_INLINE.  */
#if !_GL_CONFIG_H_INCLUDED
 #error "Please include config.h first."
#endif

#include <limits.h>
#include <stddef.h>
#include <uchar.h>

/* mbcel_t is a type representing a character CH or an encoding error byte ERR,
   along with a count of the LEN bytes that represent CH or ERR.
   If ERR is zero, CH is a valid character and 1 <= LEN <= MB_LEN_MAX;
   otherwise ERR is an encoding error byte, 0x80 <= ERR <= UCHAR_MAX,
   CH == 0, and LEN == 1.  */
typedef struct
{
  char32_t ch;
  unsigned char err;
  unsigned char len;
} mbcel_t;

/* On all known platforms, every multi-byte character length fits in
   mbcel_t's LEN.  Check this.  */
static_assert (MB_LEN_MAX <= UCHAR_MAX);

/* Pacify GCC re '*p <= 0x7f' below.  */
#if defined __GNUC__ && 4 < __GNUC__ + (3 <= __GNUC_MINOR__)
# pragma GCC diagnostic ignored "-Wtype-limits"
#endif

_GL_INLINE_HEADER_BEGIN
#ifndef MBCEL_INLINE
# define MBCEL_INLINE _GL_INLINE
#endif

/* With diffutils there is no need for the performance overhead of
   replacing glibc mbrtoc32, as it doesn't matter whether the C locale
   treats bytes with the high bit set as encoding errors.  */
#ifdef __GLIBC__
# undef mbrtoc32
#endif

/* Scan bytes from P inclusive to LIM exclusive.  P must be less than LIM.
   Return either the representation of the valid character starting at P,
   or the representation of an encoding error of length 1 at P.  */
MBCEL_INLINE mbcel_t
mbcel_scan (char const *p, char const *lim)
{
  /* Handle ASCII quickly to avoid the overhead of calling mbrtoc32.
     In supported encodings, the first byte of a multi-byte character
     cannot be an ASCII byte.  */
  if (0 <= *p && *p <= 0x7f)
    return (mbcel_t) { .ch = *p, .len = 1 };

  /* An initial mbstate_t; initialization optimized for some platforms.  */
#if defined __GLIBC__ && 2 < __GLIBC__ + (2 <= __GLIBC_MINOR__)
  mbstate_t mbs; mbs.__count = 0;
#elif (defined __FreeBSD__ || defined __DragonFly__ || defined __OpenBSD__ \
       || (defined __APPLE__ && defined __MACH__))
  /* Initialize for all encodings: UTF-8, EUC, etc.  */
  union { mbstate_t m; struct { uchar_t ch; int utf8_want, euc_want; } s; } u;
  u.s.ch = u.s.utf8_want = u.s.euc_want = 0;
# define mbs u.m
#elif defined __NetBSD__
  union { mbstate_t m; struct _RuneLocale *s; } u;
  u.s = nullptr;
# define mbs u.m
#else
  /* mbstate_t has unknown structure or is not worth optimizing.  */
  mbstate_t mbs = {0};
#endif

  char32_t ch;
  size_t len = mbrtoc32 (&ch, p, lim - p, &mbs);

  /* Any LEN with top bit set is an encoding error, as LEN == (size_t) -3
     is not supported and MB_LEN_MAX <= (size_t) -1 / 2 on all platforms.  */
  static_assert (MB_LEN_MAX <= (size_t) -1 / 2);
  if ((size_t) -1 / 2 < len)
    return (mbcel_t) { .err = *p, .len = 1 };

  /* Tell the compiler LEN is at most MB_LEN_MAX,
     as this can help GCC generate better code.  */
  if (! (len <= MB_LEN_MAX))
    unreachable ();

  /* A multi-byte character.  LEN must be positive,
     as *P != '\0' and shift sequences are not supported.  */
  return (mbcel_t) { .ch = ch, .len = len };
}

_GL_INLINE_HEADER_END

#endif /* _MBCEL_H */
