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
#include <pthread.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <errno.h>
#include "filter.h"

typedef struct {
	char **lines;
	size_t linenum;
	size_t n;
	size_t alloc;
} buffer_t;
#define	CHUNK_SZ	(16)

static void save_line(buffer_t *, const char *, size_t);
static void flush_lines(buffer_t *);
static void dump_lines(buffer_t *, FILE *, const char *);

void *
filter_errors(void *arg)
{
	struct filter_opts *opts = arg;
	buffer_t buf = { 0 };
	char *line = NULL;
	size_t linesz = 0;
	size_t linenum = 0;
	ssize_t n = 0;
	boolean_t save = B_FALSE;
	boolean_t saw_error = B_FALSE;

	while ((n = getline(&line, &linesz, opts->in)) > 0) {
		if (line[n - 1] == '\n')
			line[n - 1] = '\0';

		linenum++;

		if (!save) {
			if (strstr(line, " --> Job output") != NULL) {
				save = B_TRUE;
				saw_error = B_FALSE;
				save_line(&buf, line, linenum);
			}
			continue;
		}

		if ((strstr(line, "*** Error") == line) &&
		    (strstr(line, "ignored") == NULL))
			saw_error = B_TRUE;

		if (strstr(line, " --> Job output") != NULL ||
		    strstr(line, "==== Ended") == line) {
			if (saw_error)
				dump_lines(&buf, opts->out, opts->filename);

			save = saw_error = B_FALSE;
			flush_lines(&buf);
			continue;
		}

		save_line(&buf, line, linenum);
	}

	if (save && saw_error)
		dump_lines(&buf, opts->out, opts->filename);

	if (ferror(opts->in))
		err(EXIT_FAILURE, "%s", opts->filename);

	free(line);
	flush_lines(&buf);
	free(buf.lines);
	(void) fclose(opts->in);
	(void) fclose(opts->out);
	return (NULL);
}

static void
save_line(buffer_t *b, const char *line, size_t linenum)
{
	if (b->n + 1 > b->alloc) {
		char **temp = NULL; 
		size_t newalloc = b->alloc + CHUNK_SZ;
		size_t newsz = newalloc * sizeof (char *);

		if (newsz < newalloc || newsz < sizeof (char *))
			errx(EXIT_FAILURE, "overflow");

		temp = realloc(b->lines, newsz);
		if (temp == NULL)
			err(EXIT_FAILURE, "realloc");

		b->lines = temp;
		b->alloc = newalloc;
	}

	if (b->n == 0)
		b->linenum = linenum;

	if ((b->lines[b->n++] = strdup(line)) == NULL)
		err(EXIT_FAILURE, "strdup");
}

static void
flush_lines(buffer_t *b)
{
	for (size_t i = 0; i < b->n; i++) {
		free(b->lines[i]);
		b->lines[i] = NULL;
	}
	b->n = 0;
	b->linenum = 0;
}

static void
dump_lines(buffer_t *b, FILE *out, const char *filename)
{
	(void) fprintf(out, "#### %s:%zu\n", filename, b->linenum);
	for (size_t i = 0; i < b->n; i++) {
		if (fprintf(out, "%s\n", b->lines[i]) < 0)
			err(EXIT_FAILURE, "fprintf");
	}

	(void) fputc('\n', out);
	(void) fflush(out);
	if (ferror(out))
		err(EXIT_FAILURE, "fflush");
}
