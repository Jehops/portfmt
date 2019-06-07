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

#include "array.h"
#include "parser.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

struct Array *
refactor_sanitize_eol_comments(struct Parser *parser, struct Array *ptokens, const void *userdata)
{
	/* Try to push end of line comments out of the way above
	 * the variable as a way to preserve them.  They clash badly
	 * with sorting tokens in variables.  We could add more
	 * special cases for this, but often having them at the top
	 * is just as good.
	 */

	struct Array *tokens = array_new(sizeof(struct Array *));
	struct Token *last_token = NULL;
	ssize_t last_token_index = -1;
	ssize_t placeholder_index = -1;
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			last_token = NULL;
			last_token_index = -1;
			placeholder_index = array_len(tokens);
			array_append(tokens, NULL);
			array_append(tokens, t);
			break;
		case VARIABLE_TOKEN:
			last_token = t;
			last_token_index = array_len(tokens);
			array_append(tokens, t);
			break;
		case VARIABLE_END:
			if (placeholder_index > -1 && last_token_index > -1 &&
			    !preserve_eol_comment(last_token)) {
				struct Token *comment = token_new2(COMMENT, token_lines(last_token), token_data(last_token), NULL, token_conditional(last_token), NULL);
				parser_mark_for_gc(parser, comment);
				parser_mark_edited(parser, comment);
				array_set(tokens, placeholder_index, comment);
				array_set(tokens, last_token_index, NULL);
			}
			array_append(tokens, t);
			break;
		default:
			array_append(tokens, t);
			break;
		}
	}

	ptokens = array_new(sizeof(struct Array *));
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (t != NULL) {
			array_append(ptokens, t);
		}
	}
	array_free(tokens);

	return ptokens;
}
