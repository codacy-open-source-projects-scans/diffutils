/* Support routines for GNU DIFF.

   Copyright (C) 1988-1989, 1992-1995, 1998, 2001-2002, 2004, 2006, 2009-2013,
   2015-2024 Free Software Foundation, Inc.

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

#include "diff.h"

#include <argmatch.h>
#include <diagnose.h>
#include <dirname.h>
#include <error.h>
#include <flexmember.h>
#include <mcel.h>
#include <quotearg.h>
#include <system-quote.h>
#include <xalloc.h>

#include <stdarg.h>
#include <signal.h>

/* Use SA_NOCLDSTOP as a proxy for whether the sigaction machinery is
   present.  */
#ifndef SA_NOCLDSTOP
# define SA_NOCLDSTOP 0
# define sigprocmask(How, Set, Oset) 0
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
#ifndef SIGTSTP
# define SIGTSTP 0
#endif

char const pr_program[] = PR_PROGRAM;

/* Queue up one-line messages to be printed at the end,
   when -l is specified.  Each message is recorded with a 'struct msg'.  */

struct msg
{
  struct msg *next;

  /* Msgid of printf-style format.  */
  char const *msgid;

  /* Number of bytes in ARGS.  */
  idx_t argbytes;

  /* Arg strings, each '\0' terminated, concatenated.  */
  char args[FLEXIBLE_ARRAY_MEMBER];
};

/* Head of the chain of queues messages.  */

static struct msg *msg_chain;

/* Tail of the chain of queues messages.  */

static struct msg **msg_chain_end = &msg_chain;

/* Use when a system call returns non-zero status.
   NAME should normally be the file name.  */

void
perror_with_name (char const *name)
{
  error (0, errno, "%s", squote (0, name));
}

/* Use when a system call returns non-zero status and that is fatal.  */

void
pfatal_with_name (char const *name)
{
  int e = errno;
  print_message_queue ();
  error (EXIT_TROUBLE, e, "%s", name);
}

/* Print an error message containing MSGID, then exit.  */

void
fatal (char const *msgid)
{
  print_message_queue ();
  error (EXIT_TROUBLE, 0, "%s", _(msgid));
}

/* Like printf, except if -l in effect then save the message and print later.
   Also, all arguments must be char * or char const *.
   This is used for things like "Only in ...".  */

void
message (char const *format_msgid, ...)
{
  va_list ap;
  va_start (ap, format_msgid);

  if (paginate)
    {
      idx_t argbytes = 0;

      for (char const *m = format_msgid; *m; m++)
	if (*m == '%')
	  {
	    if (m[1] == '%')
	      m++;
	    else
	      argbytes += strlen (va_arg (ap, char const *)) + 1;
	  }
      va_end (ap);

      struct msg *new = xmalloc (FLEXSIZEOF (struct msg, args, argbytes));
      new->msgid = format_msgid;
      new->argbytes = argbytes;

      va_start (ap, format_msgid);
      char *p = new->args;
      for (char const *m = format_msgid; *m; m++)
	if (*m == '%')
	  {
	    if (m[1] == '%')
	      m++;
	    else
	      p = stpcpy (p, va_arg (ap, char const *)) + 1;
	  }

      *msg_chain_end = new;
      new->next = nullptr;
      msg_chain_end = &new->next;
    }
  else
    {
      if (sdiff_merge_assist)
        putchar (' ');
      vprintf (_(format_msgid), ap);
    }

  va_end (ap);
}

/* Output all the messages that were saved up by calls to 'message'.  */

void
print_message_queue (void)
{
  for (struct msg *m = msg_chain; m; )
    {
      /* Change this if diff ever has messages with more than 4 args.  */
      char const *p = m->args;
      char const *plim = p + m->argbytes;
      /* Unroll the loop to work around GCC 12 bug with
	 -Wanalyzer-use-of-uninitialized-value.  */
      char const *arg0 = p; p += p < plim ? strlen (p) + 1 : 0;
      char const *arg1 = p; p += p < plim ? strlen (p) + 1 : 0;
      char const *arg2 = p; p += p < plim ? strlen (p) + 1 : 0;
      char const *arg3 = p; p += p < plim ? strlen (p) + 1 : 0;
      printf (_(m->msgid), arg0, arg1, arg2, arg3);
      dassert (plim <= p);
      struct msg *next = m->next;
      free (m);
      m = next;
    }
}

/* Signal handling, needed for restoring default colors.  */

