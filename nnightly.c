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
#include <termios.h>
#include <errno.h>

static const char *input;
static size_t screen_width;
static int indent = 4;

static size_t get_screen_width(void);
static void *filter_errors(void *);
static void *format_output(void *);

int
main(int argc, const char **argv)
{
	FILE *pipes[2][2] = { 0 };
	int fds[2] = { -1, -1 };
	pthread_t tids[2] = { 0 };

	if (argc > 1) {
		if ((pipes[0][0] = fopen(argv[1], "rF")) == NULL)
			err(EXIT_FAILURE, argv[1]);
		input = argv[1];
	} else {
		pipes[0][0] = stdin;
		input = "(stdin)";
	}

	if (pipe(fds) < 0)
		err(EXIT_FAILURE, "pipe");

	if ((pipes[0][1] = fdopen(fds[0], "wF")) == NULL)
		err(EXIT_FAILURE, "fdopen");

	if ((pipes[1][0]  = fdopen(fds[1], "rF")) == NULL)
		err(EXIT_FAILURE, "fdopen");

	pipes[1][1] = stdout;

	screen_width = get_screen_width();

	pthread_create(&tids[0], NULL, filter_errors, pipes[0]);
	pthread_create(&tids[1], NULL, format_output, pipes[1]);
	pthread_join(tids[0], NULL);
	pthread_join(tids[1], NULL);

	return (0);
}

typedef struct {
	char **lines;
	size_t linenum;
	size_t n;
	size_t alloc;
} buffer_t;
#define	CHUNK_SZ	(16)

static void save_line(buffer_t *, const char *, size_t);
static void flush_lines(buffer_t *);
static void dump_lines(buffer_t *, FILE *);

static void *
filter_errors(void *arg)
{
	buffer_t buf = { 0 };
	FILE **pipes = arg;
	char *line = NULL;
	size_t linesz = 0;
	size_t linenum = 0;
	ssize_t n = 0;
	boolean_t save = B_FALSE;
	boolean_t saw_error = B_FALSE;

	while ((n = getline(&line, &linesz, pipes[0])) > 0) {
		if (line[n - 1] == '\n')
			line[n - 1] = '\0';

		linenum++;

		if (!save) {
			if (strstr(line, "dev --> Job output") == line) {
				save = B_TRUE;
				saw_error = B_FALSE;
				save_line(&buf, line, linenum);
			}
			continue;
		}

		if ((strstr(line, "*** Error") == line) &&
		    (strstr(line, "ignored") == NULL))
			saw_error = B_TRUE;

		if (strstr(line, "dev -->") == line ||
		    strstr(line, "==== Ended") == line) {
			if (saw_error)
				dump_lines(&buf, pipes[1]);

			save = saw_error = B_FALSE;
			flush_lines(&buf);
			continue;
		}

		save_line(&buf, line, linenum);
	}

	if (save && saw_error)
		dump_lines(&buf, pipes[1]);

	if (ferror(pipes[0]))
		err(EXIT_FAILURE, "%s", input);

	free(line);
	flush_lines(&buf);
	free(buf.lines);
	(void) fclose(pipes[0]);
	(void) fclose(pipes[1]);
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
dump_lines(buffer_t *b, FILE *out)
{
	(void) fprintf(out, "#### %s:%zu\n", input, b->linenum);
	for (size_t i = 0; i < b->n; i++) {
		if (fprintf(out, "%s\n", b->lines[i]) < 0)
			err(EXIT_FAILURE, "fprintf");
	}

	(void) fputc('\n', out);
	(void) fflush(out);
	if (ferror(out))
		err(EXIT_FAILURE, "fflush");
}

static const char *skip_whitespace(const char *);
static char **split_lines(const char *);
static void format_cmd(const char **, FILE *);
static void format_cc(const char **, FILE *);
static void format_lint(const char **, FILE *);
static boolean_t starts_with(const char *, const char *);
static boolean_t ends_with(const char *, const char *);

static void *
format_output(void *arg)
{
	FILE **pipes = arg;
	char *line = NULL;
	size_t linesz = 0;
	ssize_t n = 0;

	while ((n = getline(&line, &linesz, pipes[0])) > 0) {
		if (line[n - 1] == '\n')
			line[n - 1] = '\0';

		char **words = split_lines(line);

		if (words == NULL || words[0] == NULL) {
			fputc('\n', pipes[1]);
			free(words);
			continue;
		}

		if (ends_with(words[0], "/cw"))
			format_cc((const char **)words, pipes[1]);
		else if (words[1] != NULL && strcmp(words[0], "+") == 0 &&
		    (ends_with(words[1], "/gcc") ||
		    ends_with(words[1], "/cc"))) {
			(void) fprintf(pipes[1], "%s ", words[0]);
			format_cc((const char **)words + 1, pipes[1]);
		} else if (ends_with(words[0], "/lint")) {
			format_lint((const char **)words, pipes[1]);
		} else if (words[0][0] == '/') {
			format_cmd((const char **)words, pipes[1]);
		} else {
			(void) fprintf(pipes[1], "%s\n", line);
		}

		if (ferror(pipes[1]))
			err(EXIT_FAILURE, "");

		for (size_t i = 0; words[i] != NULL; i++)
			free(words[i]);
		free(words);
	}

	(void) fclose(pipes[0]);
	(void) fclose(pipes[1]);
	return (NULL);
}

static void
newline(FILE *out, size_t *pos)
{
	if (*pos > 0) {
		(void) fprintf(out, " \\\n");
		*pos = 0;
	}
}

static void
format_cmd(const char **words, FILE *out)
{
	size_t pos = 0;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if (i == 0) {
			if ((n = fprintf(out, "%s", words[0])) < 0)
				err(EXIT_FAILURE, "fprintf");
			pos += n;
			continue;
		}

		if ((len + indent < screen_width) &&
		    (pos + len + 2 > screen_width))
			newline(out, &pos);

		if (pos == 0) {
			if ((n = fprintf(out, "%*s", indent, "")) < 0)
				err(EXIT_FAILURE, "fprintf");
			pos += n;
		} else {
			(void) fputc(' ', out);
			pos++;
		}

		if ((n = fprintf(out, "%s", words[i])) < 0)
			err(EXIT_FAILURE, "fprintf");

		pos += n;
	}

	(void) fputc('\n', out);
}

