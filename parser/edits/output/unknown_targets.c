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

#include <libias/array.h>
#include <libias/set.h>
#include <libias/util.h>

#include "parser.h"
#include "parser/edits.h"
#include "rules.h"
#include "target.h"
#include "token.h"

struct Array *
output_unknown_targets(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	struct ParserEditOutput *param = (struct ParserEditOutput *)userdata;
	if (!(parser_settings(parser).behavior & PARSER_OUTPUT_RAWLINES)) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		xasprintf(error_msg, "needs PARSER_OUTPUT_RAWLINES");
		return NULL;
	}

	if (param->return_values) {
		param->keys = array_new();
		param->values = array_new();
	}
	struct Set *targets = set_new(str_compare, NULL, NULL);
	ARRAY_FOREACH(tokens, struct Token *, t) {
		if (token_type(t) != TARGET_START) {
			continue;
		}
		char *name = target_name(token_target(t));
		if (!is_special_target(name) &&
		    !is_known_target(parser, name) &&
		    !set_contains(targets, name) &&
		    (param->keyfilter == NULL || param->keyfilter(parser, name, param->keyuserdata))) {
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
			set_add(targets, name);
			param->found = 1;
			if (param->return_values) {
				array_append(param->keys, xstrdup(name));
				array_append(param->values, xstrdup(name));
			}
		}
	}

	set_free(targets);

	return NULL;
}

