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

#include <string.h>
#include "util.h"

boolean_t
starts_with(const char *word, const char *str)
{
	if (strstr(word, str) == word)
		return (B_TRUE);
	return (B_FALSE);
}

boolean_t
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

const char *
skip_whitespace(const char *p)
{
	while (*p != '\0') {
		if (*p != ' ' && *p != '\t')
			return (p);
		p++;
	}

	return (p);
}
