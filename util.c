/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Tobias Kortkamp <tobik@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "util.h"

char *
repeat(char c, size_t n)
{
	static char buf[128];
	assert (n < sizeof(buf));
	if (n > 0) {
		for (size_t i = 0; i < n; i++) {
			buf[i] = c;
		}
		buf[n] = '\0';
	} else {
		buf[0] = '\0';
	}
	return buf;
}

int
str_endswith(const char *s, const char *end)
{
	size_t len = strlen(end);
	if (strlen(s) < len) {
		return 0;
	}
	return strncmp(s + strlen(s) - len, end, len) == 0;
}

int
str_startswith(const char *s, const char *start)
{
	size_t len = strlen(start);
	if (strlen(s) < len) {
		return 0;
	}
	return strncmp(s, start, len) == 0;
}

char *
str_strip_dup(const char *s)
{
	const char *sp = s;
	for (; *sp && isspace(*sp); ++sp);
	return str_trim(xstrdup(sp));
}

char *
str_substr_dup(const char *s, size_t start, size_t end)
{
	assert (start <= end);
	end = MIN(strlen(s), end);
	char *buf = strndup(s + start, end - start);
	if (buf == NULL) {
		warn("strndup");
		abort();
	}
	return buf;
}

char *
str_trim(char *s)
{
	size_t len = strlen(s);
	while (len > 0 && isspace(s[len - 1])) {
		len--;
	}
	s[len] = 0;
	return s;
}

void *
xmalloc(size_t size)
{
	void *x = malloc(size);
	if (x == NULL) {
		warn("malloc");
		abort();
	}
	memset(x, 0, size);
	return x;
}

char *
xstrdup(const char *s)
{
	char *retval = strdup(s);
	if (retval == NULL) {
		warn("strdup");
		abort();
	}
	return retval;
}

size_t
xstrlcat(char *dst, const char *src, size_t dstsize)
{
	size_t len = strlcat(dst, src, dstsize);
	if (len >= dstsize) {
		warnx("strlcat: truncated string: %zu/%zu", len, dstsize);
		abort();
	}
	return len;
}

size_t
xstrlcpy(char *dst, const char *src, size_t dstsize)
{
	size_t len = strlcpy(dst, src, dstsize);
	if (len >= dstsize) {
		warnx("strlcpy: truncated string: %zu/%zu", len, dstsize);
		abort();
	}
	return len;
}

