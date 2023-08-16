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
#include <mcel.h>

#include <ctype.h>
#include <stdlib.h>

int
mcel_casecmp (char const *s1, char const *s2)
{
  char const *p1 = s1;
  char const *p2 = s2;

  /* Do not look at the entire extent of S1 or S2 until needed:
     when two strings differ, the difference is typically early.  */
  if (MB_CUR_MAX == 1)
    while (true)
      {
	static_assert (UCHAR_MAX <= INT_MAX);
	unsigned char c1 = *p1++;
	unsigned char c2 = *p2++;
	int cmp = c1 - c2;
	if (_GL_UNLIKELY (cmp))
	  {
	    c1 = tolower (c1);
	    c2 = tolower (c2);
	    cmp = c1 - c2;
	  }
	if (_GL_UNLIKELY (cmp | !c1))
	  return cmp;
      }
  else
    while (true)
      {
	mcel_t g1 = mcel_scanz (p1); p1 += g1.len;
	mcel_t g2 = mcel_scanz (p2); p2 += g2.len;
	int cmp = ucore_tocmp (c32tolower, g1.c, g2.c);
	if (_GL_UNLIKELY (cmp | !g1.c))
	  return cmp;
      }
}
