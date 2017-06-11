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
#include "filter.h"
#include "format.h"

static size_t screen_width;

static size_t get_screen_width(void);

int
main(int argc, const char **argv)
{
	struct filter_opts filter_opts = { 0 };
	struct format_opts format_opts = { 0 };
	int fds[2] = { -1, -1 };
	pthread_t tids[2] = { 0 };

	if (argc > 1) {
		if ((filter_opts.in = fopen(argv[1], "rF")) == NULL)
			err(EXIT_FAILURE, argv[1]);
		filter_opts.filename = argv[1];
	} else {
		filter_opts.in = stdin;
		filter_opts.filename = "(stdin)";
	}

	if (pipe(fds) < 0)
		err(EXIT_FAILURE, "pipe");

	if ((filter_opts.out = fdopen(fds[0], "wF")) == NULL)
		err(EXIT_FAILURE, "fdopen");

	if ((format_opts.in  = fdopen(fds[1], "rF")) == NULL)
		err(EXIT_FAILURE, "fdopen");

	screen_width = get_screen_width();

	format_opts.out = stdout;
	format_opts.indent = 4;
	format_opts.cols = screen_width;

	pthread_create(&tids[0], NULL, filter_errors, &filter_opts);
	pthread_create(&tids[1], NULL, format_output, &format_opts);
	pthread_join(tids[0], NULL);
	pthread_join(tids[1], NULL);

	return (0);
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
