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
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "conditional.h"
#include "parser.h"
#include "token.h"
#include "variable.h"

struct Array *
refactor_sanitize_append_modifier(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	/* Sanitize += before bsd.options.mk */
	ssize_t start = -1;
	struct Array *seen = array_new(sizeof(struct Variable *));
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			start = i;
			break;
		case VARIABLE_END: {
			if (start < 0) {
				continue;
			}
			if (!array_append_unique(seen, token_variable(t), variable_compare)) {
				start = -1;
				continue;
			}
			for (size_t j = start; j <= i; j++) {
				struct Token *o = array_get(ptokens, j);
				if (strcmp(variable_name(token_variable(o)), "CXXFLAGS") != 0 &&
				    strcmp(variable_name(token_variable(o)), "CFLAGS") != 0 &&
				    strcmp(variable_name(token_variable(o)), "LDFLAGS") != 0 &&
				    variable_modifier(token_variable(o)) == MODIFIER_APPEND) {
					variable_set_modifier(token_variable(o), MODIFIER_ASSIGN);
				}
			}
			start = -1;
			break;
		} case CONDITIONAL_TOKEN:
			if (conditional_type(token_conditional(t)) == COND_INCLUDE &&
			    (strcmp(token_data(t), "<bsd.port.options.mk>") == 0 ||
			     strcmp(token_data(t), "<bsd.port.pre.mk>") == 0 ||
			     strcmp(token_data(t), "<bsd.port.post.mk>") == 0 ||
			     strcmp(token_data(t), "<bsd.port.mk>") == 0)) {
				goto end;
			}
		default:
			break;
		}
	}
end:
	array_free(seen);

	return ptokens;
}
