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

#ifdef SYSTEM_INLINE
# define SYSTEM_EXTERN
#else
# define SYSTEM_EXTERN extern
# define SYSTEM_INLINE _GL_INLINE
#endif

#if (defined __linux__ || defined __CYGWIN__ || defined __FreeBSD__ \
     || defined __NetBSD__ || defined _AIX)
/* The device number of the /proc file system if known; zero otherwise.  */
SYSTEM_EXTERN dev_t proc_dev;
#endif

/* Use this for code that could be used if diff ever cares about
   st_size for symlinks, which it doesn't now.  */
#define care_about_symlink_size false

#if care_about_symlink_size && (defined __linux__ || defined __ANDROID__)
# include <sys/utsname.h>
/* 1 if symlink st_size is OK, -1 if not, 0 if unknown yet.  */
SYSTEM_EXTERN signed char symlink_size_ok;
#endif

_GL_INLINE_HEADER_BEGIN

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

/* Do struct stat *S, *T describe the same file?  */
SYSTEM_INLINE bool same_file (struct stat const *s, struct stat const *t)
{
  if (! SAME_INODE (*s, *t))
    {
# if HAVE_STRUCT_STAT_ST_RDEV
      /* Two character special files describe the same device if st_rdev
	 is the same, and likewise for block special devices.
	 They have the same contents, so treat them as the same.  */
      if (((S_ISCHR (s->st_mode) && S_ISCHR (t->st_mode))
	   || (S_ISBLK (s->st_mode) && S_ISBLK (t->st_mode)))
	  && s->st_rdev == t->st_rdev)
	return true;
# endif
      return false;
    }

  /* Although POSIX says that two files are identical if st_ino and st_dev
     are the same, all too many file systems incorrectly assign the same
     (device, inode) pair to two distinct files, including:

     - GNU/Linux NFS servers that export all local file systems as a
       single NFS file system, if a local (device, inode) pair collides
       with another one after hashing.

     - GNU/Linux NFS servers that export Btrfs file systems with subvolumes,
       if the Btrfs (subvolume, inode) hashing function collides.
       See <https://lwn.net/Articles/866709/>.

     - Qemu virtio-fs before Qemu 5.2 (2020); see
       <https://bugzilla.redhat.com/show_bug.cgi?id=1795362>.

     - Network Appliance NFS servers in snapshot directories; see
       Network Appliance bug #195.

     - ClearCase MVFS; see bug id ATRia04618.

     Check whether two files that purport to be the same have the same
     attributes, to work around instances of this common bug.

     Birthtime is special as st_birthtime is not portable, but when
     either birthtime is available comparing them should be definitive.  */

#if (defined HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC_TV_NSEC \
     || defined HAVE_STRUCT_STAT_ST_BIRTHTIM_TV_NSEC \
     || defined HAVE_STRUCT_STAT_ST_BIRTHTIMENSEC \
     || (defined _WIN32 && ! defined __CYGWIN__))
  /* If either file has a birth time, comparing them is definitive.  */
  timespec sbirth = get_stat_birthtime (s);
  timespec tbirth = get_stat_birthtime (t);
  if (0 <= sbirth.tv_nsec || 0 <= tbirth.tv_nsec)
    return timespec_cmp (sbirth, tbirth) == 0;
#endif

  /* Fall back on comparing other easily-obtainable attributes.
     Do not inspect all attributes, only attributes useful in checking
     for the bug.  Check attributes most likely to differ first.

     It's possible for two distinct files on a buggy file system to have
     the same attributes, but it's not worth slowing down all
     implementations (or complicating the configuration) to cater to
     these rare cases in buggy implementations.

     It's also possible for the same file to appear to be two different
     files, e.g., because its permissions were changed between the two
     stat calls.  In that case cmp and diff will do extra work
     to determine that the file contents are the same.  */

  return (get_stat_ctime_ns (s) == get_stat_ctime_ns (t)
	  && get_stat_mtime_ns (s) == get_stat_mtime_ns (t)
	  && s->st_ctime == t->st_ctime
	  && s->st_mtime == t->st_mtime
	  && s->st_size == t->st_size
	  && s->st_mode == t->st_mode
	  && s->st_uid == t->st_uid
	  && s->st_gid == t->st_gid
	  && s->st_nlink == t->st_nlink);
}

/* Return the number of bytes in the file described by *S,
   or -1 if this cannot be determined reliably.  */
SYSTEM_INLINE off_t stat_size (struct stat const *s)
{
  mode_t mode = s->st_mode;
  off_t size = s->st_size;
  if (size < 0)
    return -1;
  if (! (S_ISREG (mode) || (care_about_symlink_size && S_ISLNK (mode))
	 || S_TYPEISSHM (s) || S_TYPEISTMO (s)))
    return -1;

#if (defined __linux__ || defined __CYGWIN__ || defined __FreeBSD__ \
     || defined __NetBSD__ || defined _AIX)
  /* On some systems, /proc files with size zero are suspect.  */
  if (size == 0)
    {
      if (!proc_dev)
	{
	  struct stat st;
	  st.st_dev = 0;
	  lstat ("/proc/self", &st);
	  proc_dev = st.st_dev;
	}
      if (proc_dev && s->st_dev == proc_dev)
	return -1;
    }
#endif
#if care_about_symlink_size && (defined __linux__ || defined __ANDROID__)
  /* Symlinks have suspect sizes on Linux kernels before 5.15,
     due to bugs in fscrypt.  */
  if (S_ISLNK (mode))
    {
      if (! symlink_size_ok)
	{
	  struct utsname name;
	  uname (&name);
	  char *p = name.release;
	  symlink_size_ok = ((p[1] != '.' || '5' < p[0]
			      || (p[0] == '5'
				  && ('1' <= p[2] && p[2] <= '9')
				  && ('0' <= p[3] && p[3] <= '9')
				  && ('5' <= p[3]
				      || ('0' <= p[4] && p[4] <= '9'))))
			     ? 1 : -1);
	}
      if (symlink_size_ok < 0)
	return -1;
    }
#endif

  return size;
}

#define STREQ(a, b) (strcmp (a, b) == 0)

/* Return the floor of the log base 2 of N.  Return -1 if N is zero.  */
SYSTEM_INLINE int floor_log2 (idx_t n)
{
  static_assert (IDX_MAX <= ULLONG_MAX);
  return ULLONG_WIDTH - 1 - count_leading_zeros_ll (n);
}

_GL_INLINE_HEADER_END