static void
xsigaddset (sigset_t *set, int sig)
{
  if (sigaddset (set, sig) != 0)
    pfatal_with_name ("sigaddset");
}

static bool
xsigismember (sigset_t const *set, int sig)
{
  int mem = sigismember (set, sig);
  if (mem < 0)
    pfatal_with_name ("sigismember");
  assume (mem <= 1);
  return mem;
}

typedef void (*signal_handler) (int);
static signal_handler
xsignal (int sig, signal_handler func)
{
  signal_handler h = signal (sig, func);
  if (h == SIG_ERR)
    pfatal_with_name ("signal");
  return h;
}

static void
xsigprocmask (int how, sigset_t const *restrict set, sigset_t *restrict oset)
{
  if (sigprocmask (how, set, oset) != 0)
    pfatal_with_name ("sigprocmask");
}

/* If true, some signals are caught.  This is separate from
   'caught_signals' because POSIX doesn't require an all-zero sigset_t
   to be valid.  */
static bool some_signals_caught;

/* The set of signals that are caught.  */
static sigset_t caught_signals;

/* If nonzero, the value of the pending fatal signal.  */
static sig_atomic_t volatile interrupt_signal;

/* A count of the number of pending stop signals that have been received.  */
static sig_atomic_t volatile stop_signal_count;

/* An ordinary signal was received; arrange for the program to exit.  */

static void
sighandler (int sig)
{
  if (! SA_NOCLDSTOP)
    signal (sig, SIG_IGN);
  if (! interrupt_signal)
    interrupt_signal = sig;
}

/* A SIGTSTP was received; arrange for the program to suspend itself.  */

static void
stophandler (int sig)
{
  if (! SA_NOCLDSTOP)
    signal (sig, stophandler);
  if (! interrupt_signal)
    stop_signal_count++;
}
/* Process any pending signals.  If signals are caught, this function
   should be called periodically.  Ideally there should never be an
   unbounded amount of time when signals are not being processed.
   Signal handling can restore the default colors, so callers must
   immediately change colors after invoking this function.  */

static void
process_signals (void)
{
  while (interrupt_signal | stop_signal_count)
    {
      set_color_context (RESET_CONTEXT);
      fflush (stdout);

      sigset_t oldset;
      xsigprocmask (SIG_BLOCK, &caught_signals, &oldset);

      /* Reload stop_signal_count and (if needed) interrupt_signal, in
	 case a new signal was handled before sigprocmask took effect.  */
      int stops = stop_signal_count, sig;

      /* SIGTSTP is special, since the application can receive that signal
         more than once.  In this case, don't set the signal handler to the
         default.  Instead, just raise the uncatchable SIGSTOP.  */
      if (stops)
        {
          stop_signal_count = stops - 1;
          sig = SIGSTOP;
        }
      else
	{
	  sig = interrupt_signal;
	  xsignal (sig, SIG_DFL);
	}

      /* Exit or suspend the program.  */
      if (raise (sig) != 0)
	pfatal_with_name ("raise");
      xsigprocmask (SIG_SETMASK, &oldset, nullptr);

      /* If execution reaches here, then the program has been
         continued (after being suspended).  */
    }
}

/* The signals that can be caught, the number of such signals,
   and which of them are actually caught.  */
static int const sig[] =
  {
#if SIGTSTP
    /* This one is handled specially; see is_tstp_index.  */
    SIGTSTP,
#endif

    /* The usual suspects.  */
#ifdef SIGALRM
    SIGALRM,
#endif
#ifdef SIGHUP
    SIGHUP,
#endif
    SIGINT,
#ifdef SIGPIPE
    SIGPIPE,
#endif
#ifdef SIGQUIT
    SIGQUIT,
#endif
    SIGTERM,
#ifdef SIGPOLL
    SIGPOLL,
#endif
#ifdef SIGPROF
    SIGPROF,
#endif
#ifdef SIGVTALRM
    SIGVTALRM,
#endif
#ifdef SIGXCPU
    SIGXCPU,
#endif
#ifdef SIGXFSZ
    SIGXFSZ,
#endif
  };
enum { nsigs = sizeof (sig) / sizeof *(sig) };

/* True if sig[j] == SIGTSTP.  */
static bool
is_tstp_index (int j)
{
  return SIGTSTP && j == 0;
}

