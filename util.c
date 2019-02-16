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
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
#include <string.h>
#include <sys/param.h>

#include "util.h"

int
sbuf_cmp(struct sbuf *a, struct sbuf *b)
{
	assert(a && sbuf_done(a));
	assert(b && sbuf_done(b));
	return strcmp(sbuf_data(a), sbuf_data(b));
}

int
sbuf_strcmp(struct sbuf *a, const char *b)
{
	assert(a && sbuf_done(a));
	assert(b);
	return strcmp(sbuf_data(a), b);
}

int
sbuf_endswith(struct sbuf *s, const char *end)
{
	assert(sbuf_done(s));
	ssize_t len = strlen(end);
	if (sbuf_len(s) < len) {
		return 0;
	}
	return strncmp(sbuf_data(s) + sbuf_len(s) - len, end, len) == 0;
}

struct sbuf *
sbuf_dup(struct sbuf *s) {
	if (s == NULL) {
		return sbuf_dupstr(NULL);
	} else {
		assert(sbuf_done(s));
		return sbuf_dupstr(sbuf_data(s));
	}
}

struct sbuf *
sbuf_dupstr(const char *s) {
	struct sbuf *r = sbuf_new_auto();
	if (r == NULL) {
		errx(1, "sbuf_new_auto");
	}
	if (s != NULL) {
		sbuf_cpy(r, s);
	}
	return r;
}

struct sbuf *
sbuf_strip_dup(struct sbuf *s) {
	assert(sbuf_done(s));
	struct sbuf *r = sbuf_dupstr(NULL);
	char *sp = sbuf_data(s);
	for (; *sp && isspace(*sp); ++sp);
	sbuf_cpy(r, sp);
	sbuf_trim(r);
	return r;
}

struct sbuf *
sbuf_substr_dup(struct sbuf *s, size_t start, size_t end) {
	assert(sbuf_done(s));
	assert (start <= end);
	assert (sbuf_len(s) >= 0);
	end = MIN((size_t)sbuf_len(s), end);
	struct sbuf *buf = sbuf_dupstr(NULL);
	sbuf_bcat(buf, sbuf_data(s) + start, end - start);
	return buf;
}

void
sbuf_finishx(struct sbuf *s)
{
	if (sbuf_finish(s) != 0) {
		errx(1, "sbuf_finish");
	}
}
