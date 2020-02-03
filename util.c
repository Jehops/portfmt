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

#include <sys/param.h>
#include <assert.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "util.h"

char *
read_symlink(int dir, const char *path)
{
	char buf[PATH_MAX];
	ssize_t len = readlinkat(dir, path, buf, sizeof(buf));
	if (len != -1) {
		return xstrndup(buf, len);
	}
	return NULL;
}

int
update_symlink(int dir, const char *path1, const char *path2, char **prev)
{
	if (prev != NULL) {
		*prev = NULL;
	}
	while (symlinkat(path1, dir, path2) == -1) {
		if (errno == EEXIST) {
			if (prev != NULL) {
				*prev = read_symlink(dir, path2);
			}
			if (unlinkat(dir, path2, 0) == -1) {
				if (prev != NULL) {
					free(*prev);
					*prev = NULL;
				}
				return 0;
			}
		} else {
			if (prev != NULL) {
				free(*prev);
				*prev = NULL;
			}
			return 0;
		}
	}

	return 1;
}

char *
repeat(char c, size_t n)
{
	char *buf = xmalloc(n + 1);
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

char *
str_common_prefix(const char *a, const char *b)
{
	const char *ap = a;
	const char *bp = b;
	size_t i;
	for (i = 0; *ap != 0 && *bp != 0 && *ap++ == *bp++; i++);

	if (i > 0) {
		return xstrndup(a, i);
	} else {
		return NULL;
	}
}

int
str_compare(const void *ap, const void *bp, void *userdata)
{
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;
	return strcmp(a, b);
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

char *
str_join(struct Array *array, const char *sep)
{
	size_t sz = array_len(array) + 1;
	for (size_t i = 0; i < array_len(array); i++) {
		char *s = array_get(array, i);
		sz += strlen(s);
	}

	char *buf = xmalloc(sz);
	for (size_t i = 0; i < array_len(array); i++) {
		char *s = array_get(array, i);
		xstrlcat(buf, s, sz);
		if (i != array_len(array) - 1) {
			xstrlcat(buf, sep, sz);
		}
	}

	return buf;
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
	return str_trim(sp);
}

char *
str_substr_dup(const char *s, size_t start, size_t end)
{
	assert (start <= end);
	end = MIN(strlen(s), end);
	return xstrndup(s + start, end - start);
}

char *
str_trim(const char *s)
{
	size_t len = strlen(s);
	while (len > 0 && isspace(s[len - 1])) {
		len--;
	}
	return xstrndup(s, len);
}

#if HAVE_GNU_QSORT_R

void
sort(void *base, size_t nmemb, size_t size, CompareFn compar, void *userdata)
{
	qsort_r(base, nmemb, size, compar, userdata);
}

#else

// c.f. https://reviews.freebsd.org/D17083

struct SortWrapper {
	CompareFn compar;
	void *userdata;
};

static int
qsort_r_compare_wrapper(void *userdata, const void *a, const void *b)
{
	const struct SortWrapper *wrapper = userdata;
	return wrapper->compar(a, b, wrapper->userdata);
}

void
sort(void *base, size_t nmemb, size_t size, CompareFn compar, void *userdata)
{
	struct SortWrapper wrapper = { compar, userdata };
	qsort_r(base, nmemb, size, &wrapper, qsort_r_compare_wrapper);
}

#endif

int
xasprintf(char **ret, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int retval = vasprintf(ret, format, ap);
	va_end(ap);

	if (retval < 0) {
		warn("vasprintf");
		abort();
	}

	return retval;
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

char *
xstrndup(const char *s, size_t len)
{
	char *retval = strndup(s, len);
	if (retval == NULL) {
		warn("strndup");
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
