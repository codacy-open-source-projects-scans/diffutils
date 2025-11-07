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

#include <config.h>

#include "syncsig.h"

#include "minmax.h"

#include <signal.h>
#include <stdcountof.h>

/* Use SA_NOCLDSTOP as a proxy for whether the sigaction machinery is
   present.  */
#ifndef SA_NOCLDSTOP
# define SA_NOCLDSTOP 0
# define sigprocmask(how, set, oset) 0
# if ! HAVE_SIGINTERRUPT
#  define siginterrupt(sig, flag) 0
# endif
#endif

#ifndef SA_RESTART
# define SA_RESTART 0
#endif
#ifndef SIGSTOP
# define SIGSTOP 0
#endif

/* The set of signals that are caught.  */
static sigset_t caught_signals;

/* The signals we can catch.
   This includes all catchable GNU/Linux or POSIX signals that by
   default are ignored, or that stop or terminate the process.
   It also includes SIGQUIT since that can come from the terminal.
   It excludes other signals that normally come from program failure.
   If you modify this table, also modify signal_count's initializer.  */
static int const catchable[] =
  {
    /* SIGABRT is normally from program failure.  */
    SIGALRM,
    /* SIGBUS is normally from program failure.  */
#ifdef SIGCHLD
    SIGCHLD,
#else
# define SIGCHLD 0
#endif
    /* SIGCLD (older platforms) is an alias for SIGCHLD.  */
#ifdef SIGCONT
    SIGCONT,
#else
# define SIGCONT 0
#endif
    /* SIGEMT is normally from program failure.  */
    /* SIGFPE is normally from program failure.  */
    SIGHUP,
    /* SIGILL is normally from program failure.  */
    /* SIGINFO is an alias for SIGPWR on Linux.  */
    SIGINT,
    /* SIGIO is an alias for SIGPOLL.  */
    /* SIGKILL can't be caught.  */
#ifdef SIGLOST
    SIGLOST,
#else
# define SIGLOST 0
#endif
    SIGPIPE,
#ifdef SIGPOLL /* Removed from POSIX.1-2024; still in Linux.  */
    SIGPOLL,
#else
# define SIGPOLL 0
#endif
#ifdef SIGPROF /* Removed from POSIX.1-2024; still in Linux.  */
    SIGPROF,
#else
# define SIGPROF 0
#endif
#ifdef SIGPWR
    SIGPWR,
#else
# define SIGPWR 0
#endif
    SIGQUIT,
    /* SIGSEGV is normally from program failure.  */
    /* Linux SIGSTKFLT is unused.  */
    /* SIGSTOP can't be caught.  */
    /* SIGSYS is normally from program failure.  */
    SIGTERM,
    /* SIGTRAP is normally from program failure.  */
#ifdef SIGTSTP
    SIGTSTP,
#else
# define SIGTSTP 0
#endif
#ifdef SIGTTIN
    SIGTTIN,
#else
# define SIGTTIN 0
#endif
#ifdef SIGTTOU
    SIGTTOU,
#else
# define SIGTTOU 0
#endif
#ifdef SIGURG
    SIGURG,
#else
# define SIGURG 0
#endif
#ifdef SIGUSR1
    SIGUSR1,
#else
# define SIGUSR1 0
#endif
#ifdef SIGUSR2
    SIGUSR2,
#else
# define SIGUSR2 0
#endif
#ifdef SIGVTALRM
    SIGVTALRM,
#else
# define SIGVTALRM 0
#endif
#ifdef SIGWINCH
    SIGWINCH,
#else
# define SIGWINCH 0
#endif
#ifdef SIGXCPU
    SIGXCPU,
#else
# define SIGXCPU 0
#endif
#ifdef SIGXFSZ
    SIGXFSZ,
#else
# define SIGXFSZ 0
#endif
  };

/* Number of pending signals received, for each signal type.  */
static sig_atomic_t volatile signal_count[] =
  {
    /* Explicitly initialize, so that the table is large enough.  */
    [SIGALRM] = 0,
    [SIGCHLD] = 0,
    [SIGCONT] = 0,
    [SIGHUP] = 0,
    [SIGINT] = 0,
    [SIGLOST] = 0,
    [SIGPIPE] = 0,
    [SIGPOLL] = 0,
    [SIGPROF] = 0,
    [SIGPWR] = 0,
    [SIGQUIT] = 0,
    [SIGTERM] = 0,
    [SIGTSTP] = 0,
    [SIGTTIN] = 0,
    [SIGTTOU] = 0,
    [SIGURG] = 0,
    [SIGUSR1] = 0,
    [SIGUSR2] = 0,
    [SIGVTALRM] = 0,
    [SIGWINCH] = 0,
    [SIGXCPU] = 0,
    [SIGXFSZ] = 0,
  };