static void
install_signal_handlers (void)
{
  if (sigemptyset (&caught_signals) != 0)
    pfatal_with_name ("sigemptyset");

#if SA_NOCLDSTOP
  for (int j = 0; j < nsigs; j++)
    {
      struct sigaction actj;
      if (sigaction (sig[j], nullptr, &actj) == 0 && actj.sa_handler != SIG_IGN)
	xsigaddset (&caught_signals, sig[j]);
    }

  struct sigaction act;
  act.sa_mask = caught_signals;
  act.sa_flags = SA_RESTART;

  for (int j = 0; j < nsigs; j++)
    if (xsigismember (&caught_signals, sig[j]))
      {
	act.sa_handler = is_tstp_index (j) ? stophandler : sighandler;
	if (sigaction (sig[j], &act, nullptr) != 0)
	  pfatal_with_name ("sigaction");
	some_signals_caught = true;
      }
#else
  for (int j = 0; j < nsigs; j++)
    {
      signal_handler h = signal (sig[j], SIG_IGN);
      if (h != SIG_IGN && h != SIG_ERR)
	{
	  xsigaddset (&caught_signals, sig[j]);
	  xsignal (sig[j], is_tstp_index (j) ? stophandler : sighandler);
	  some_signals_caught = true;
	  if (siginterrupt (sig[j], 0) != 0)
	    pfatal_with_name ("siginterrupt");
	}
    }
#endif
}

/* Clean up signal handlers just before exiting the program.  Do this
   by resetting signal actions back to default, and then processing
   any signals that arrived before resetting.  */
void
cleanup_signal_handlers (void)
{
  if (some_signals_caught)
    {
      for (int j = 0; j < nsigs; j++)
	if (xsigismember (&caught_signals, sig[j]))
	  xsignal (sig[j], SIG_DFL);
      process_signals ();
    }
}

static char const *current_name[2];
static bool currently_recursive;
static bool colors_enabled;

struct bin_str
  {
    idx_t len;			/* Number of bytes */
    const char *string;		/* Pointer to the same */
  };

/* Parse a string as part of the --palette argument; this may involve
   decoding all kinds of escape characters.  If equals_end is set an
   unescaped equal sign ends the string, otherwise only a : or \0
   does.  Return true if successful.

   The resulting string is *not* null-terminated, but may contain
   embedded nulls.

   *dest and *src may point into the same string, in which case *dest
   must not exceed *src and the string is modified in place.

   Note that both dest and src are char **; on return they point to
   the first free byte after the array and the character that ended
   the input string, respectively.  */

static bool
get_funky_string (char **dest, const char **src, bool equals_end)
{
  enum {
    ST_GND, ST_BACKSLASH, ST_OCTAL, ST_HEX, ST_CARET, ST_END, ST_ERROR
  } state = ST_GND;

  char const *p = *src;		/* We don't want to double-indirect */
  char *q = *dest;		/* the whole darn time.  */

  char num = 0;			/* For numerical codes.  */

  while (state < ST_END)
    {
      switch (state)
        {
        case ST_GND:		/* Ground state (no escapes) */
          switch (*p)
            {
            case ':':
            case '\0':
              state = ST_END;	/* End of string */
              break;
            case '\\':
              state = ST_BACKSLASH; /* Backslash scape sequence */
              ++p;
              break;
            case '^':
              state = ST_CARET; /* Caret escape */
              ++p;
              break;
            case '=':
              if (equals_end)
                {
                  state = ST_END; /* End */
                  break;
                }
              FALLTHROUGH;
            default:
              *(q++) = *(p++);
              break;
            }
          break;

        case ST_BACKSLASH:	/* Backslash escaped character */
          switch (*p)
            {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
              state = ST_OCTAL;	/* Octal sequence */
              num = *p - '0';
              break;
            case 'x':
            case 'X':
              state = ST_HEX;	/* Hex sequence */
              num = 0;
              break;
            case 'a':		/* Bell */
              num = '\a';
              break;
            case 'b':		/* Backspace */
              num = '\b';
              break;
            case 'e':		/* Escape */
              num = 27;
              break;
            case 'f':		/* Form feed */
              num = '\f';
              break;
            case 'n':		/* Newline */
              num = '\n';
              break;
            case 'r':		/* Carriage return */
              num = '\r';
              break;
            case 't':		/* Tab */
              num = '\t';
              break;
            case 'v':		/* Vtab */
              num = '\v';
              break;
            case '?':		/* Delete */
              num = 127;
              break;
            case '_':		/* Space */
              num = ' ';
              break;
            case '\0':		/* End of string */
              state = ST_ERROR;	/* Error! */
              break;
            default:		/* Escaped character like \ ^ : = */
              num = *p;
              break;
            }
          if (state == ST_BACKSLASH)
            {
              *(q++) = num;
              state = ST_GND;
            }
          ++p;
          break;

        case ST_OCTAL:		/* Octal sequence */
          if (*p < '0' || *p > '7')
            {
              *(q++) = num;
              state = ST_GND;
            }
          else
            num = (num << 3) + (*(p++) - '0');
          break;

        case ST_HEX:		/* Hex sequence */
          switch (*p)
            {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
              num = (num << 4) + (*(p++) - '0');
              break;
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
              num = (num << 4) + (*(p++) - 'a') + 10;
              break;
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
              num = (num << 4) + (*(p++) - 'A') + 10;
              break;
            default:
              *(q++) = num;
              state = ST_GND;
              break;
            }
          break;

        case ST_CARET:		/* Caret escape */
          state = ST_GND;	/* Should be the next state... */
          if (*p >= '@' && *p <= '~')
            {
              *(q++) = *(p++) & 037;
            }
          else if (*p == '?')
            {
              *(q++) = 127;
            }
          else
            state = ST_ERROR;
          break;

        default:
	  unreachable ();
        }
    }

  *dest = q;
  *src = p;

  return state != ST_ERROR;
}

