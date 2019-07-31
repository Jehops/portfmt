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

#if HAVE_ERR
# include <err.h>
#endif
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "diff.h"
#include "parser.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static struct Array *
variable_list(struct Array *tokens)
{
	struct Array *output = array_new(sizeof(char *));
	struct Array *vars = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (is_include_bsd_port_mk(t)) {
			break;
		}
		if (token_type(t) != VARIABLE_START) {
			continue;
		}
		char *var = variable_name(token_variable(t));
		// Ignore port local variables that start with an _
		if (var[0] != '_') {
			array_append_unique(vars, var, str_compare);
		}
	}

	enum BlockType block = BLOCK_UNKNOWN;
	enum BlockType last_block = BLOCK_UNKNOWN;
	int flag = 0;
	for (size_t i = 0; i < array_len(vars); i++) {
		char *var = array_get(vars, i);
		block = variable_order_block(var);
		if (block != last_block) {
			if (flag && block != last_block) {
				array_append(output, xstrdup(""));
			}
			char *buf;
			xasprintf(&buf, "# %s", blocktype_tostring(block));
			array_append(output, buf);
		}
		flag = 1;
		array_append(output, xstrdup(var));
		last_block = block;
	}

	array_free(vars);

	return output;
}

struct Array *
lint_order(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	int *status = (int*)userdata;
	struct ParserSettings settings = parser_settings(parser);
	if (!(settings.behavior & PARSER_OUTPUT_RAWLINES)) {
		return NULL;
	}
	int no_color = settings.behavior & PARSER_OUTPUT_NO_COLOR;

	struct Array *origin = variable_list(tokens);

	struct Array *vars = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (is_include_bsd_port_mk(t)) {
			break;
		}
		if (token_type(t) != VARIABLE_START) {
			continue;
		}
		char *var = variable_name(token_variable(t));
		// Ignore port local variables that start with an _
		if (var[0] != '_') {
			array_append_unique(vars, var, str_compare);
		}
	}

	array_sort(vars, compare_order);

	struct Array *target = array_new(sizeof(char *));
	struct Array *unknowns = array_new(sizeof(char *));
	enum BlockType block = BLOCK_UNKNOWN;
	enum BlockType last_block = BLOCK_UNKNOWN;
	int flag = 0;
	for (size_t i = 0; i < array_len(vars); i++) {
		char *var = array_get(vars, i);
		if ((block = variable_order_block(var)) != BLOCK_UNKNOWN) {
			if (block != last_block) {
				if (flag && block != last_block) {
					array_append(target, xstrdup(""));
				}
				char *buf;
				xasprintf(&buf, "# %s", blocktype_tostring(block));
				array_append(target, buf);
			}
			flag = 1;
			array_append(target, xstrdup(var));
			last_block = block;
		} else {
			array_append(unknowns, var);
			last_block = BLOCK_UNKNOWN;
		}
	}

	array_sort(unknowns, str_compare);
	if (array_len(vars) > 0 && array_len(unknowns) > 0) {
		array_append(target, xstrdup(""));
		char *buf;
		xasprintf(&buf, "# %s", blocktype_tostring(BLOCK_UNKNOWN));
		array_append(target, buf);
		array_append(target, xstrdup("# WARNING:"));
		array_append(target, xstrdup("# Portclippy did not recognize the following variables."));
		array_append(target, xstrdup("# They could be local variables only, misspellings of"));
		array_append(target, xstrdup("# framework variables, or Portclippy needs to be made aware"));
		array_append(target, xstrdup("# of them.  Please double check them."));
		array_append(target, xstrdup("#"));
		array_append(target, xstrdup("# Prefix them with an _ to tell Portclippy to ignore them."));
		array_append(target, xstrdup("# This is also an important signal for other contributors"));
		array_append(target, xstrdup("# who are working on your port.  It removes any doubt of"));
		array_append(target, xstrdup("# whether they are framework variables or not and whether"));
		array_append(target, xstrdup("# they are safe to remove/rename or not."));
	}
	for (size_t i = 0; i < array_len(unknowns); i++) {
		char *var = array_get(unknowns, i);
		array_append(target, xstrdup(var));
	}

	array_free(unknowns);
	array_free(vars);

	// Diff

	struct diff p;
	int rc = array_diff(origin, target, &p, str_compare);
	if (rc <= 0) {
		*error = PARSER_ERROR_EDIT_FAILED;
		*error_msg = xstrdup("lint_order: cannot compute difference");
		goto cleanup;
	}

	size_t edits = 0;
	for (size_t i = 0; i < p.sessz; i++) {
		switch (p.ses[i].type) {
		case DIFF_ADD:
		case DIFF_DELETE:
			edits++;
			break;
		default:
			break;
		}
	}
	if (edits == 0) {
		*status = 0;
		goto done;
	}

	*status = 1;
	for (size_t i = 0; i < p.sessz; i++) {
		const char *s = *(const char **)p.ses[i].e;
		if (strlen(s) == 0) {
			parser_enqueue_output(parser, "\n");
			continue;
		} else if (*s == '#') {
			if (p.ses[i].type != DIFF_DELETE) {
				if (!no_color) {
					parser_enqueue_output(parser, ANSI_COLOR_CYAN);
				}
				parser_enqueue_output(parser, s);
				parser_enqueue_output(parser, "\n");
				if (!no_color) {
					parser_enqueue_output(parser, ANSI_COLOR_RESET);
				}
			}
			continue;
		}
		switch (p.ses[i].type) {
		case DIFF_ADD:
			if (!no_color) {
				parser_enqueue_output(parser, ANSI_COLOR_GREEN);
			}
			parser_enqueue_output(parser, "+");
			break;
		case DIFF_DELETE:
			if (!no_color) {
				parser_enqueue_output(parser, ANSI_COLOR_RED);
			}
			parser_enqueue_output(parser, "-");
			break;
		default:
			break;
		}
		parser_enqueue_output(parser, s);
		parser_enqueue_output(parser, "\n");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
	}

done:
	free(p.ses);
	free(p.lcs);

cleanup:
	for (size_t i = 0; i < array_len(origin); i++) {
		free(array_get(origin, i));
	}
	array_free(origin);

	for (size_t i = 0; i < array_len(target); i++) {
		free(array_get(target, i));
	}
	array_free(target);

	return NULL;
}
