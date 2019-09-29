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
#if HAVE_ERR
# include <err.h>
#endif
#include <regex.h>
#include <stdlib.h>

#include "regexp.h"
#include "util.h"

struct Regexp {
	int exec;
	regex_t *regex;
	regmatch_t *match;
	size_t nmatch;
	const char *buf;
};

struct Regexp *
regexp_new(regex_t *regex)
{
	struct Regexp *regexp = xmalloc(sizeof(struct Regexp));
	regexp->regex = regex;
	regexp->nmatch = 8;
	regexp->match = reallocarray(NULL, regexp->nmatch, sizeof(regmatch_t));
	if (regexp->match == NULL) {
		warn("reallocarray");
		abort();
	}
	return regexp;
}

void
regexp_free(struct Regexp *regexp)
{
	if (regexp == NULL) {
		return;
	}
	free(regexp->match);
	free(regexp);
}

size_t
regexp_length(struct Regexp *regexp, size_t group)
{
	assert(regexp->exec > 0);

	if (group >= regexp->nmatch || regexp->match[group].rm_eo < 0 ||
	    regexp->match[group].rm_so < 0) {
		return 0;
	}
	return regexp->match[group].rm_eo - regexp->match[group].rm_so;
}

size_t
regexp_end(struct Regexp *regexp, size_t group)
{
	assert(regexp->exec > 0);

	if (group >= regexp->nmatch || regexp->match[group].rm_eo < 0) {
		return 0;
	}
	return regexp->match[group].rm_eo;
}

size_t
regexp_start(struct Regexp *regexp, size_t group)
{
	assert(regexp->exec > 0);

	if (group >= regexp->nmatch || regexp->match[group].rm_so < 0) {
		return 0;
	}
	return regexp->match[group].rm_so;
}

char *
regexp_substr(struct Regexp *regexp, size_t group)
{
	assert(regexp->buf != NULL);

	if (group >= regexp->nmatch) {
		return NULL;
	}
	return str_substr_dup(regexp->buf, regexp_start(regexp, group), regexp_end(regexp, group));
}

int
regexp_exec(struct Regexp *regexp, const char *buf)
{
	regexp->buf = buf;
	regexp->exec++;
	return regexec(regexp->regex, regexp->buf, regexp->nmatch, regexp->match, 0);
}