enum parse_state
  {
    PS_START = 1,
    PS_2,
    PS_3,
    PS_4,
    PS_DONE,
    PS_FAIL
  };

#define LEN_STR_PAIR(s) sizeof (s) - 1, s

static struct bin_str color_indicator[] =
  {
    { LEN_STR_PAIR ("\033[") },		/* lc: Left of color sequence */
    { LEN_STR_PAIR ("m") },		/* rc: Right of color sequence */
    { 0, nullptr },			/* ec: End color (replaces lc+rs+rc) */
    { LEN_STR_PAIR ("0") },		/* rs: Reset to ordinary colors */
    { LEN_STR_PAIR ("1") },		/* hd: Header */
    { LEN_STR_PAIR ("32") },		/* ad: Add line */
    { LEN_STR_PAIR ("31") },		/* de: Delete line */
    { LEN_STR_PAIR ("36") },		/* ln: Line number */
  };

static const char *const indicator_name[] =
  {
    "lc", "rc", "ec", "rs", "hd", "ad", "de", "ln", nullptr
  };
ARGMATCH_VERIFY (indicator_name, color_indicator);

static char *color_palette;

/* Set the color palette to PALETTE, a string that set_color_context
   can modify later.  */
void
set_color_palette (char *palette)
{
  color_palette = palette;
}

static void
parse_diff_color (void)
{
  /* Process color_palette into itself.  This saves a bit of memory,
     and pacifies Coverity.  The output is no larger than the input.  */
  char *buf = color_palette;
  char const *p = buf;
  if (p == nullptr || *p == '\0')
    return;
  /* Do not process the color palette twice.  */
  color_palette = nullptr;

  char label[] = "??";		/* Indicator label */

  enum parse_state state = PS_START;
  while (true)
    {
      switch (state)
        {
        case PS_START:		/* First label character */
          switch (*p)
            {
            case ':':
              ++p;
              break;

            case '*':
              ++p;
	      state = get_funky_string (&buf, &p, true) ? PS_4 : PS_FAIL;
              break;

            case '\0':
              state = PS_DONE;	/* Done! */
              goto done;

            default:	/* Assume it is file type label */
              label[0] = *(p++);
              state = PS_2;
              break;
            }
          break;

        case PS_2:		/* Second label character */
          if (*p)
            {
              label[1] = *(p++);
              state = PS_3;
            }
          else
            state = PS_FAIL;	/* Error */
          break;

        case PS_3:		/* Equal sign after indicator label */
          state = PS_FAIL;	/* Assume failure...  */
          if (*(p++) == '=')/* It *should* be...  */
            {
              for (int ind_no = 0; indicator_name[ind_no] != nullptr; ind_no++)
                {
                  if (STREQ (label, indicator_name[ind_no]))
                    {
		      char *str = buf;
		      if (get_funky_string (&buf, &p, false))
			{
			  color_indicator[ind_no].string = str;
			  color_indicator[ind_no].len = buf - str;
			  state = PS_START;
			}
                      break;
                    }
                }
              if (state == PS_FAIL)
                error (0, 0, _("unrecognized prefix: %s"), label);
            }
          break;

        case PS_4:		/* Equal sign after *.ext */
	  state = (*p++ == '=' && get_funky_string (&buf, &p, false)
		   ? PS_START : PS_FAIL);
          break;

        case PS_FAIL:
          goto done;

        default:
	  unreachable ();
        }
    }
 done:

  if (state == PS_FAIL)
    {
      error (0, 0,
             _("unparsable value for --palette"));
      colors_enabled = false;
    }
}

