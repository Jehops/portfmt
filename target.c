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

#include "util.h"
#include "target.h"

struct Target {
	char *name;
	char *deps;
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

struct Target *
target_new(char *buf)
{
	char *after_target = NULL;
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
		} else if (c == ':' || c == '!') {
			after_target = buf + i;
			break;
		}
	}
	if (after_target == NULL || after_target < buf) {
		return NULL;
	}

	struct Target *target = xmalloc(sizeof(struct Target));

	char *tmp = xmalloc(strlen(buf) + 1);
	strncpy(tmp, buf, after_target - buf);
	target->name = str_trim(tmp);
	free(tmp);

	target->deps = xstrdup(after_target + 1);

	return target;
}

struct Target *
target_clone(struct Target *target)
{
	struct Target *newtarget = xmalloc(sizeof(struct Target));
	newtarget->name = xstrdup(target->name);
	newtarget->deps = xstrdup(target->deps);
	return newtarget;
}

void
target_free(struct Target *target)
{
	if (target == NULL) {
		return;
	}
	free(target->name);
	free(target->deps);
	free(target);
}

char *
target_name(struct Target *target)
{
	assert(target != NULL);
	return target->name;
}

