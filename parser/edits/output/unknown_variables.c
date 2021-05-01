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

#include <libias/array.h>
#include <libias/set.h>
#include <libias/util.h>

#include "parser.h"
#include "parser/edits.h"
#include "rules.h"
#include "token.h"
#include "variable.h"

static void
check_opthelper(struct Parser *parser, struct ParserEditOutput *param, struct Set *vars, const char *option, int optuse, int optoff)
{
	const char *suffix;
	if (optoff) {
		suffix = "_OFF";
	} else {
		suffix = "";
	}
	char *var;
	if (optuse) {
		var = str_printf("%s_USE%s", option, suffix);
	} else {
		var = str_printf("%s_VARS%s", option, suffix);
	}
	struct Array *optvars;
	if (!parser_lookup_variable_all(parser, var, &optvars, NULL)) {
		free(var);
		return;
	}

	ARRAY_FOREACH(optvars, const char *, token) {
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
			name = str_printf("USE_%s", tmp);
			free(tmp);
		}
		if (variable_order_block(parser, name, NULL) == BLOCK_UNKNOWN &&
		    !set_contains(vars, name)) {
			set_add(vars, name);
			if (param->callback) {
				param->callback(name, name, var, param->callbackuserdata);
			}
		} else {
			free(name);
		}
	}
	free(var);
	array_free(optvars);
}

PARSER_EDIT(output_unknown_variables)
{
	struct ParserEditOutput *param = userdata;
	if (param == NULL) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		*error_msg = str_printf("missing parameter");
		return NULL;
	}

	param->found = 0;

	struct Set *vars = set_new(str_compare, NULL, free);
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		if (token_type(t) != VARIABLE_START) {
			continue;
		}
		char *name = variable_name(token_variable(t));
		if (variable_order_block(parser, name, NULL) == BLOCK_UNKNOWN &&
		    !set_contains(vars, name) &&
		    (param->keyfilter == NULL || param->keyfilter(parser, name, param->keyuserdata))) {
			set_add(vars, xstrdup(name));
			param->found = 1;
			if (param->callback) {
				param->callback(name, name, NULL, param->callbackuserdata);
			}
		}
	}
	struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
	SET_FOREACH (options, const char *, option) {
		check_opthelper(parser, param, vars, option, 1, 0);
		check_opthelper(parser, param, vars, option, 0, 0);
		check_opthelper(parser, param, vars, option, 1, 1);
		check_opthelper(parser, param, vars, option, 0, 1);
	}
	set_free(vars);

	return NULL;
}

