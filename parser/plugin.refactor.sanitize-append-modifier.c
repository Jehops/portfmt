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

#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "parser/plugin.h"
#include "rules.h"
#include "set.h"
#include "token.h"
#include "variable.h"

static struct Array *
refactor_sanitize_append_modifier(struct Parser *parser, struct Array *ptokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	if (userdata != NULL) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		return NULL;
	}

	/* Sanitize += before bsd.options.mk */
	struct Set *seen = set_new(variable_compare, NULL, NULL);
	struct Array *tokens = array_new();
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
		case VARIABLE_TOKEN:
			array_append(tokens, t);
			break;
		case VARIABLE_END: {
			array_append(tokens, t);
			if (set_contains(seen, token_variable(t))) {
				array_truncate(tokens);
				continue;
			} else {
				set_add(seen, token_variable(t));
			}
			for (size_t j = 0; j < array_len(tokens); j++) {
				struct Token *o = array_get(tokens, j);
				if (strcmp(variable_name(token_variable(o)), "CXXFLAGS") != 0 &&
				    strcmp(variable_name(token_variable(o)), "CFLAGS") != 0 &&
				    strcmp(variable_name(token_variable(o)), "LDFLAGS") != 0 &&
				    strcmp(variable_name(token_variable(o)), "RUSTFLAGS") != 0 &&
				    variable_modifier(token_variable(o)) == MODIFIER_APPEND) {
					variable_set_modifier(token_variable(o), MODIFIER_ASSIGN);
					parser_mark_edited(parser, o);
				}
			}
			array_truncate(tokens);
			break;
		} case CONDITIONAL_TOKEN:
			if (is_include_bsd_port_mk(t)) {
				goto end;
			}
		default:
			break;
		}
	}
end:
	set_free(seen);
	array_free(tokens);

	return ptokens;
}

PLUGIN("refactor.sanitize-append-modifier", refactor_sanitize_append_modifier);
