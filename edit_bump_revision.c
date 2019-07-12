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
#if HAVE_ERR
# include <err.h>
#endif
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

static char *lookup_variable(struct Array *, const char *, char **comment);

static char *
lookup_variable(struct Array *ptokens, const char *name, char **comment)
{
	struct Array *tokens = array_new(sizeof(char *));
	struct Array *comments = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			array_truncate(tokens);
			break;
		case VARIABLE_TOKEN:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				if (is_comment(t)) {
					array_append(comments, token_data(t));
				} else {
					array_append(tokens, token_data(t));
				}
			}
			break;
		case VARIABLE_END:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				goto found;
			}
			break;
		default:
			break;
		}
	}

	array_free(comments);
	array_free(tokens);
	return NULL;

found:
	*comment = array_join(comments, " ");
	char *buf = array_join(tokens, " ");
	array_free(comments);
	array_free(tokens);
	return buf;
}

struct Array *
edit_bump_revision(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	char *revision;
	char *comment = NULL;
	char *current_revision = lookup_variable(ptokens, "PORTREVISION", &comment);
	if (current_revision) {
		const char *errstr = NULL;
		int rev = strtonum(current_revision, 0, INT_MAX, &errstr);
		if (errstr == NULL) {
			rev++;
		} else {
			*error = PARSER_ERROR_EXPECTED_INT;
			if (comment) {
				free(comment);
			}
			return NULL;
		}
		xasprintf(&revision, "PORTREVISION=%d %s", rev, comment);
	} else {
		revision = xstrdup("PORTREVISION=1");
	}

	struct ParserSettings settings = parser_settings(parser);
	struct Parser *subparser = parser_new(&settings);
	*error = parser_read_from_buffer(subparser, revision, strlen(revision));
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}
	*error = parser_read_finish(subparser);
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}
	*error = parser_edit(parser, edit_merge, subparser);
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}

cleanup:
	if (comment) {
		free(comment);
	}
	free(revision);
	parser_free(subparser);

	return NULL;
}
