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

#ifndef _UTIL_H
#define	_UTIL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

boolean_t starts_with(const char *, const char *);
boolean_t ends_with(const char *, const char *);
const char *skip_whitespace(const char *);
char **split_lines(const char *);
void *zalloc(size_t);

#ifdef __cplusplus
}
#endif

#endif /* _UTIL_H */
