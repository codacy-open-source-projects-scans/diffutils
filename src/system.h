/* System dependent declarations.

   Copyright (C) 1988-1989, 1992-1995, 1998, 2001-2002, 2004, 2006, 2009-2013,
   2015-2023 Free Software Foundation, Inc.

   This file is part of GNU DIFF.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

#include <count-leading-zeros.h>
#include <verify.h>

#include <sys/types.h>

#include <sys/stat.h>
#include <stat-macros.h>
#include <stat-time.h>
#include <timespec.h>

#ifndef STAT_BLOCKSIZE
# if HAVE_STRUCT_STAT_ST_BLKSIZE
#  define STAT_BLOCKSIZE(s) ((s).st_blksize)
# else
#  define STAT_BLOCKSIZE(s) (8 * 1024)
# endif
#endif

#include <unistd.h>

#include <fcntl.h>
#include <time.h>

#include <sys/wait.h>

#include <dirent.h>
#ifndef _D_EXACT_NAMLEN
# define _D_EXACT_NAMLEN(dp) strlen ((dp)->d_name)
#endif

#include <stdlib.h>
#define EXIT_TROUBLE 2

#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdckdint.h>
#include <stddef.h>
#include <string.h>

#include <gettext.h>
#if ! ENABLE_NLS
# undef textdomain
# define textdomain(Domainname) /* empty */
# undef bindtextdomain
# define bindtextdomain(Domainname, Dirname) /* empty */
#endif

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <ctype.h>

#include <errno.h>

#include <signal.h>
#if !defined SIGCHLD && defined SIGCLD
# define SIGCHLD SIGCLD
#endif

#include <attribute.h>
#include <c-ctype.h>
#include <idx.h>
#include <intprops.h>
#include <minmax.h>
#include <propername.h>
#include <same-inode.h>

#include "version.h"

/* Evaluate an assertion E that is guaranteed to be true.
   E should not crash, loop forever, or have side effects.  */
#if defined DDEBUG && !defined NDEBUG
/* Abort the program if E is false.  */
# include <assert.h>
# define dassert(e) assert (e)
#else
/* The compiler can assume E, as behavior is undefined otherwise.  */
# define dassert(e) assume (e)
#endif

_GL_INLINE_HEADER_BEGIN

#ifndef SYSTEM_INLINE
# define SYSTEM_INLINE _GL_INLINE
#endif

/* Type used for fast comparison of several bytes at a time.
   The type is a pointer to an incomplete struct,
   so that its values are less likely to be misused.
   This used to be uintmax_t, but changing it to the size of a pointer
   made plain 'cmp' 90% faster (GCC 4.8.1, x86).  */

#ifndef word
typedef struct incomplete *word;
#endif

/* The signed integer type of a line number.  Since files are read
   into main memory, ptrdiff_t should be wide enough.  pI is for
   printing line numbers.  */

typedef ptrdiff_t lin;
#define LIN_MAX PTRDIFF_MAX
#define pI "t"
static_assert (LIN_MAX == IDX_MAX);

/* This section contains POSIX-compliant defaults for macros
   that are meant to be overridden by hand in config.h as needed.  */

#ifndef file_name_cmp
# define file_name_cmp strcmp
#endif

#ifndef initialize_main
# define initialize_main(argcp, argvp)
#endif

#ifndef NULL_DEVICE
# define NULL_DEVICE "/dev/null"
#endif

/* Do struct stat *S, *T describe the same special file?  */
#ifndef same_special_file
# if HAVE_STRUCT_STAT_ST_RDEV && defined S_ISBLK && defined S_ISCHR
#  define same_special_file(s, t) \
     (((S_ISBLK ((s)->st_mode) && S_ISBLK ((t)->st_mode)) \
       || (S_ISCHR ((s)->st_mode) && S_ISCHR ((t)->st_mode))) \
      && (s)->st_rdev == (t)->st_rdev)
# else
#  define same_special_file(s, t) 0
# endif
#endif

/* Do struct stat *S, *T describe the same file?  Answer -1 if unknown.  */
#ifndef same_file
# define same_file(s, t) \
    (SAME_INODE (*s, *t) \
     || same_special_file (s, t))
#endif

/* Do struct stat *S, *T have the same file attributes?

   POSIX says that two files are identical if st_ino and st_dev are
   the same, but many file systems incorrectly assign the same (device,
   inode) pair to two distinct files, including:

   - GNU/Linux NFS servers that export all local file systems as a
     single NFS file system, if a local device number (st_dev) exceeds
     255, or if a local inode number (st_ino) exceeds 16777215.

   - Network Appliance NFS servers in snapshot directories; see
     Network Appliance bug #195.

   - ClearCase MVFS; see bug id ATRia04618.

   Check whether two files that purport to be the same have the same
   attributes, to work around instances of this common bug.  Do not
   inspect all attributes, only attributes useful in checking for this
   bug.

   Check first the attributes most likely to differ, for speed.
   Birthtime is special as st_birthtime is not portable,
   but when there is a birthtime it is most likely to differ.

   It's possible for two distinct files on a buggy file system to have
   the same attributes, but it's not worth slowing down all
   implementations (or complicating the configuration) to cater to
   these rare cases in buggy implementations.  */

#ifndef same_file_attributes
# define same_file_attributes(s, t) \
   (timespec_cmp (get_stat_birthtime (s), get_stat_birthtime (t)) == 0 \
    && (s)->st_ctime == (t)->st_ctime \
    && get_stat_ctime_ns (s) == get_stat_ctime_ns (t) \
    && (s)->st_mtime == (t)->st_mtime \
    && get_stat_mtime_ns (s) == get_stat_mtime_ns (t) \
    && (s)->st_size == (t)->st_size \
    && (s)->st_mode == (t)->st_mode \
    && (s)->st_uid == (t)->st_uid \
    && (s)->st_gid == (t)->st_gid \
    && (s)->st_nlink == (t)->st_nlink)
#endif

#define STREQ(a, b) (strcmp (a, b) == 0)

/* Return the floor of the log base 2 of N.  Return -1 if N is zero.  */
SYSTEM_INLINE int floor_log2 (idx_t n)
{
  static_assert (IDX_MAX <= ULLONG_MAX);
  return ULLONG_WIDTH - 1 - count_leading_zeros_ll (n);
}

_GL_INLINE_HEADER_END
