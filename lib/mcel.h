/* Multi-byte characters, Error encodings, and Lengths (MCELs)
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

/* The mcel_scan function lets code iterate through an array of bytes,
   supporting character encodings in practical use
   more simply than using plain mbrtoc32.

   Instead of this single-byte code:

      char *p = ..., *lim = ...;
      for (; p < lim; p++)
        process (*p);

   You can use this multi-byte code:

      char *p = ..., *lim = ...;
      for (mcel_t g; p < lim; p += g.len)
        {
	  g = mcel_scan (p, lim);
	  process (g);
	}

   The mcel_scanz function is similar except it works with a
   string of unknown length that is terminated with '\0'.
   Instead of this single-byte code:

      char *p = ...;
      for (; *p; p++)
	process (*p);

   You can use this multi-byte code:

      char *p = ...;
      for (mcel_t g; *p; p += g.len)
	{
	  g = mcel_scanz (p);
	  process (g);
	}

   mcel_scant (P, TERMINATOR) is like mcel_scanz (P) except the
   string is terminated by TERMINATOR.  The TERMINATORs '\0', '\r',
   '\n', '.', '/' are safe, as they cannot be a part (even a trailing
   byte) of a multi-byte character.

   You can select from G using G.c and G.len.
   You can use ucore_* functions on G.c, e.g., ucore_iserr (G.c),
   ucore_is (c32isalpha, G.c), and ucore_to (c32tolower, G.c).

   mcel_strcasecmp compares two null-terminated multi-byte strings
   lexicographically, ignoring case.

   Although ISO C and POSIX allow encodings that have shift states or
   that can produce multiple characters from an indivisible byte sequence,
   POSIX does not require support for these encodings,
   they are not in practical use on GNUish platforms,
   and omitting support for them simplifies the API.  */

#ifndef _MCEL_H
#define _MCEL_H 1

/* This API is an extension of ucore.h.  Programs that include this
   file can assume ucore.h is included too.  */
#include <ucore.h>

/* The maximum multi-byte character length supported on any platform.
   This can be less than MB_LEN_MAX because many platforms have a
   large MB_LEN_MAX to allow for stateful encodings, and mcel does not
   support these encodings.  MCEL_LEN_MAX is enough for UTF-8, EUC,
   Shift-JIS, GB18030, etc.  In all multi-byte encodings supported by glibc,
   0 < MB_CUR_MAX <= MCEL_LEN_MAX <= MB_LEN_MAX.  */
enum { MCEL_LEN_MAX = MB_LEN_MAX < 4 ? MB_LEN_MAX : 4 };

/* mcel_t is a type representing a character or encoding error C,
   along with a count of the LEN bytes that represent C.
   1 <= LEN <= MB_LEN_MAX.  */
typedef struct
{
  ucore_t c;
  unsigned char len;
} mcel_t;

/* Every multi-byte character length fits in mcel_t's LEN.  */
static_assert (MB_LEN_MAX <= UCHAR_MAX);

/* Bytes have 8 bits, as POSIX requires.  */
static_assert (CHAR_BIT == 8);

/* Pacify GCC re 'c <= 0x7f' below.  */
#if defined __GNUC__ && 4 < __GNUC__ + (3 <= __GNUC_MINOR__)
# pragma GCC diagnostic ignored "-Wtype-limits"
#endif

_GL_INLINE_HEADER_BEGIN
#ifndef MCEL_INLINE
# define MCEL_INLINE _GL_INLINE
#endif

/* With mcel there should be no need for the performance overhead of
   replacing glibc mbrtoc32, as callers shouldn't care whether the
   C locale treats a byte with the high bit set as an encoding error.  */
#ifdef __GLIBC__
# undef mbrtoc32
#endif

/* Shifting an encoding error byte (at least 0x80) left by this value
   yields a value in the range UCORE_ERR_MIN .. 2*UCORE_ERR_MIN - 1.
   This suffices to sort encoding errors after characters.  */
enum { MCEL_ENCODING_ERROR_SHIFT = 14 };
static_assert (UCORE_ERR_MIN == 0x80 << MCEL_ENCODING_ERROR_SHIFT);

/* Whether C represents itself as a Unicode character
   when it is the first byte of a single- or multi-byte character.
   These days it is safe to assume ASCII, so do not support
   obsolescent encodings like CP864, EBCDIC, Johab, and Shift JIS.  */
MCEL_INLINE bool
mcel_isbasic (char c)
{
  return 0 <= c && c <= 0x7f;
}

/* Scan bytes from P inclusive to LIM exclusive.  P must be less than LIM.
   Return the character or encoding error starting at P.  */
MCEL_INLINE mcel_t
mcel_scan (char const *p, char const *lim)
{
  /* Handle ASCII quickly to avoid the overhead of calling mbrtoc32.
     In supported encodings, the first byte of a multi-byte character
     cannot be an ASCII byte.  */
  if (_GL_LIKELY (mcel_isbasic (*p)))
    return (mcel_t) { .c = *p, .len = 1 };

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

  char32_t c;
  size_t len = mbrtoc32 (&c, p, lim - p, &mbs);

  /* Any LEN with top bit set is an encoding error, as LEN == (size_t) -3
     is not supported and MB_LEN_MAX is small.  */
  if (_GL_LIKELY (len <= (size_t) -1 / 2))
    {
      /* A multi-byte character.  LEN must be positive,
	 as *P != '\0' and shift sequences are not supported.  */
      assume (0 < len);
      assume (len <= MB_LEN_MAX);
      assume (c <= UCORE_CHAR_MAX);
      return (mcel_t) { .c = c, .len = len };
    }
  else
    {
      /* An encoding error.  */
      unsigned char b = *p;
      c = b << MCEL_ENCODING_ERROR_SHIFT;
      assume (UCORE_ERR_MIN <= c);
      assume (c <= UCORE_ERR_MAX);
      return (mcel_t) { .c = c, .len = 1 };
    }
}

/* Scan bytes from P, a byte sequence terminated by TERMINATOR.
   If *P == TERMINATOR, scan just that byte; otherwise scan
   bytes up to but not including TERMINATOR.
   TERMINATOR must be ASCII, and should be '\0', '\r', '\n', '.', or '/'.
   Return the character or encoding error starting at P.  */
MCEL_INLINE mcel_t
mcel_scant (char const *p, char terminator)
{
  /* Handle ASCII quickly for speed.  */
  if (_GL_LIKELY (mcel_isbasic (*p)))
    return (mcel_t) { .c = *p, .len = 1 };

  /* Defer to mcel_scan for non-ASCII.  Compute length with code that
     is typically branch-free and faster than memchr or strnlen.  */
  char const *lim = p + 1;
  for (int i = 0; i < MCEL_LEN_MAX - 1; i++)
    lim += *lim != terminator;
  return mcel_scan (p, lim);
}

/* Scan bytes from P, a byte sequence terminated by '\0'.
   If *P == '\0', scan just that byte; otherwise scan
   bytes up to but not including '\0'.
   Return the character or encoding error starting at P.  */
MCEL_INLINE mcel_t
mcel_scanz (char const *p)
{
  return mcel_scant (p, '\0');
}

/* Compare the multi-byte strings S1 and S2 lexicographically, ignoring case.
   Return <0, 0, >0 for <, =, >.  Consider encoding errors to be
   greater than characters and compare them byte by byte.  */
int mcel_casecmp (char const *s1, char const *s2);

_GL_INLINE_HEADER_END

#endif /* _MCEL_H */
