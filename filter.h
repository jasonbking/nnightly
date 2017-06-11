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

#ifndef _FILTER_H
#define	_FILTER_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct filter_opts {
	FILE *in;
	FILE *out;
	const char *filename;
};

void *filter_errors(void *);

#ifdef __cplusplus
}
#endif

#endif /* _FILTER_H */
