/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2017 Jason King
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <stdarg.h>
#include <sys/debug.h>
#include <sys/sysmacros.h> /* for ARRAY_SIZE */
#include "format.h"
#include "util.h"

static void format_cmd(struct format_opts *, char **);
static void format_cc(struct format_opts *, char **);
static void format_lint(struct format_opts *, char **);
static size_t writef(FILE *, const char *, ...);
static void newline(FILE *, size_t *);
static size_t pad(FILE *, int);
static inline boolean_t fits(const char *, size_t, size_t);
static size_t nextline(FILE *, char **, size_t *);

void *
format_output(void *arg)
{
	struct format_opts *opts = arg;
	char *line = NULL;
	size_t linesz = 0;
	size_t n = 0;

	while ((n = nextline(opts->in, &line, &linesz)) > 0) {
		char **words = split_lines(line);

		if (words == NULL || words[0] == NULL) {
			fputc('\n', opts->out);
			free(words);
			continue;
		}

		if (ends_with(words[0], "/cw"))
			format_cc(opts, words);
		else if (words[1] != NULL && strcmp(words[0], "+") == 0 &&
		    (ends_with(words[1], "/gcc") ||
		    ends_with(words[1], "/cc"))) {
			(void) fprintf(opts->out, "%s ", words[0]);
			format_cc(opts, words + 1);
		} else if (ends_with(words[0], "/lint")) {
			format_lint(opts, words);
		} else if (words[0][0] == '/') {
			format_cmd(opts, words);
		} else {
			(void) fprintf(opts->out, "%s\n", line);
		}

		if (ferror(opts->out))
			err(EXIT_FAILURE, "");

		for (size_t i = 0; words[i] != NULL; i++)
			free(words[i]);
		free(words);
	}

	(void) fclose(opts->in);
	(void) fclose(opts->out);
	return (NULL);
}

/* TODO: better splitting of lines using ; */
static void
format_cmd(struct format_opts *opts, char **words)
{
	size_t pos = 0;

	for (size_t i = 0; words[i] != NULL; i++) {
		if (i == 0) {
			pos = writef(opts->out, "%s", words[0]);
			continue;
		}

		if (!fits(words[i], pos, opts->cols))
			newline(opts->out, &pos);

		pos += pad(opts->out, (pos == 0) ? opts->indent : 1);
		pos += writef(opts->out, "%s", words[i]);

		if (feof(opts->out))
			return;
	}

	(void) writef(opts->out, "\n");
}

static void
format_cc(struct format_opts *opts, char **words)
{
	size_t pos = 0;
	boolean_t own_line = B_FALSE;
	boolean_t fflag = B_FALSE;
	boolean_t Wflag = B_FALSE;

	for (size_t i = 0; words[i] != NULL; i++) {
		if (i == 0) {
			pos = writef(opts->out, "%s", words[0]);
			continue;
		}

		if (!fits(words[i], pos, opts->cols))
			newline(opts->out, &pos);

		/* put these on their own lines */
		if (strchr(words[i], '=') != NULL ||
		    starts_with(words[i], "-I") ||
		    starts_with(words[i], "-L") ||
		    starts_with(words[i], "-D")) {
			newline(opts->out, &pos);
			own_line = B_TRUE;
		}

		if (starts_with(words[i], "-f")) {
			if (!fflag) {
				newline(opts->out, &pos);
				fflag = B_TRUE;
			}
		} else {
			fflag = B_FALSE;
		}

		if (starts_with(words[i], "-W")) {
			if (!Wflag) {
				newline(opts->out, &pos);
				Wflag = B_TRUE;
			}
		} else {
			Wflag = B_FALSE;
		}

		pos += pad(opts->out, (pos == 0) ? opts->indent : 1);
		pos += writef(opts->out, "%s", words[i]);

		if (own_line) {
			newline(opts->out, &pos);
			own_line = B_FALSE;
		}

		if (feof(opts->out))
			return;
	}

	(void) writef(opts->out, "\n");
}

static void
format_lint(struct format_opts *opts, char **words)
{
	size_t pos = 0;
	boolean_t own_line = B_FALSE;

	for (size_t i = 0; words[i] != NULL; i++) {
		if (i == 0) {
			pos = writef(opts->out, "%s", words[0]);
			continue;
		}

		if (!fits(words[i], pos, opts->cols))
			newline(opts->out, &pos);

		if (starts_with(words[i], "-I") ||
		    starts_with(words[i], "-D")) {
			newline(opts->out, &pos);
			own_line = B_TRUE;
		}

		pos += pad(opts->out, (pos == 0) ? opts->indent : 1);
		pos += writef(opts->out, "%s", words[i]);

		if (own_line) {
			newline(opts->out, &pos);
			own_line = B_FALSE;
		}

		if (feof(opts->out))
			return;
	}

	(void) writef(opts->out, "\n");
}

/* like getline, but will concatenate continuation lines together */
#define	LINE_SZ	(128)
static size_t
nextline(FILE *f, char **line, size_t *len)
{
	char *p = NULL;
	size_t n = 0;
	size_t size = 0;
	int c = -1;
	boolean_t slash = B_FALSE;	/* he is real */

	if (*line == NULL || *len < LINE_SZ) {
		if ((*line = realloc(*line, LINE_SZ)) == NULL)
			err(EXIT_FAILURE, "out of memory");
		*len = LINE_SZ;
	}

	size = *len;
	p = *line;

	while (1) {
		if ((c = getc_unlocked(f)) == EOF)
			break;

		*p++ = c;
		if (++n == size) {
			if ((*line = realloc(*line, size * 2)) == NULL)
				err(EXIT_FAILURE, "out of memory");

			p = *line;
			p += size;
			*len = size = 2 * size;
		}

		switch (c) {
		case '\n':
			if (slash) {
				p -= 2;
				n -= 2;
				*p = '\0';
				slash = B_FALSE;
				continue;
			}
			goto done;
		case '\\':
			slash = B_TRUE;
			break;
		default:
			slash = B_FALSE;
		}
	}

done:
	/* don't save trailing \n */
	if (c == '\n')
		p--;

	*p = '\0';
	return (n);
}

/* Will it blen^Wwrap? */
static inline boolean_t
fits(const char *word, size_t pos, size_t width)
{
	size_t len = strlen(word);

	if (pos + strlen(word) + 2 <= width)
		return (B_TRUE);
	return (B_FALSE);
}

/* wrap stdio functions and let them handle errors */
static void
newline(FILE *out, size_t *pos)
{
	if (*pos == 0)
		return;

	if (fprintf(out, " \\\n") < 0 && ferror(out))
		err(EXIT_FAILURE, "fprintf");
	*pos = 0;
}

static size_t
pad(FILE *out, int amt)
{
	int n = fprintf(out, "%*s", amt, "");

	if (n < 0) {
		if (feof(out))
			return (0);
		err(EXIT_FAILURE, "fprintf");
	}

	return ((size_t)n);
}

static size_t
writef(FILE *out, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vfprintf(out, fmt, ap);
	va_end(ap);

	if (n < 0) {
		if (feof(out))
			return (0);
		err(EXIT_FAILURE, "fprintf");
	}
	return ((size_t)n);
}