static void
check_color_output (bool is_pipe)
{
  bool output_is_tty;

  if (! outfile || colors_style == NEVER)
    return;

  output_is_tty = presume_output_tty || (!is_pipe && isatty (fileno (outfile)));

  colors_enabled = (colors_style == ALWAYS
                    || (colors_style == AUTO && output_is_tty));

  if (colors_enabled)
    parse_diff_color ();

  if (output_is_tty)
    install_signal_handlers ();
}

/* Call before outputting the results of comparing files NAME0 and NAME1
   to set up OUTFILE, the stdio stream for the output to go to.

   Usually, OUTFILE is just stdout.  But when -l was specified
   we fork off a 'pr' and make OUTFILE a pipe to it.
   'pr' then outputs to our stdout.  */

void
setup_output (char const *name0, char const *name1, bool recursive)
{
  current_name[0] = name0;
  current_name[1] = name1;
  currently_recursive = recursive;
  outfile = nullptr;
}

#if HAVE_WORKING_FORK
static pid_t pr_pid;
#endif


void
begin_output (void)
{
  if (outfile)
    return;

  char const *names[2];
  for (int f = 0; f < 2; f++)
    names[f] = quotearg_n_style (f,
				 (strchr (current_name[f], ' ')
				  ? c_quoting_style : c_maybe_quoting_style),
				 current_name[f]);

  /* Construct the header of this piece of diff.  */
  /* POSIX 1003.1-2017 specifies this format.  But there are some bugs in
     the standard: it says that we must print only the last component
     of the pathnames, and it requires two spaces after "diff" if
     there are no options.  These requirements are silly and do not
     match historical practice.  */
  char *name = xmalloc (sizeof "diff" + strlen (switch_string)
			+ 1 + strlen (names[0]) + 1 + strlen (names[1]));
  char *p = stpcpy (name, "diff");
  p = stpcpy (p, switch_string);
  *p++ = ' ';
  p = stpcpy (p, names[0]);
  *p++ = ' ';
  strcpy (p, names[1]);

  if (paginate)
    {
      if (fflush (stdout) != 0)
        pfatal_with_name (_("write failed"));

      char const *argv[4] = {pr_program, "-h", name, nullptr };

      /* Make OUTFILE a pipe to a subsidiary 'pr'.  */
#if HAVE_WORKING_FORK
      int pipes[2];
      if (pipe (pipes) != 0)
	pfatal_with_name ("pipe");

      pr_pid = fork ();
      if (pr_pid < 0)
	pfatal_with_name ("fork");

      if (pr_pid == 0)
	{
	  close (pipes[1]);
	  if (pipes[0] != STDIN_FILENO)
	    {
	      if (dup2 (pipes[0], STDIN_FILENO) < 0)
		pfatal_with_name ("dup2");
	      close (pipes[0]);
	    }

	  execv (pr_program, (char **) argv);
	  _exit (errno == ENOENT ? 127 : 126);
	}
      else
	{
	  close (pipes[0]);
	  outfile = fdopen (pipes[1], "w");
	  if (!outfile)
	    pfatal_with_name ("fdopen");
	  check_color_output (true);
	}
#else
      char *command = system_quote_argv (SCI_SYSTEM, (char **) argv);
      errno = 0;
      outfile = popen (command, "w");
      if (!outfile)
	pfatal_with_name (command);
      check_color_output (true);
      free (command);
#endif
    }
  else
    {

      /* If -l was not specified, output the diff straight to 'stdout'.  */

      outfile = stdout;
      check_color_output (false);

      /* If handling multiple files (because scanning a directory),
         print which files the following output is about.  */
      if (currently_recursive)
        puts (name);
    }

  free (name);

  /* A special header is needed at the beginning of context output.  */
  if (output_style == OUTPUT_CONTEXT || output_style == OUTPUT_UNIFIED)
    print_context_header (curr.file, names,
			  output_style == OUTPUT_UNIFIED);
}

