/* Case-insensitive string comparison function.
   Copyright 2023 Free Software Foundation, Inc.

   This file is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   This file is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

#include <config.h>

/* Specification.  */
#include <mbcel.h>

#include <ctype.h>
#include <stdlib.h>

/* Compare the multi-byte strings S1 and S2 lexicographically, ignoring case.
   Return <0, 0, >0 for <, =, >.  Consider encoding errors to be
   greater than characters and compare them byte by byte.  */

int
mbcel_strcasecmp (char const *s1, char const *s2)
{
  char const *p1 = s1;
  char const *p2 = s2;

  /* Do not look at the entire extent of S1 or S2 until needed:
     when two strings differ, the difference is typically early.  */
  if (MB_CUR_MAX == 1)
    while (true)
      {
	unsigned char c1 = *p1++;
	unsigned char c2 = *p2++;
	int cmp = MBCEL_UCHAR_FITS ? c1 - c2 : _GL_CMP (c1, c2);
	if (_GL_UNLIKELY (cmp))
	  {
	    c1 = tolower (c1);
	    c2 = tolower (c2);
	    cmp = MBCEL_UCHAR_FITS ? c1 - c2 : _GL_CMP (c1, c2);
	  }
	if (_GL_UNLIKELY (cmp | !c1))
	  return cmp;
      }
  else
    while (true)
      {
	mbcel_t g1 = mbcel_scanz (p1); p1 += g1.len;
	mbcel_t g2 = mbcel_scanz (p2); p2 += g2.len;
	int cmp = mbcel_casecmp (g1, g2);
	if (_GL_UNLIKELY (cmp | ! (g1.ch | g1.err)))
	  return cmp;
      }
}
