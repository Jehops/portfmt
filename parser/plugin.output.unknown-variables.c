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

#include <ctype.h>
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

static void
check_opthelper(struct Parser *parser, struct ParserPluginOutput *param, struct Set *vars, const char *option, int optuse)
{
	char *var;
	if (optuse) {
		xasprintf(&var, "%s_USE", option);
	} else {
		xasprintf(&var, "%s_VARS", option);
	}
	struct Array *optvars;
	if (!parser_lookup_variable_all(parser, var, &optvars, NULL)) {
		free(var);
		return;
	}

	for (size_t i = 0; i < array_len(optvars); i++) {
		const char *token = array_get(optvars, i);
		char *suffix = strchr(token, '+');
		if (!suffix) {
			suffix = strchr(token, '=');
			if (!suffix) {
				continue;
			}
		} else if (*(suffix + 1) != '=') {
			continue;
		}
		char *name = xstrndup(token, suffix - token);
		for (char *p = name; *p != 0; p++) {
			*p = toupper(*p);
		}
		if (optuse) {
			char *tmp = name;
			xasprintf(&name, "USE_%s", tmp);
			free(tmp);
		}
		if (variable_order_block(parser, name) == BLOCK_UNKNOWN &&
		    !set_contains(vars, name)) {
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
			set_add(vars, name);
			if (param->return_values) {
				array_append(param->keys, xstrdup(name));
				array_append(param->values, xstrdup(name));
			}
		} else {
			free(name);
		}
	}
	free(var);
	array_free(optvars);
}

static struct Array *
output_unknown_variables(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	struct ParserPluginOutput *param = (struct ParserPluginOutput *)userdata;
	if (!(parser_settings(parser).behavior & PARSER_OUTPUT_RAWLINES)) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		xasprintf(error_msg, "needs PARSER_OUTPUT_RAWLINES");
		return NULL;
	}

	if (param->return_values) {
		param->keys = array_new();
		param->values = array_new();
	}
	struct Set *vars = set_new(str_compare, NULL, free);
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (token_type(t) != VARIABLE_START) {
			continue;
		}
		char *name = variable_name(token_variable(t));
		if (variable_order_block(parser, name) == BLOCK_UNKNOWN &&
		    !set_contains(vars, name) &&
		    param->filter(parser, name, name, param->userdata)) {
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
			set_add(vars, xstrdup(name));
			param->found = 1;
			if (param->return_values) {
				array_append(param->keys, xstrdup(name));
				array_append(param->values, xstrdup(name));
			}
		}
	}
	struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
	SET_FOREACH (options, const char *, option) {
		check_opthelper(parser, param, vars, option, 1);
		check_opthelper(parser, param, vars, option, 0);
	}
	set_free(vars);

	return NULL;
}

PLUGIN("output.unknown-variables", output_unknown_variables);
