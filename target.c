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
#include <stdlib.h>
#include <string.h>

#include <libias/array.h>
#include <libias/mempool.h>
#include <libias/util.h>

#include "target.h"

struct Target {
	struct Mempool *pool;
	struct Array *names;
	struct Array *deps;
	char *comment;
};

static size_t
consume_token(const char *line, size_t pos, char startchar, char endchar)
{
	int counter = 0;
	int escape = 0;
	size_t i = pos;
	for (; i < strlen(line); i++) {
		char c = line[i];
		if (escape) {
			escape = 0;
			continue;
		}
		if (startchar == endchar) {
			if (c == startchar) {
				if (counter == 1) {
					return i;
				} else {
					counter++;
				}
			} else if (c == '\\') {
				escape = 1;
			}
		} else {
			if (c == startchar) {
				counter++;
			} else if (c == endchar && counter == 1) {
				return i;
			} else if (c == endchar) {
				counter--;
			} else if (c == '\\') {
				escape = 1;
			}
		}
	}
	return 0;
}

static void
add_name(struct Mempool *pool, struct Array *names, const char *buf, size_t start, size_t i)
{
	char *tmp = xstrndup(buf + start, i - start);
	char *name = str_trim(tmp);
	free(tmp);
	if (*name) {
		array_append(names, mempool_add(pool, name, free));
	} else {
		free(name);
	}
}

static const char *
consume_names(struct Mempool *pool, const char *buf, struct Array *names, int deps)
{
	const char *after_target = NULL;
	size_t start = 0;
	for (size_t i = 0; i < strlen(buf); i++) {
		char c = buf[i];
		if (c == '$') {
			size_t pos = consume_token(buf, i, '{', '}');
			if (pos == 0) {
				i++;
				if (i >= strlen(buf) || !isalnum(buf[i])) {
					return NULL;
				}
			} else {
				i = pos;
			}
		} else if (!deps && (c == ':' || c == '!')) {
			if (c == ':' && buf[i + 1] == ':') {
				// consume extra : after target name (for example, pre-everthing::)
				i++;
			}
			if (i > start) {
				add_name(pool, names, buf, start, i);
			}
			after_target = buf + i + 1;
			break;
		} else if (c == ' ') {
			if (i > start) {
				add_name(pool, names, buf, start, i);
			}
			start = i + 1;
		} else if (c == '#') {
			after_target = buf + i + 1;
			break;
		}
	}

	if (deps) {
		if (buf[start] && buf[start] != '#') {
			char *name = str_trim(buf + start);
			if (*name) {
				array_append(names, mempool_add(pool, str_trim(buf + start), free));
			} else {
				free(name);
			}
		}
	}

	if (after_target == NULL || after_target < buf) {
		return NULL;
	} else {
		for (; *after_target && isspace(*after_target); ++after_target);
		return after_target;
	}
}

struct Target *
target_new(char *buf)
{
	struct Mempool *pool = mempool_new();
	struct Array *names = mempool_add(pool, array_new(), array_free);
	struct Array *deps = mempool_add(pool, array_new(), array_free);
	const char *after_target = consume_names(pool, buf, names, 0);
	if (after_target == NULL) {
		mempool_free(pool);
		return NULL;
	}
	const char *comment = consume_names(pool, after_target, deps, 1);

	struct Target *target = xmalloc(sizeof(struct Target));
	target->pool = pool;
	target->deps = deps;
	target->names = names;
	if (comment) {
		target->comment = mempool_add(pool, xstrdup(comment), free);
	}
	return target;
}

struct Target *
target_clone(struct Target *target)
{
	struct Target *newtarget = xmalloc(sizeof(struct Target));
	newtarget->pool = mempool_new();
	newtarget->deps = array_new();
	ARRAY_FOREACH(target->deps, char *, dep) {
		array_append(newtarget->deps, mempool_add(newtarget->pool, xstrdup(dep), free));
	}
	newtarget->names = array_new();
	ARRAY_FOREACH(target->names, char *, name) {
		array_append(newtarget->names, mempool_add(newtarget->pool, xstrdup(name), free));
	}
	if (target->comment) {
		newtarget->comment = mempool_add(newtarget->pool, xstrdup(target->comment), free);
	}
	return newtarget;
}

void
target_free(struct Target *target)
{
	if (target == NULL) {
		return;
	}
	mempool_free(target->pool);
	free(target);
}

const char *
target_comment(struct Target *target)
{
	return target->comment;
}

struct Array *
target_dependencies(struct Target *target)
{
	return target->deps;
}

struct Array *
target_names(struct Target *target)
{
	return target->names;
}