static void
format_cc(const char **words, FILE *out)
{
	size_t width = 0;
	boolean_t own_line = B_FALSE;
	boolean_t fflag = B_FALSE;
	boolean_t Wflag = B_FALSE;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if (i == 0) {
			n = fprintf(out, "%s", words[0]);
			if (n < 0)
				err(EXIT_FAILURE, "fprintf");
			width += n;
			continue;
		}

		if (width + len + 2 > screen_width)
			newline(out, &width);

		/* put these on their own lines */
		if (strchr(words[i], '=') != NULL ||
		    starts_with(words[i], "-I") ||
		    starts_with(words[i], "-L") ||
		    starts_with(words[i], "-D")) {
			newline(out, &width);
			own_line = B_TRUE;
		}

		if (starts_with(words[i], "-f")) {
			if (!fflag) {
				newline(out, &width);
				fflag = B_TRUE;
			}
		} else {
			fflag = B_FALSE;
		}

		if (starts_with(words[i], "-W")) {
			if (!Wflag) {
				newline(out, &width);
				Wflag = B_TRUE;
			}
		} else {
			Wflag = B_FALSE;
		}

		if (width == 0) {
			if ((n = fprintf(out, "%*s", indent, "")) < 0)
				err(EXIT_FAILURE, "fprintf");
			width += n;
		} else {
			(void) fputc(' ', out);
			width++;
		}

		n = fprintf(out, "%s", words[i]);
		if (n < 0)
			err(EXIT_FAILURE, "fprintf");
		width += n;

		if (own_line) {
			newline(out, &width);
			own_line = B_FALSE;
		}
	}

	(void) fputc('\n', out);
}

static void
format_lint(const char **words, FILE *out)
{
	size_t width = 0;

	for (size_t i = 0; words[i] != NULL; i++) {
		size_t len = strlen(words[i]);
		int n = 0;

		if ((width > 0) && (width + len + 2 > screen_width)) {
			(void) fputc(' ', out);
			(void) fputc('\\', out);
			(void) fputc('\n', out);
			width = 0;
		}

		if (width == 0) {
			if (i > 0) {
				if ((n = fprintf(out, "%.*s", indent, "")) < 0)
					err(EXIT_FAILURE, "fprintf");
				width += n;
			}
		} else {
			(void) fputc(' ', out);
			width++;
		}

		n = fprintf(out, "%s", words[i]);
		if (n < 0)
			err(EXIT_FAILURE, "fprintf");
		width += n;
	}

	(void) fputc('\n', out);
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

static boolean_t
starts_with(const char *word, const char *str)
{
	if (strstr(word, str) == word)
		return (B_TRUE);
	return (B_FALSE);
}

static boolean_t
ends_with(const char *word, const char *str)
{
	size_t wordlen = strlen(word);
	size_t slen = strlen(str);

	if (wordlen < slen)
		return (B_FALSE);
	if (strcmp(word + wordlen - slen, str) == 0)
		return (B_TRUE);
	return (B_FALSE);
}

static const char *
skip_whitespace(const char *p)
{
	while (*p != '\0') {
		if (*p != ' ' && *p != '\t')
			return (p);
		p++;
	}

	return (p);
}

static size_t
get_screen_width(void)
{
	struct winsize win = { 0 };

	if (getenv("COLUMNS") != NULL) {
		size_t amt = 0;

		errno = 0;
		amt = strtoul(getenv("COLUMNS"), NULL, 10);
		if (errno == 0)
			return (amt);
	}

	/* Since this might be called in a pipeline, try each one */
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &win) != -1)
		return ((size_t)win.ws_col);
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) != -1)
		return ((size_t)win.ws_col);
	if (ioctl(STDERR_FILENO, TIOCGWINSZ, &win) != -1)
		return ((size_t)win.ws_col);

	/* the good old fallback standard */
	return (80);
}
