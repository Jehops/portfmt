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

#include <stdlib.h>
#include <stdio.h>

#include "array.h"
#include "parser.h"
#include "parser/plugin.h"
#include "token.h"
#include "util.h"
#include "variable.h"

static struct Array *
refactor_collapse_adjacent_variables(struct Parser *parser, struct Array *ptokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	if (userdata != NULL) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		return NULL;
	}

	struct Array *tokens = array_new();
	struct Variable *last_var = NULL;
	struct Token *last_end = NULL;
	struct Token *last_token = NULL;
	struct Array *ignored_tokens = array_new();
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			if (last_var != NULL &&
			    variable_cmp(token_variable(t), last_var) == 0 &&
			    variable_modifier(last_var) != MODIFIER_EXPAND &&
			    variable_modifier(token_variable(t)) != MODIFIER_EXPAND) {
				if (last_end) {
					array_append(ignored_tokens, t);
					array_append(ignored_tokens, last_end);
					last_end = NULL;
				}
			}
			break;
		case VARIABLE_TOKEN:
			last_token = t;
			break;
		case VARIABLE_END:
			if (!last_token || !str_startswith(token_data(last_token), "#")) {
				last_end = t;
			}
			last_token = NULL;
			last_var = token_variable(t);
			break;
		default:
			last_var = NULL;
			break;
		}
	}

	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		if (array_find(ignored_tokens, t, NULL, NULL) == -1) {
			array_append(tokens, t);
		} else {
			parser_mark_for_gc(parser, t);
		}
	}

	array_free(ignored_tokens);
	return tokens;
}

PLUGIN("refactor.collapse-adjacent-variables", refactor_collapse_adjacent_variables);
