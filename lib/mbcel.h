/* Multi-byte characters, error encodings, and lengths
   Copyright 2023 Free Software Foundation, Inc.

   This file is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 3 of the
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

   The mbcel_scanz function is similar except it works with a
   string of unknown length that is terminated with '\0'.
   Instead of this single-byte code:

      char *p = ...;
      for (; *p; p++)
	process (*p);

   You can use this multi-byte code:

      char *p = ...;
      for (mbcel_t g; *p; p += g.len)
	{
	  g = mbcel_scanz (p);
	  process (g);
	}

   mbcel_scant (P, TERMINATOR) is like mbcel_scanz (P) except the
   string is terminated by TERMINATOR.  The TERMINATORs '\0', '\r',
   '\n', '.', '/' are safe, as they cannot be a part (even a trailing
   byte) of a multi-byte character.

   mbcel_cmp (G1, G2) and mbcel_casecmp (G1, G2) compare two mbcel_t
   values lexicographically by character or by encoding byte value,
   with encoding bytes sorting after characters.  mbcel_casecmp
   ignores case in characters.  mbcel_strcasecmp compares two
   null-terminated strings lexicographically.

   Although ISO C and POSIX allow encodings that have shift states or
   that can produce multiple characters from an indivisible byte sequence,
   POSIX does not require support for these encodings,
   they are not in practical use on GNUish platforms,
   and omitting support for them simplifies the API.  */

#ifndef _MBCEL_H
#define _MBCEL_H 1

/* This file uses _GL_INLINE_HEADER_BEGIN, _GL_INLINE,
   _GL_ATTRIBUTE_MAY_ALIAS.  */
#if !_GL_CONFIG_H_INCLUDED
 #error "Please include config.h first."
#endif

#include <limits.h>
#include <stddef.h>
#include <uchar.h>

/* The maximum multibyte character length supported on any platform.
   This can be less than MB_LEN_MAX because many platforms have a
   large MB_LEN_MAX to allow for stateful encodings, and mbcel does
   not need to support these encodings.  MBCEL_LEN_MAX is enough for
   UTF-8, EUC, Shift-JIS, GB18030, etc.
   0 < MB_CUR_MAX <= MBCEL_LEN_MAX <= MB_LEN_MAX.  */
enum { MBCEL_LEN_MAX = MB_LEN_MAX < 4 ? MB_LEN_MAX : 4 };

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

/* With mbcel there should be no need for the performance overhead of
   replacing glibc mbrtoc32, as callers shouldn't care whether the
   C locale treats a byte with the high bit set as an encoding error.  */
#ifdef __GLIBC__
# undef mbrtoc32
#endif

/* Shifting an encoding error byte (which must be at least 2**7)
   left by 14 yields at least 2**21 (0x200000), which is greater
   than the maximum Unicode value 0x10FFFF.  This suffices to sort
   encoding errors after characters.  */
enum { MBCEL_ENCODING_ERROR_SHIFT = 14 };

/* In the typical case where unsigned char easily fits in int,
   optimizations are possible.  */
enum {
  MBCEL_UCHAR_FITS = UCHAR_MAX <= INT_MAX,
  MBCEL_UCHAR_EASILY_FITS = UCHAR_MAX <= INT_MAX >> MBCEL_ENCODING_ERROR_SHIFT
};

#ifndef _GL_LIKELY
/* Rely on __builtin_expect, as provided by the module 'builtin-expect'.  */
# define _GL_LIKELY(cond) __builtin_expect ((cond), 1)
# define _GL_UNLIKELY(cond) __builtin_expect ((cond), 0)
#endif

/* Scan bytes from P inclusive to LIM exclusive.  P must be less than LIM.
   Return either the valid character starting at P,
   or the encoding error of length 1 at P.  */
MBCEL_INLINE mbcel_t
mbcel_scan (char const *p, char const *lim)
{
  /* Handle ASCII quickly to avoid the overhead of calling mbrtoc32.
     In supported encodings, the first byte of a multi-byte character
     cannot be an ASCII byte.  */
  if (_GL_LIKELY (0 <= *p && *p <= 0x7f))
    return (mbcel_t) { .ch = *p, .len = 1 };

  /* An initial mbstate_t; initialization optimized for some platforms.
     For details about these and other platforms, see wchar.in.h.  */
#if defined __GLIBC__ && 2 < __GLIBC__ + (2 <= __GLIBC_MINOR__)
  /* Although only a trivial optimization, it's worth it for GNU.  */
  mbstate_t mbs; mbs.__count = 0;
#elif (defined __FreeBSD__ || defined __DragonFly__ || defined __OpenBSD__ \
       || (defined __APPLE__ && defined __MACH__))
  /* These platforms have 128-byte mbstate_t.  What were they thinking?
     Initialize just for supported encodings (UTF-8, EUC, etc.).
     Avoid memset because some compilers generate function call code.  */
  struct mbhidden { char32_t ch; int utf8_want, euc_want; }
    _GL_ATTRIBUTE_MAY_ALIAS;
  union { mbstate_t m; struct mbhidden s; } u;
  u.s.ch = u.s.utf8_want = u.s.euc_want = 0;
