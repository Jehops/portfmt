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
#include "token.h"
#include "util.h"
#include "variable.h"

static struct Array *
output_variable_value(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	if (!(parser_settings(parser).behavior & PARSER_OUTPUT_RAWLINES)) {
		return NULL;
	}
	if (userdata == NULL) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		return NULL;
	}

	struct ParserPluginOutput *param = (struct ParserPluginOutput *)userdata;
	if (param->return_values) {
		param->keys = array_new();
		param->values= array_new();
	}
	int found = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);

		switch (token_type(t)) {
		case VARIABLE_START:
			if ((param->keyfilter == NULL || param->keyfilter(parser, variable_name(token_variable(t)), param->keyuserdata))) {
				found = 1;
			}
			break;
		case VARIABLE_TOKEN:
			if (found && token_data(t) &&
			    (param->keyfilter == NULL || param->keyfilter(parser, variable_name(token_variable(t)), param->keyuserdata)) &&
			    (param->filter == NULL || param->filter(parser, token_data(t), param->userdata))) {
				if (param->return_values) {
					array_append(param->keys, xstrdup(variable_name(token_variable(t))));
					array_append(param->values, xstrdup(token_data(t)));
				}
				parser_enqueue_output(parser, token_data(t));
				parser_enqueue_output(parser, "\n");
			}
			break;
		default:
			break;
		}
	}

	if (!found) {
		*error = PARSER_ERROR_NOT_FOUND;
	}

	return NULL;
}

PLUGIN("output.variable-value", output_variable_value);
