/* Synchronous signal handling

   Copyright 2025 Free Software Foundation, Inc.

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

/* Written by Paul Eggert.  */

/* Flags for syncsig_install.  */
enum
  {
    /* Also catch SIGTSTP, SIGTTIN, SIGTTOU, which by default stop the process.
       These flags have no effect on platforms lacking these signals.  */
    SYNCSIG_TSTP = (1 << 0),
    SYNCSIG_TTIN = (1 << 1),
    SYNCSIG_TTOU = (1 << 2),
  };

/* Set up asynchronous signal handling according to FLAGS.
   syncsig_install fails only on unusual platforms where
   valid calls to functions like sigaction can fail;
   if it fails, signal handling is in a weird state
   and neither syncsig_process nor syncsig_uninstall should be called.  */
extern void syncsig_install (int flags);

/* Return a signal number if a signal has arrived, zero otherwise.
   After syncsig_install, there should not be an unbounded amount of
   time between calls to this function, and its result should be dealt
   with promptly.  */
extern int syncsig_poll (void);

/* Do the action for SIG that would have been done
   had syncsig_install not been called.
   SIG should have recently been returned by syncsig_poll.

   For example, if SIG is SIGTSTP stop the process and return after
   SIGCONT is delivered.  Another example: kill the process if SIG is
   SIGINT and SIGINT handling is the default.

   This function should be called only after syncsig_install.  */
extern void syncsig_deliver (int sig);

/* Stop doing asynchronous signal handling, undoing syncsig_install.
   This function should be called only after syncsig_install.
   To deal with signals arriving just before calling this function,
   call syncsig_poll afterwards.  */
extern void syncsig_uninstall (void);
