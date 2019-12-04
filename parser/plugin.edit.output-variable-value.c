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
#include "regexp.h"
#include "token.h"
#include "variable.h"

static struct Array *
edit_output_variable_value(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	if (!(parser_settings(parser).behavior & PARSER_OUTPUT_RAWLINES)) {
		return NULL;
	}

	regex_t re;
	if (regcomp(&re, userdata, REG_EXTENDED) != 0) {
		*error = PARSER_ERROR_INVALID_REGEXP;
		return NULL;
	}

	struct Regexp *regexp = regexp_new(&re);
	int found = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);

		switch (token_type(t)) {
		case VARIABLE_START:
			if (regexp_exec(regexp, variable_name(token_variable(t))) == 0) {
				found = 1;
			}
			break;
		case VARIABLE_TOKEN:
			if (found && token_data(t) &&
			    regexp_exec(regexp, variable_name(token_variable(t))) == 0) {
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

	regexp_free(regexp);

	return NULL;
}

PLUGIN("edit.output-variable-value", edit_output_variable_value);
