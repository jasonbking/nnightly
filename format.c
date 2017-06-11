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
#include "format.h"
#include "util.h"

static void format_cmd(struct format_opts *, char **);
static void format_cc(struct format_opts *, char **);
static void format_lint(struct format_opts *, char **);
static char **split_lines(const char *);
static void newline(FILE *, size_t *);

#define	CHUNK_SZ (64)

void *
format_output(void *arg)
{
	struct format_opts *opts = arg;
	char *line = NULL;
	size_t linesz = 0;
	ssize_t n = 0;

	while ((n = getline(&line, &linesz, opts->in)) > 0) {
		if (line[n - 1] == '\n')
			line[n - 1] = '\0';

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

static void
format_cmd(struct format_opts *opts, char **words)
{
	size_t pos = 0;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if (i == 0) {
			if ((n = fprintf(opts->out, "%s", words[0])) < 0)
				err(EXIT_FAILURE, "fprintf");
			pos += n;
			continue;
		}

		if ((len + opts->indent < opts->cols) &&
		    (pos + len + 2 > opts->cols))
			newline(opts->out, &pos);

		if (pos == 0) {
			n = fprintf(opts->out, "%*s", opts->indent, "");
			if (n < 0)
				err(EXIT_FAILURE, "fprintf");
			pos += n;
		} else {
			(void) fputc(' ', opts->out);
			pos++;
		}

		if ((n = fprintf(opts->out, "%s", words[i])) < 0)
			err(EXIT_FAILURE, "fprintf");

		pos += n;
	}

	(void) fputc('\n', opts->out);
}

static void
format_cc(struct format_opts *opts, char **words)
{
	size_t width = 0;
	boolean_t own_line = B_FALSE;
	boolean_t fflag = B_FALSE;
	boolean_t Wflag = B_FALSE;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if (i == 0) {
			n = fprintf(opts->out, "%s", words[0]);
			if (n < 0)
				err(EXIT_FAILURE, "fprintf");
			width += n;
			continue;
		}

		if (width + len + 2 > opts->cols)
			newline(opts->out, &width);

		/* put these on their own lines */
		if (strchr(words[i], '=') != NULL ||
		    starts_with(words[i], "-I") ||
		    starts_with(words[i], "-L") ||
		    starts_with(words[i], "-D")) {
			newline(opts->out, &width);
			own_line = B_TRUE;
		}

		if (starts_with(words[i], "-f")) {
			if (!fflag) {
				newline(opts->out, &width);
				fflag = B_TRUE;
			}
		} else {
			fflag = B_FALSE;
		}

		if (starts_with(words[i], "-W")) {
			if (!Wflag) {
				newline(opts->out, &width);
				Wflag = B_TRUE;
			}
		} else {
			Wflag = B_FALSE;
		}

		if (width == 0) {
			n = fprintf(opts->out, "%*s", opts->indent, "");
			if (n < 0)
				err(EXIT_FAILURE, "fprintf");
			width += n;
		} else {
			(void) fputc(' ', opts->out);
			width++;
		}

		n = fprintf(opts->out, "%s", words[i]);
		if (n < 0)
			err(EXIT_FAILURE, "fprintf");
		width += n;

		if (own_line) {
			newline(opts->out, &width);
			own_line = B_FALSE;
		}
	}

	(void) fputc('\n', opts->out);
}

static void
format_lint(struct format_opts *opts, char **words)
{
	size_t width = 0;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if ((width > 0) && (width + len + 2 > opts->cols)) {
			(void) fputc(' ', opts->out);
			(void) fputc('\\', opts->out);
			(void) fputc('\n', opts->out);
			width = 0;
		}

		if (width == 0) {
			if (i > 0) {
				n = fprintf(opts->out, "%.*s", opts->indent,
				    "");
				if (n < 0)
					err(EXIT_FAILURE, "fprintf");
				width += n;
			}
		} else {
			(void) fputc(' ', opts->out);
			width++;
		}

		n = fprintf(opts->out, "%s", words[i]);
		if (n < 0)
			err(EXIT_FAILURE, "fprintf");
		width += n;
	}

	(void) fputc('\n', opts->out);
}

/* Try to split honoring shell-like quoting */		
static char **
split_lines(const char *line)
{
	const char *p = line;
	const char *start = NULL;
	char **words = NULL;
	size_t words_alloc = 0;
	size_t words_n = 0;
	size_t len = 0;
	boolean_t in_space = B_TRUE;
	boolean_t quote = B_FALSE;
	boolean_t dquote = B_FALSE;
	boolean_t litnext = B_FALSE;

	while (*p != '\0') {
		p = skip_whitespace(p);
 		if (p == '\0')
			break;

		quote = dquote = B_FALSE;
		start = p;

		if (words_n + 2 >= words_alloc) {
			char **temp = NULL;
			size_t newlen = words_alloc + CHUNK_SZ;

			temp = realloc(words, newlen * sizeof (char *));
			if (temp == NULL)
				err(EXIT_FAILURE, "realloc");

			words = temp;
			words_alloc = newlen;
		}

		while (*p != '\0') {
			if (quote) {
				if (*p == '\'')
					quote = B_FALSE;
				p++;
				continue;
			}

			if (litnext) {
				litnext = B_FALSE;
				p++;
				continue;
			}

			if (dquote) {
				if (*p == '"')
					dquote = B_FALSE;
				p++;
				continue;
			}

			switch (*p) {
			case '\'':
				quote = B_TRUE;
				break;
			case '"':
				dquote = B_TRUE;
				break;
			case '\\':
				litnext = B_TRUE;
				break;
			}

			if (*p == ' ' || *p == '\t')
				break;
			p++;
		}

		len = (size_t)(p - start) + 1;
		if ((words[words_n] = calloc(1, len)) == NULL)
			err(EXIT_FAILURE, "calloc");

		(void) strncpy(words[words_n++], start, len - 1);
	}

	if (words_n > 0)
		words[words_n] = NULL;

	return (words);
}

static void
newline(FILE *out, size_t *pos)
{
	if (*pos > 0) {
		(void) fprintf(out, " \\\n");
		*pos = 0;
	}
}
