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
#include <err.h>
#include <stdlib.h>
#include "util.h"

#define	CHUNK_SZ	(32)

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

void *
zalloc(size_t amt)
{
	void *p = calloc(1, amt);

	if (p == NULL)
		err(EXIT_FAILURE, "calloc");
	return (p);
}

/* split lines, but try to respect shell-like quoting */
char **
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