/* Call after the end of output of diffs for one file.
   Close OUTFILE and get rid of the 'pr' subfork.  */

void
finish_output (void)
{
  if (outfile && outfile != stdout)
    {
      int wstatus;
      int werrno = 0;
      if (ferror (outfile))
        fatal ("write failed");
#if ! HAVE_WORKING_FORK
      wstatus = pclose (outfile);
      if (wstatus == -1)
        werrno = errno;
#else
      if (fclose (outfile) != 0)
        pfatal_with_name (_("write failed"));
      if (waitpid (pr_pid, &wstatus, 0) < 0)
        pfatal_with_name ("waitpid");
#endif
      int status = (! werrno && WIFEXITED (wstatus)
		    ? WEXITSTATUS (wstatus)
		    : INT_MAX);
      if (status)
        error (EXIT_TROUBLE, werrno,
               _(status == 126
		 ? "subsidiary program %s could not be invoked"
                 : status == 127
		 ? "subsidiary program %s not found"
                 : status == INT_MAX
		 ? "subsidiary program %s failed"
		 : "subsidiary program %s failed (exit status %d)"),
	       quote (pr_program), status);
    }

  outfile = nullptr;
}

/* Find the consecutive changes at the start of the script START.
   Return the last link before the first gap.  */

struct change *
find_change (struct change *script)
{
  return script;
}

/* Divide SCRIPT into pieces by calling HUNKFUN and
   print each piece with PRINTFUN.
   Both functions take one arg, an edit script.

   HUNKFUN is called with the tail of the script
   and returns the last link that belongs together with the start
   of the tail.

   PRINTFUN takes a subscript which belongs together (with a null
   link at the end) and prints it.  */

void
print_script (struct change *script,
              struct change * (*hunkfun) (struct change *),
              void (*printfun) (struct change *))
{
  struct change *next = script;

  while (next)
    {
      /* Find a set of changes that belong together.  */
      struct change *this = next;
      struct change *end = (*hunkfun) (next);

      /* Disconnect them from the rest of the changes,
         making them a hunk, and remember the rest for next iteration.  */
      next = end->link;
      end->link = nullptr;
#ifdef DEBUG
      debug_script (this);
#endif

      /* Print this hunk.  */
      (*printfun) (this);

      /* Reconnect the script so it will all be freed properly.  */
      end->link = next;
    }
}

/* Print the text of a single line LINE,
   flagging it with the characters in LINE_FLAG (which say whether
   the line is inserted, deleted, changed, etc.).  LINE_FLAG must not
   end in a blank, unless it is a single blank.  */

void
print_1_line (char const *line_flag, char const *const *line)
{
  print_1_line_nl (line_flag, line, false);
}

/* Print the text of a single line LINE,
   flagging it with the characters in LINE_FLAG (which say whether
   the line is inserted, deleted, changed, etc.).  LINE_FLAG must not
   end in a blank, unless it is a single blank.  If SKIP_NL is set, then
   the final '\n' is not printed.  */

void
print_1_line_nl (char const *line_flag, char const *const *line, bool skip_nl)
{
  char const *base = line[0], *limit = line[1]; /* Help the compiler.  */
  FILE *out = outfile; /* Help the compiler some more.  */
  char const *flag_format = nullptr;

  /* If -T was specified, use a Tab between the line-flag and the text.
     Otherwise use a Space (as Unix diff does).
     Print neither space nor tab if line-flags are empty.
     But omit trailing blanks if requested.  */

  if (line_flag && *line_flag)
    {
      char const *flag_format_1 = flag_format = initial_tab ? "%s\t" : "%s ";
      char const *line_flag_1 = line_flag;

      if (suppress_blank_empty && **line == '\n')
        {
          flag_format_1 = "%s";

          /* This hack to omit trailing blanks takes advantage of the
             fact that the only way that LINE_FLAG can end in a blank
             is when LINE_FLAG consists of a single blank.  */
          line_flag_1 += *line_flag_1 == ' ';
        }

      fprintf (out, flag_format_1, line_flag_1);
    }

  output_1_line (base, limit - (skip_nl && limit[-1] == '\n'), flag_format, line_flag);

  if ((!line_flag || line_flag[0]) && limit[-1] != '\n')
    {
      set_color_context (RESET_CONTEXT);
      fprintf (out, "\n\\ %s\n", _("No newline at end of file"));
    }
}

