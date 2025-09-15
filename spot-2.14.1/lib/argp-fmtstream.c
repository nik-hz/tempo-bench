/* Word-wrapping and line-truncating streams
   Copyright (C) 1997-2020 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Written by Miles Bader <miles@gnu.ai.mit.edu>.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* This package emulates glibc 'line_wrap_stream' semantics for systems that
   don't have that.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#include "argp-fmtstream.h"
#include "argp-namefrob.h"

#ifndef ARGP_FMTSTREAM_USE_LINEWRAP

#ifndef isblank
#define isblank(ch) ((ch)==' ' || (ch)=='\t')
#endif

#ifdef _LIBC
# include <wchar.h>
# include <libio/libioP.h>
# define __vsnprintf(s, l, f, a) _IO_vsnprintf (s, l, f, a)
#endif

#define INIT_BUF_SIZE 200
#define PRINTF_SIZE_GUESS 150

/* Return an argp_fmtstream that outputs to STREAM, and which prefixes lines
   written on it with LMARGIN spaces and limits them to RMARGIN columns
   total.  If WMARGIN >= 0, words that extend past RMARGIN are wrapped by
   replacing the whitespace before them with a newline and WMARGIN spaces.
   Otherwise, chars beyond RMARGIN are simply dropped until a newline.
   Returns NULL if there was an error.  */
argp_fmtstream_t
__argp_make_fmtstream (FILE *stream,
                       size_t lmargin, size_t rmargin, ssize_t wmargin)
{
  argp_fmtstream_t fs;

  fs = (struct argp_fmtstream *) malloc (sizeof (struct argp_fmtstream));
  if (fs != NULL)
    {
      fs->stream = stream;

      fs->lmargin = lmargin;
      fs->rmargin = rmargin;
      fs->wmargin = wmargin;
      fs->point_col = 0;

      fs->buf = (char *) malloc (INIT_BUF_SIZE);
      if (! fs->buf)
        {
          free (fs);
          fs = 0;
        }
      else
        {
          fs->p = fs->buf;
          fs->end = fs->buf + INIT_BUF_SIZE;
        }
    }

  return fs;
}
#if 0
/* Not exported.  */
#ifdef weak_alias
weak_alias (__argp_make_fmtstream, argp_make_fmtstream)
#endif
#endif

/* Flush FS to its stream, and free it (but don't close the stream).  */
void
__argp_fmtstream_free (argp_fmtstream_t fs)
{
  __argp_fmtstream_update (fs);
  if (fs->p > fs->buf)
    {
#ifdef _LIBC
      __fxprintf (fs->stream, "%.*s", (int) (fs->p - fs->buf), fs->buf);
#else
      fwrite_unlocked (fs->buf, 1, fs->p - fs->buf, fs->stream);
#endif
    }
  free (fs->buf);
  free (fs);
}
#if 0
/* Not exported.  */
#ifdef weak_alias
weak_alias (__argp_fmtstream_free, argp_fmtstream_free)
#endif
#endif


static void
write_block (argp_fmtstream_t fs, char *buf, int len)
{
#ifdef _LIBC
  __fxprintf (fs->stream, "%.*s", len, buf);
#else
  fwrite_unlocked (buf, 1, len, fs->stream);
#endif
}

/* Process FS's buffer so that line wrapping is done and flush all of it. So
   after return fs->p will be set to fb->buf.  */