# define mbs u.m
#elif defined __NetBSD__
  /* Experiments on both 32- and 64-bit NetBSD platforms have
     shown that it doesn't work to clear fewer than 24 bytes.  */
  struct mbhidden { long long int a, b, c; } _GL_ATTRIBUTE_MAY_ALIAS;
  union { mbstate_t m; struct mbhidden s; } u;
  u.s.a = u.s.b = u.s.c = 0;
# define mbs u.m
#else
  /* mbstate_t has unknown structure or is not worth optimizing.  */
  mbstate_t mbs = {0};
#endif

  char32_t ch;
  size_t len = mbrtoc32 (&ch, p, lim - p, &mbs);

  /* Any LEN with top bit set is an encoding error, as LEN == (size_t) -3
     is not supported and MB_LEN_MAX is small.  */
  if (_GL_UNLIKELY ((size_t) -1 / 2 < len))
    return (mbcel_t) { .err = *p, .len = 1 };

  /* Tell the compiler LEN is at most MB_LEN_MAX,
     as this can help GCC generate better code.  */
  if (! (len <= MB_LEN_MAX))
    unreachable ();

  /* A multi-byte character.  LEN must be positive,
     as *P != '\0' and shift sequences are not supported.  */
  return (mbcel_t) { .ch = ch, .len = len };
}

/* Scan bytes from P, a byte sequence terminated by TERMINATOR.
   If *P == TERMINATOR, scan just that byte; otherwise scan
   bytes up to but not including a TERMINATOR byte.
   TERMINATOR must be ASCII, and should be '\0', '\r', '\n', '.', or '/'.
   Return either the valid character starting at P,
   or the encoding error of length 1 at P.  */
MBCEL_INLINE mbcel_t
mbcel_scant (char const *p, char terminator)
{
  /* Handle ASCII quickly for speed.  */
  if (_GL_LIKELY (0 <= *p && *p <= 0x7f))
    return (mbcel_t) { .ch = *p, .len = 1 };

  /* Defer to mbcel_scan for non-ASCII.  Compute length with code that
     is typically branch-free and faster than memchr or strnlen.  */
  char const *lim = p + 1;
  for (int i = 0; i < MBCEL_LEN_MAX - 1; i++)
    lim += *lim != terminator;
  return mbcel_scan (p, lim);
}

/* Scan bytes from P, a byte sequence terminated by '\0'.
   If *P == '\0', scan just that byte; otherwise scan
   bytes up to but not including a '\0'.
   Return either the valid character starting at P,
   or the encoding error of length 1 at P.  */
MBCEL_INLINE mbcel_t
mbcel_scanz (char const *p)
{
  return mbcel_scant (p, '\0');
}

/* Compare G1 and G2, with encoding errors sorting after characters.
   Return <0, 0, >0 for <, =, >.  */
MBCEL_INLINE int
mbcel_cmp (mbcel_t g1, mbcel_t g2)
{
  int c1 = g1.ch, c2 = g2.ch, e1 = g1.err, e2 = g2.err, ccmp = c1 - c2,
    ecmp = MBCEL_UCHAR_EASILY_FITS ? e1 - e2 : _GL_CMP (e1, e2);
  return (ecmp << MBCEL_ENCODING_ERROR_SHIFT) + ccmp;
}

/* Compare G1 and G2 ignoring case, with encoding errors sorting after
   characters.  Return <0, 0, >0 for <, =, >.  */
MBCEL_INLINE int
mbcel_casecmp (mbcel_t g1, mbcel_t g2)
{
  int cmp = mbcel_cmp (g1, g2);
  if (_GL_LIKELY (g1.err | g2.err | !cmp))
    return cmp;
  int c1 = c32tolower (g1.ch);
  int c2 = c32tolower (g2.ch);
  return c1 - c2;
}

/* Compare the multi-byte strings S1 and S2 lexicographically, ignoring case.
   Return <0, 0, >0 for <, =, >.  Consider encoding errors to be
   greater than characters and compare them byte by byte.  */
int mbcel_strcasecmp (char const *s1, char const *s2);

_GL_INLINE_HEADER_END

#endif /* _MBCEL_H */