/* Output a line from BASE up to LIMIT.
   With -t, expand white space characters to spaces, and if FLAG_FORMAT
   is nonzero, output it with argument LINE_FLAG after every
   internal carriage return, so that tab stops continue to line up.  */

void
output_1_line (char const *base, char const *limit, char const *flag_format,
               char const *line_flag)
{
  enum { MAX_CHUNK = 1024 };
  if (!expand_tabs)
    {
      idx_t left = limit - base;
      while (left)
        {
          idx_t to_write = MIN (left, MAX_CHUNK);
          idx_t written = fwrite (base, sizeof (char), to_write, outfile);
          process_signals ();
          if (written < to_write)
            return;
          base += written;
          left -= written;
        }
    }
  else
    {
      FILE *out = outfile;
      char const *t = base;
      intmax_t tab = 0, column = 0, tab_size = tabsize;
      int counter_proc_signals = 0;

      while (t < limit)
        {
          counter_proc_signals++;
          if (counter_proc_signals == MAX_CHUNK)
            {
              process_signals ();
              counter_proc_signals = 0;
            }

	  switch (*t)
            {
            case '\t':
	      t++;
	      do
		if (putc (' ', out) < 0)
		  return;
	      while (++column < tab_size);

	      tab++;
	      column = 0;
              break;

            case '\r':
	      t++;
	      if (putc ('\r', out) < 0)
		return;
              if (flag_format && t < limit && *t != '\n')
		if (fprintf (out, flag_format, line_flag) < 0)
		  return;
              tab = column = 0;
              break;

            case '\b':
	      t++;
	      if (0 < column)
		column--;
	      else if (0 < tab)
		{
		  tab--;
		  column = tab_size - 1;
		}
	      else
		continue;
	      if (putc ('\b', out) < 0)
		return;
              break;

            default:;
	      mcel_t g = mcel_scan (t, limit);
	      column += g.err ? 1 : c32isprint (g.ch) ? c32width (g.ch) : 0;
	      tab += column / tab_size;
	      column %= tab_size;
	      if (fwrite (t, sizeof *t, g.len, outfile) != g.len)
		return;
	      t += g.len;
              break;
            }
        }
    }
}

enum indicator_no
  {
    C_LEFT, C_RIGHT, C_END, C_RESET, C_HEADER, C_ADD, C_DELETE, C_LINE
  };

static void
put_indicator (const struct bin_str *ind)
{
  fwrite (ind->string, ind->len, 1, outfile);
}

static enum color_context last_context = RESET_CONTEXT;

void
set_color_context (enum color_context color_context)
{
  if (color_context != RESET_CONTEXT)
    process_signals ();
  if (colors_enabled && last_context != color_context)
    {
      put_indicator (&color_indicator[C_LEFT]);
      switch (color_context)
        {
        case HEADER_CONTEXT:
          put_indicator (&color_indicator[C_HEADER]);
          break;

        case LINE_NUMBER_CONTEXT:
          put_indicator (&color_indicator[C_LINE]);
          break;

        case ADD_CONTEXT:
          put_indicator (&color_indicator[C_ADD]);
          break;

        case DELETE_CONTEXT:
          put_indicator (&color_indicator[C_DELETE]);
          break;

        case RESET_CONTEXT:
          put_indicator (&color_indicator[C_RESET]);
          break;

        default:
          unreachable ();
        }
      put_indicator (&color_indicator[C_RIGHT]);
      last_context = color_context;
    }
}


char const change_letter[] = { '\0', 'd', 'a', 'c' };

/* Translate an internal line number (an index into diff's table of lines)
   into an actual line number in the input file.
   The internal line number is I.  FILE points to the data on the file.

   Internal line numbers count from 0 starting after the prefix.
   Actual line numbers count from 1 within the entire file.  */

lin
translate_line_number (struct file_data const *file, lin i)
{
  return i + file->prefix_lines + 1;
}

/* Translate a line number range.  */

void
translate_range (struct file_data const *file,
                 lin a, lin b,
                 lin *aptr, lin *bptr)
{
  *aptr = translate_line_number (file, a - 1) + 1;
  *bptr = translate_line_number (file, b + 1) - 1;
}

/* Print a pair of line numbers with SEPCHAR, translated for file FILE.
   If the two numbers are identical, print just one number.

   Args A and B are internal line numbers.
   We print the translated (real) line numbers.  */