/* This acts as bool though its type is sig_atomic_t.
   If true, signal_count might contain nonzero entries.
   If false, signal_count is all zero.
   This is to speed up syncsig_poll when it returns 0.  */
static sig_atomic_t volatile possible_signal_count;

/* Actions before syncsig_install was called.  */
#if SA_NOCLDSTOP
static struct sigaction oldact[countof (signal_count)];
#else
static void (*oldact[countof (signal_count)]) (int);
#endif

/* Record an asynchronous signal.  This function is async-signal-safe.  */
static void
sighandler (int sig)
{
#if !SA_NOCLDSTOP
  /* An unavoidable race here: the default action might mistakenly
     be taken before 'signal' is called.  */
  signal (sig, sighandler);
#endif

  possible_signal_count = true;
  signal_count[sig]++;
}

void
syncsig_install (int flags)
{
  for (int i = 0; i < countof (signal_count); i++)
    signal_count[i] = 0;
  possible_signal_count = false;

  sigemptyset (&caught_signals);

  for (int i = 0; i < countof (catchable); i++)
    {
      int sig = catchable[i];

      if (((sig == SIGTSTP) & !(flags & SYNCSIG_TSTP))
	  | ((sig == SIGTTIN) & !(flags & SYNCSIG_TTIN))
	  | ((sig == SIGTTOU) & !(flags & SYNCSIG_TTOU)))
	continue;

#if SA_NOCLDSTOP
      sigaction (sig, nullptr, &oldact[sig]);
      bool catching_sig = oldact[sig].sa_handler != SIG_IGN;
#else
      oldact[i] = signal (sig, SIG_IGN);
      bool catching_sig = oldact[i] != SIG_IGN;
      if (catching_sig)
	{
	  /* An unavoidable race here: SIG might be mistakenly ignored
	     before 'signal' is called.  */
	  signal (sig, sighandler);
	  siginterrupt (sig, 0);
	}
#endif
      if (catching_sig)
	sigaddset (&caught_signals, sig);
    }

#if SA_NOCLDSTOP
  struct sigaction act;
  act.sa_handler = sighandler;
  act.sa_mask = caught_signals;
  act.sa_flags = SA_RESTART;

  for (int i = 0; i < countof (catchable); i++)
    {
      int sig = catchable[i];
      if (sigismember (&caught_signals, sig))
	sigaction (sig, &act, nullptr);
    }
#endif
}

void
syncsig_uninstall (void)
{
  for (int i = 0; i < countof (catchable); i++)
    {
      int sig = catchable[i];
      if (sigismember (&caught_signals, sig))
	{
#if SA_NOCLDSTOP
	  sigaction (sig, &oldact[sig], nullptr);
#else
	  signal (sig, oldact[sig]);
#endif
	}
    }
}

int
syncsig_poll (void)
{
  int sig = 0;

  if (possible_signal_count)
    {
      /* This module uses static rather than thread-local storage,
	 so it is useful only in single-threaded programs,
	 and it uses sigprocmask rather than pthread_sigmask.  */
      sigset_t oldset;
      sigprocmask (SIG_BLOCK, &caught_signals, &oldset);

      for (int i = 0; ; i++)
	{
	  if (i == countof (catchable))
	    {
	      possible_signal_count = false;
	      break;
	    }
	  int s = catchable[i];
	  int c = signal_count[s];
	  if (c)
	    {
	      signal_count[s] = c - 1;
	      sig = s;
	      break;
	    }
	}

      sigprocmask (SIG_SETMASK, &oldset, nullptr);
    }

  return sig;
}

void
syncsig_deliver (int sig)
{
#if SA_NOCLDSTOP
  struct sigaction act;
#else
  void (*act) (int);
#endif

  if (sig == SIGTSTP)
    sig = SIGSTOP;
  else
    {
#if SA_NOCLDSTOP
      sigaction (sig, &oldact[sig], &act);
#else
      act = signal (sig, oldact[sig]);
#endif

    }

  raise (sig);

  if (sig != SIGSTOP)
    {
      /* The program did not exit due to the raised signal, so continue.  */
#if SA_NOCLDSTOP
      sigaction (sig, &act, nullptr);
#else
      signal (sig, act);
#endif
    }
}
