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

#if HAVE_ERR
# include <err.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

static struct Variable *has_variable(struct Array *, const char *);
static char *lookup_variable(struct Array *, const char *);
static struct Array *edit_set_variable(struct Parser *, struct Array *, const void *);

struct EditSetVariableHelperParams {
	struct Parser *parser;
	struct Array *tokens;
	int always;
};

struct EditSetVariableParams {
	const char *name;
	const char *value;
	const char *after;
};

static struct Array *
edit_set_variable_helper(struct Parser *subparser, struct Array *subtokens, const void *userdata)
{
	struct EditSetVariableHelperParams *params = (struct EditSetVariableHelperParams *)userdata;
	for (size_t j = 0; j < array_len(subtokens); j++) {
		struct Token *vt = array_get(subtokens, j);
		if (params->always || token_type(vt) == VARIABLE_TOKEN) {
			struct Token *et = token_clone(vt, NULL);
			parser_mark_edited(params->parser, et);
			array_append(params->tokens, et);
		}
	}
	return subtokens;
}

static struct Array *
edit_set_variable(struct Parser *parser, struct Array *ptokens, const void *userdata)
{
	struct EditSetVariableParams *params = (struct EditSetVariableParams *)userdata;

	char *tmp;
	struct Variable *var = NULL;
	if ((var = has_variable(ptokens, params->name)) != NULL) {
		char *varmod = variable_tostring(var);
		xasprintf(&tmp, "%s\t\\\n%s", varmod, params->value);
		free(varmod);
	} else {
		xasprintf(&tmp, "%s=\t\\\n%s", params->name, params->value);
	}
	struct Parser *subparser = parser_parse_string(parser, tmp);
	free(tmp);

	struct Array *tokens = array_new(sizeof(char *));
	if (has_variable(ptokens, params->name)) {
		int set = 0;
		for (size_t i = 0; i < array_len(ptokens); i++) {
			struct Token *t = array_get(ptokens, i);
			switch (token_type(t)) {
			case VARIABLE_TOKEN:
				if (variable_cmp(token_variable(t), var) == 0) {
					if (is_comment(t)) {
						array_append(tokens, t);
					} else if (!set) {
						parser_mark_for_gc(parser, t);
						struct EditSetVariableHelperParams params = { parser, tokens, 0 };
						parser_edit(subparser, edit_set_variable_helper, &params);
						set = 1;
					}
				} else {
					array_append(tokens, t);
				}
				break;
			default:
				array_append(tokens, t);
				break;
			}
		}
	} else if (params->after != NULL) {
		int set = 0;
		for (size_t i = 0; i < array_len(ptokens); i++) {
			struct Token *t = array_get(ptokens, i);
			array_append(tokens, t);
			if (!set && token_type(t) == VARIABLE_END &&
			    strcmp(variable_name(token_variable(t)), params->after) == 0) {
				struct EditSetVariableHelperParams params = { parser, tokens, 1 };
				parser_edit(subparser, edit_set_variable_helper, &params);
				set = 1;
			}
		}
	} else {
		errx(1, "cannot append: %s not currently set", params->name);
		array_free(tokens);
		return NULL;
	}

	parser_free(subparser);
	return tokens;
}

static char *
lookup_variable(struct Array *ptokens, const char *name)
{
	struct Array *tokens = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			array_truncate(tokens);
			break;
		case VARIABLE_TOKEN:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				if (!is_comment(t)) {
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

	array_free(tokens);
	return NULL;

	size_t sz;
found:
	sz = array_len(tokens) + 1;
	for (size_t i = 0; i < array_len(tokens); i++) {
		char *s = array_get(tokens, i);
		sz += strlen(s);
	}

	char *buf = xmalloc(sz);
	for (size_t i = 0; i < array_len(tokens); i++) {
		char *s = array_get(tokens, i);
		xstrlcat(buf, s, sz);
		if (i != array_len(tokens) - 1) {
			xstrlcat(buf, " ", sz);
		}
	}

	array_free(tokens);
	return buf;
}

static struct Variable *
has_variable(struct Array *tokens, const char *var)
{
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (token_type(t) == VARIABLE_START &&
		    strcmp(variable_name(token_variable(t)), var) == 0) {
			return token_variable(t);
		}
	}
	return NULL;
}

struct Array *
edit_bump_revision(struct Parser *parser, struct Array *ptokens, const void *userdata)
{
	const char *after = "PORTVERSION";
	if (has_variable(ptokens, "DISTVERSION")) {
		after = "DISTVERSION";
	}
	if (has_variable(ptokens, "DISTVERSIONSUFFIX")) {
		after = "DISTVERSIONSUFFIX";
	}
	if (has_variable(ptokens, "PORTVERSION") &&
	    !has_variable(ptokens, "DISTVERSION")) {
		if (has_variable(ptokens, "DISTVERSIONPREFIX")) {
			after = "DISTVERSIONPREFIX";
		}
		if (has_variable(ptokens, "DISTVERSIONSUFFIX")) {
			after = "DISTVERSIONSUFFIX";
		}
	}

	char *current_revision = lookup_variable(ptokens, "PORTREVISION");
	if (current_revision) {
		const char *errstr = NULL;
		int revision = strtonum(current_revision, 0, INT_MAX, &errstr);
		if (errstr == NULL) {
			revision++;
		} else {
			errx(1, "unable to parse PORTREVISION: %s: %s", current_revision, errstr);
		}
		char *rev;
		xasprintf(&rev, "%d", revision);
		struct EditSetVariableParams params = { "PORTREVISION", rev, after };
		parser_edit(parser, edit_set_variable, &params);
		free(rev);
	} else {
		struct EditSetVariableParams params = { "PORTREVISION", "1", after };
		parser_edit(parser, edit_set_variable, &params);
	}

	return NULL;
}
