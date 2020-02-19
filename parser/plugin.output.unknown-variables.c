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

#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "parser/plugin.h"
#include "rules.h"
#include "set.h"
#include "token.h"
#include "util.h"
#include "variable.h"

static struct Array *
output_unknown_variables(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	struct Array **unknowns = (struct Array **)userdata;
	if (!(parser_settings(parser).behavior & PARSER_OUTPUT_RAWLINES)) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		xasprintf(error_msg, "needs PARSER_OUTPUT_RAWLINES");
		return NULL;
	}

	if (unknowns) {
		*unknowns = NULL;
	}
	struct Set *vars = set_new(str_compare, NULL, NULL);
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (token_type(t) != VARIABLE_START) {
			continue;
		}
		char *name = variable_name(token_variable(t));
		if (variable_order_block(parser, name) == BLOCK_UNKNOWN &&
		    !set_contains(vars, name)) {
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
			set_add(vars, name);
		}
	}
	if (unknowns) {
		*unknowns = set_toarray(vars);
	}
	set_free(vars);

	return NULL;
}

PLUGIN("output.unknown-variables", output_unknown_variables);
