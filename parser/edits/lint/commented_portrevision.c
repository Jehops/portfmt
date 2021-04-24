/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Tobias Kortkamp <tobik@FreeBSD.org>
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
#include <string.h>

#include <libias/array.h>
#include <libias/set.h>
#include <libias/util.h>

#include "parser.h"
#include "parser/edits.h"
#include "token.h"

PARSER_EDIT(lint_commented_portrevision)
{
	struct Set **retval = userdata;

	int no_color = parser_settings(parser).behavior & PARSER_OUTPUT_NO_COLOR;
	struct Set *comments = set_new(str_compare, NULL, free);
	struct ParserSettings settings;

	ARRAY_FOREACH(ptokens, struct Token *, t) {
		if (token_type(t) != COMMENT) {
			continue;
		}

		char *comment = str_trim(token_data(t));
		if (strlen(comment) <= 1) {
			free(comment);
			continue;
		}

		parser_init_settings(&settings);
		struct Parser *subparser = parser_new(&settings);
		if (parser_read_from_buffer(subparser, comment + 1, strlen(comment) - 1) != PARSER_ERROR_OK) {
			free(comment);
			parser_free(subparser);
			continue;
		}
		if (parser_read_finish(subparser) != PARSER_ERROR_OK) {
			free(comment);
			parser_free(subparser);
			continue;
		}

		struct Array *revtokens = NULL;
		if (parser_lookup_variable(subparser, "PORTEPOCH", &revtokens, NULL) ||
		    parser_lookup_variable(subparser, "PORTREVISION", &revtokens, NULL)) {
			if (array_len(revtokens) <= 1) {
				if (set_contains(comments, comment)) {
					free(comment);
				} else {
					set_add(comments, comment);
				}
			} else {
				free(comment);
			}
			array_free(revtokens);
		} else {
			free(comment);
		}

		parser_free(subparser);
	}

	if (retval == NULL && set_len(comments) > 0) {
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_CYAN);
		}
		parser_enqueue_output(parser, "# Commented PORTEPOCH or PORTREVISION\n");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
		SET_FOREACH(comments, const char *, comment) {
			parser_enqueue_output(parser, comment);
			parser_enqueue_output(parser, "\n");
		}
	}

	if (retval) {
		*retval = comments;
	} else {
		set_free(comments);
	}

	return NULL;
}
