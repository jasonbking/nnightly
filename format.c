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

enum {
	SINGLE_LINE,
	GROUP
};

struct rules {
	const char *prefix;
	int action;
};

struct format_common {
	FILE *out;
	const struct rules *rules;
	size_t nrules;
	size_t cols;
	size_t last;
	int indent;
};

static struct rules cc_rules[] = {
	{ "-D", SINGLE_LINE },
	{ "-I", SINGLE_LINE },
	{ "-L", SINGLE_LINE },
	{ "-f", GROUP },
	{ "-W", GROUP }
};

static struct rules lint_rules[] = {
	{ "-D", SINGLE_LINE },
	{ "-I", SINGLE_LINE },
	{ "-erroff", SINGLE_LINE }
};

static void format_cmd(struct format_opts *, char **);
static void format_cc(struct format_opts *, char **);
static void format_lint(struct format_opts *, char **);
static void format_common(struct format_common *, const char *, size_t *,
    boolean_t);
static void format_common_init(struct format_common *, struct format_opts *,
    const struct rules *, size_t);
static char **split_lines(const char *);
static size_t writef(FILE *, const char *, ...);
static void newline(FILE *, size_t *);
static size_t indent(FILE *, int);

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

		if (i == 0) {
			pos = writef(opts->out, "%s", words[0]);
			continue;
		}

		if ((len + opts->indent < opts->cols) &&
		    (pos + len + 2 > opts->cols))
			newline(opts->out, &pos);

		pos += indent(opts->out, (pos == 0) ? opts->indent : 1);
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
		size_t len = strlen(words[i]);

		if (i == 0) {
			pos = writef(opts->out, "%s", words[0]);
			continue;
		}

		if (pos + len + 2 > opts->cols)
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

		pos += indent(opts->out, (pos == 0) ? opts->indent : 1);
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
	size_t width = 0;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if ((width > 0) && (width + len + 2 > opts->cols)) {
			(void) fprintf(opts->out, " \\\n");
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

static void
format_common(struct format_common *fc, const char *word, size_t *col,
    boolean_t firstword)
{
	size_t i = 0;

	if (firstword)
		fc->last = fc->nrules;

	for (size_t i = 0; i < fc->nrules; i++) {
		if (starts_with(word, fc->rules[i].prefix))
			break;
	}
	if (fc->last == fc->nrules)
		return;

	switch (fc->rules[i].action) {
	case SINGLE_LINE:
		newline(fc->out, col);
		break;
	case GROUP:
		if (fc->last != i)
			newline(fc->out, col);
		break;
	default:
		abort();
	}
	fc->last = i;
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

static size_t
indent(FILE *out, int amt)
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

static void
format_common_init(struct format_common *fc, struct format_opts *opts,
    const struct rules *rules, size_t nrules)
{
	fc->out = opts->out;
	fc->nrules = fc->last = nrules;
	fc->indent = opts->indent;
	fc->cols = opts->cols;
}
