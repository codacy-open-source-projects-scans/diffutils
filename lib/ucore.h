/* Unicode Characters OR Encoding errors (UCOREs)
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

/* This API's fundamental type ucore_t represents
   a single Unicode character or an encoding error.
   ucore_iserr (C) tests whether C is an encoding error.
   ucore_is (P, C) etc. test whether char class P accepts C.
   ucore_to (TO, C) etc. use TO to convert C.
   ucore_cmp (C1, C2) and ucore_tocmp (TO, C1, C2) compare C1 and C2,
   with encoding errors sorting after characters.  */

#ifndef _UCORE_H
#define _UCORE_H 1

#if !_GL_CONFIG_H_INCLUDED
 #error "Please include config.h first."
#endif

#include <verify.h>

#include <limits.h>
#include <stddef.h>
#include <uchar.h>

/* ucore_t represents a Unicode Character OR Encoding error.
   If 0 <= C <= UCORE_CHAR_MAX, C represents a Unicode character.
   If UCORE_ERR_MIN <= C <= UCORE_ERR_MAX, C represents an encoding error.
   Other ucore_t values C are invalid.  */
typedef int ucore_t;

enum {
  UCORE_CHAR_MAX = 0x10FFFF,
  UCORE_ERR_MIN = 0x200000,
  UCORE_ERR_MAX = 2 * UCORE_ERR_MIN - 1
};

/* Information is not lost by encoding errors as integers.  */
static_assert (UCHAR_MAX <= UCORE_ERR_MAX - UCORE_ERR_MIN);

/* On glibc platforms, predicates like c32isalnum and c32tolower
   do the right thing for char32_t values that are not valid characters.
   POSIX says the behavior is undefined, so play it safe elsewhere.
   Do not rely on UCORE_C32_SAFE for c32width.  */
#ifdef __GLIBC__
enum { UCORE_C32_SAFE = true };
#else
enum { UCORE_C32_SAFE = false };
#endif

#ifndef _GL_LIKELY
/* Rely on __builtin_expect, as provided by the module 'builtin-expect'.  */
# define _GL_LIKELY(cond) __builtin_expect ((cond), 1)
# define _GL_UNLIKELY(cond) __builtin_expect ((cond), 0)
#endif

_GL_INLINE_HEADER_BEGIN
#ifndef UCORE_INLINE
# define UCORE_INLINE _GL_INLINE
#endif

/* Return true if C represents an encoding error, false otherwise.  */
UCORE_INLINE bool
ucore_iserr (ucore_t c)
{
  /* (c & UCORE_ERR_MIN) is a bit cheaper than (UCORE_ERR_MIN <= c)
     with GCC 13 x86-64.  */
  if (_GL_UNLIKELY (c & UCORE_ERR_MIN))
    {
      assume (UCORE_ERR_MIN <= c && c <= UCORE_ERR_MAX);
      return true;
    }
  else
    {
      assume (0 <= c && c <= UCORE_CHAR_MAX);
      return false;
    }
}

/* Whether the uchar predicate P accepts C, e.g., ucore_is (c32isalpha, C).  */
UCORE_INLINE bool
ucore_is (int (*p) (wint_t), wint_t c)
{
  /* When C is out of range, predicates based on glibc return false.
     Behavior is undefined on other platforms, so play it safe.  */
  return (UCORE_C32_SAFE || ! ucore_iserr (c)) && p (c);
}

/* Apply the uchar translator TO to C, e.g., ucore_to (c32tolower, C).  */
UCORE_INLINE wint_t
ucore_to (wint_t (*to) (wint_t), ucore_t c)
{
  return UCORE_C32_SAFE || ! ucore_iserr (c) ? to (c) : c;
}

/* Compare C1 and C2, with encoding errors sorting after characters.
   Return <0, 0, >0 for <, =, >.  */
UCORE_INLINE int
ucore_cmp (ucore_t c1, ucore_t c2)
{
  return c1 - c2;
}

/* Apply the uchar translater TO to C1 and C2 and compare the results,
   with encoding errors sorting after characters,
   Return <0, 0, >0 for <, =, >.  */
UCORE_INLINE int
ucore_tocmp (wint_t (*to) (wint_t), ucore_t c1, ucore_t c2)
{
  if (c1 == c2)
    return 0;
  int i1 = ucore_to (to, c1), i2 = ucore_to (to, c2);
  return i1 - i2;
}

_GL_INLINE_HEADER_END

#endif /* _MCEL_H */