void
__argp_fmtstream_update (argp_fmtstream_t fs)
{
  char *buf, *nl;
  size_t len;

  /* Scan the buffer for newlines.  */
  buf = fs->buf;
  while (buf < fs->p)
    {
      size_t r;

      if (fs->point_col == 0 && fs->lmargin != 0)
        {
          /* We are starting a new line.  Print spaces to the left margin.  */
	  size_t i;
	  for (i = 0; i < fs->lmargin; i++)
	    write_block(fs, " ", 1);
          fs->point_col = fs->lmargin;
        }

      len = fs->p - buf;
      nl = memchr (buf, '\n', len);

      if (fs->point_col < 0)
        fs->point_col = 0;

      if (!nl)
        {
          /* The buffer ends in a partial line.  */

          if (fs->point_col + len < fs->rmargin)
            {
              /* The remaining buffer text is a partial line and fits
                 within the maximum line width. Output the line and increment
                 point.  */
	      write_block(fs, buf, len);
              fs->point_col += len;
              break;
            }
          else
            /* Set the end-of-line pointer for the code below to
               the end of the buffer.  */
            nl = fs->p;
        }
      else if (fs->point_col + (nl - buf) < (ssize_t) fs->rmargin)
        {
          /* The buffer contains a full line that fits within the maximum
             line width.  Output the line, reset point and scan the next
             line.  */
	  write_block(fs, buf, nl + 1 - buf);
          fs->point_col = 0;
          buf = nl + 1;
          continue;
        }

      /* This line is too long.  */
      r = fs->rmargin - 1;

      if (fs->wmargin < 0)
        {
          /* Truncated everything past the right margin.  */
          if (nl < fs->p)
            {
	      write_block(fs, buf, r - fs->point_col);
	      write_block(fs, "\n", 1);
              /* Reset point for the next line and start scanning it.  */
              fs->point_col = 0;
              buf = nl + 1; /* Skip full line plus \n. */
	      continue;
            }
          else
            {
              /* The buffer ends with a partial line that is beyond the
                 maximum line width.  Advance point for the characters
                 written, and discard those past the max from the buffer.  */
	      write_block(fs, buf, r - fs->point_col);
	      fs->point_col += len;
              break;
            }
        }
      else
        {
          /* Do word wrap.  Go to the column just past the maximum line
             width and scan back for the beginning of the word there.
             Then insert a line break.  */

          char *p, *nextline;
          int i;

          p = buf + (r + 1 - fs->point_col);
          while (p >= buf && !isblank ((unsigned char) *p))
            --p;
          nextline = p + 1;     /* This will begin the next line.  */

          if (nextline > buf)
            {
              /* Swallow separating blanks.  */
              if (p >= buf)
                do
                  --p;
                while (p >= buf && isblank ((unsigned char) *p));
              nl = p + 1;       /* The newline will replace the first blank. */
            }
          else
            {
              /* A single word that is greater than the maximum line width.
                 Oh well.  Put it on an overlong line by itself.  */
              p = buf + (r + 1 - fs->point_col);
              /* Find the end of the long word.  */
              if (p < nl)
                do
                  ++p;
                while (p < nl && !isblank ((unsigned char) *p));
	      nl = p;
	      nextline = nl + 1;
	    }

	  write_block(fs, buf, nl - buf);
	  if (nextline < fs->p)
	    {
	      /* There are more lines to process. Do line break and print
		 blanks up to the wrap margin.  */
	      write_block(fs, "\n", 1);
	      for (i = 0; i < fs->wmargin; ++i)
		write_block(fs, " ", 1);
	      fs->point_col = fs->wmargin;
	    }
	  buf = nextline;
	}
    }

  /* Remember that we've flushed everything.  */
  fs->p = fs->buf;
}

/* Ensure that FS has space for AMOUNT more bytes in its buffer, either by
   growing the buffer, or by flushing it.  True is returned iff we succeed. */
int
__argp_fmtstream_ensure (struct argp_fmtstream *fs, size_t amount)
{
  if ((size_t) (fs->end - fs->p) < amount)
    {
      /* Flush FS's buffer.  */
      __argp_fmtstream_update (fs);

      if ((size_t) (fs->end - fs->buf) < amount)
        /* Gotta grow the buffer.  */
        {
          size_t old_size = fs->end - fs->buf;
          size_t new_size = old_size + amount;
          char *new_buf;

          if (new_size < old_size || ! (new_buf = realloc (fs->buf, new_size)))
            {
              __set_errno (ENOMEM);
              return 0;
            }

          fs->buf = new_buf;
          fs->end = new_buf + new_size;
          fs->p = fs->buf;
        }
    }

  return 1;
}

ssize_t
__argp_fmtstream_printf (struct argp_fmtstream *fs, const char *fmt, ...)
{
  int out;
  size_t avail;
  size_t size_guess = PRINTF_SIZE_GUESS; /* How much space to reserve. */

  do
    {
      va_list args;

      if (! __argp_fmtstream_ensure (fs, size_guess))
        return -1;

      va_start (args, fmt);
      avail = fs->end - fs->p;
      out = __vsnprintf (fs->p, avail, fmt, args);
      va_end (args);
      if ((size_t) out >= avail)
        size_guess = out + 1;
    }
  while ((size_t) out >= avail);

  fs->p += out;

  return out;
}
#if 0
/* Not exported.  */
#ifdef weak_alias
weak_alias (__argp_fmtstream_printf, argp_fmtstream_printf)
#endif
#endif

#endif /* !ARGP_FMTSTREAM_USE_LINEWRAP */