void
print_number_range (char sepchar, struct file_data *file, lin a, lin b)
{
  lin trans_a, trans_b;
  translate_range (file, a, b, &trans_a, &trans_b);

  /* Note: we can have B < A in the case of a range of no lines.
     In this case, we should print the line number before the range,
     which is B.  */
  if (trans_b > trans_a)
    fprintf (outfile, "%"pI"d%c%"pI"d", trans_a, sepchar, trans_b);
  else
    fprintf (outfile, "%"pI"d", trans_b);
}

/* Look at a hunk of edit script and report the range of lines in each file
   that it applies to.  HUNK is the start of the hunk, which is a chain
   of 'struct change'.  The first and last line numbers of file 0 are stored in
   *FIRST0 and *LAST0, and likewise for file 1 in *FIRST1 and *LAST1.
   Note that these are internal line numbers that count from 0.

   If no lines from file 0 are deleted, then FIRST0 is LAST0+1.

   Return UNCHANGED if only ignorable lines are inserted or deleted,
   OLD if lines of file 0 are deleted,
   NEW if lines of file 1 are inserted,
   and CHANGED if both kinds of changes are found. */

enum changes
analyze_hunk (struct change *hunk,
              lin *first0, lin *last0,
              lin *first1, lin *last1)
{
  bool trivial = ignore_blank_lines || ignore_regexp.fastmap;
  int trivial_length = ignore_blank_lines - 1;
    /* If 0, ignore zero-length lines;
       if -1, do not ignore lines just because of their length.  */

  bool skip_white_space =
    ignore_blank_lines && IGNORE_TRAILING_SPACE <= ignore_white_space;
  bool skip_leading_white_space =
    skip_white_space && IGNORE_SPACE_CHANGE <= ignore_white_space;

  /* Help the compiler.  */
  char const *const *linbuf0 = curr.file[0].linbuf;
  char const *const *linbuf1 = curr.file[1].linbuf;

  lin show_from = 0, show_to = 0;

  *first0 = hunk->line0;
  *first1 = hunk->line1;

  struct change *next = hunk;
  lin l0, l1;
  do
    {
      l0 = next->line0 + next->deleted - 1;
      l1 = next->line1 + next->inserted - 1;
      show_from += next->deleted;
      show_to += next->inserted;

      for (lin i = next->line0; i <= l0 && trivial; i++)
        {
          char const *line = linbuf0[i];
          char const *lastbyte = linbuf0[i + 1] - 1;
          char const *newline = lastbyte + (*lastbyte != '\n');
          idx_t len = newline - line;
          char const *p = line;
          if (skip_white_space)
            while (*p != '\n')
	      {
		mcel_t g = mcel_scan (p, newline);
		if (! c32isspace (g.ch))
		  {
		    if (! skip_leading_white_space)
		      p = line;
		    break;
		  }
		p += g.len;
	      }
          if (newline - p != trivial_length
              && (! ignore_regexp.fastmap
                  || (re_search (&ignore_regexp, line, len, 0, len, nullptr)
		      < 0)))
            trivial = false;
        }

      for (lin i = next->line1; i <= l1 && trivial; i++)
        {
          char const *line = linbuf1[i];
          char const *lastbyte = linbuf1[i + 1] - 1;
          char const *newline = lastbyte + (*lastbyte != '\n');
          idx_t len = newline - line;
          char const *p = line;
          if (skip_white_space)
            while (*p != '\n')
	      {
		mcel_t g = mcel_scan (p, newline);
		if (! c32isspace (g.ch))
		  {
		    if (! skip_leading_white_space)
		      p = line;
		    break;
		  }
		p += g.len;
	      }
          if (newline - p != trivial_length
              && (! ignore_regexp.fastmap
                  || (re_search (&ignore_regexp, line, len, 0, len, nullptr)
		      < 0)))
            trivial = false;
        }
    }
  while ((next = next->link));

  *last0 = l0;
  *last1 = l1;

  /* If all inserted or deleted lines are ignorable,
     tell the caller to ignore this hunk.  */

  if (trivial)
    return UNCHANGED;

  return (show_from ? OLD : UNCHANGED) | (show_to ? NEW : UNCHANGED);
}

#ifdef DEBUG
void
debug_script (struct change *sp)
{
  fflush (stdout);

  for (; sp; sp = sp->link)
    fprintf (stderr, "%3"pI"d %3"pI"d delete %"pI"d insert %"pI"d\n",
	     sp->line0, sp->line1, sp->deleted, sp->inserted);

  fflush (stderr);
}
#endif
