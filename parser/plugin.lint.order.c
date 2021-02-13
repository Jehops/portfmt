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

#include <libias/array.h>
#include <libias/diff.h>
#include <libias/util.h>

#include "conditional.h"
#include "parser.h"
#include "parser/edits.h"
#include "rules.h"
#include "target.h"
#include "token.h"
#include "variable.h"

static int check_target_order(struct Parser *, struct Array *, int, int);
static int check_variable_order(struct Parser *, struct Array *, int);
static int output_diff(struct Parser *, struct Array *, struct Array *, int);

enum SkipDeveloperState {
	SKIP_DEVELOPER_INIT,
	SKIP_DEVELOPER_IF,
	SKIP_DEVELOPER_SKIP,
	SKIP_DEVELOPER_END,
};

static enum SkipDeveloperState
skip_developer_only(enum SkipDeveloperState state, struct Token *t)
{
	switch(token_type(t)) {
	case CONDITIONAL_START:
		switch (conditional_type(token_conditional(t))) {
		case COND_IF:
			return SKIP_DEVELOPER_IF;
		default:
			return SKIP_DEVELOPER_INIT;
		}
	case CONDITIONAL_TOKEN:
		switch (state) {
		case SKIP_DEVELOPER_INIT:
		case SKIP_DEVELOPER_END:
			break;
		case SKIP_DEVELOPER_IF:
			return SKIP_DEVELOPER_SKIP;
		case SKIP_DEVELOPER_SKIP:
			if (token_data(t) &&
			    (strcmp(token_data(t), "defined(DEVELOPER)") == 0 ||
			     strcmp(token_data(t), "defined(MAINTAINER_MODE)") == 0 ||
			     strcmp(token_data(t), "make(makesum)") == 0)) {
				return SKIP_DEVELOPER_END;
			}
			break;
		}
		return SKIP_DEVELOPER_INIT;
	default:
		return state;
	}
}

static struct Array *
variable_list(struct Parser *parser, struct Array *tokens)
{
	struct Array *output = array_new();
	struct Array *vars = array_new();
	enum SkipDeveloperState developer_only = SKIP_DEVELOPER_INIT;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (is_include_bsd_port_mk(t)) {
			break;
		}
		developer_only = skip_developer_only(developer_only, t);
		if (developer_only == SKIP_DEVELOPER_END ||
		    token_type(t) != VARIABLE_START) {
			continue;
		}
		char *var = variable_name(token_variable(t));
		// Ignore port local variables that start with an _
		if (var[0] != '_' && array_find(vars, var, str_compare, NULL) == -1) {
			array_append(vars, var);
		}
	}

	enum BlockType block = BLOCK_UNKNOWN;
	enum BlockType last_block = BLOCK_UNKNOWN;
	int flag = 0;
	for (size_t i = 0; i < array_len(vars); i++) {
		char *var = array_get(vars, i);
		block = variable_order_block(parser, var);
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

static struct Array *
target_list(struct Array *tokens)
{
	struct Array *targets = array_new();
	enum SkipDeveloperState developer_only = SKIP_DEVELOPER_INIT;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		developer_only = skip_developer_only(developer_only, t);
		if (developer_only == SKIP_DEVELOPER_END ||
		    token_type(t) != TARGET_START) {
			continue;
		}
		char *target = target_name(token_target(t));
		// Ignore port local targets that start with an _
		if (target[0] != '_' && !is_special_target(target) &&
		    array_find(targets, target, str_compare, NULL) == -1) {
			array_append(targets, target);
		}
	}

	return targets;
}

int
check_variable_order(struct Parser *parser, struct Array *tokens, int no_color)
{
	struct Array *origin = variable_list(parser, tokens);

	struct Array *vars = array_new();
	enum SkipDeveloperState developer_only = SKIP_DEVELOPER_INIT;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		if (is_include_bsd_port_mk(t)) {
			break;
		}
		developer_only = skip_developer_only(developer_only, t);
		if (developer_only == SKIP_DEVELOPER_END ||
		    token_type(t) != VARIABLE_START) {
			continue;
		}
		char *var = variable_name(token_variable(t));
		// Ignore port local variables that start with an _
		if (var[0] != '_' && array_find(vars, var, str_compare, NULL) == -1) {
			array_append(vars, var);
		}
	}

	array_sort(vars, compare_order, parser);

	struct Array *target = array_new();
	struct Array *unknowns = array_new();
	enum BlockType block = BLOCK_UNKNOWN;
	enum BlockType last_block = BLOCK_UNKNOWN;
	int flag = 0;
	for (size_t i = 0; i < array_len(vars); i++) {
		char *var = array_get(vars, i);
		if ((block = variable_order_block(parser, var)) != BLOCK_UNKNOWN) {
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

	array_sort(unknowns, str_compare, NULL);
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

	return output_diff(parser, origin, target, no_color);
}

int
check_target_order(struct Parser *parser, struct Array *tokens, int no_color, int status_var)
{
	struct Array *targets = target_list(tokens);
	struct Array *origin = array_new();

	if (status_var) {
		array_append(origin, xstrdup(""));
	}
	array_append(origin, xstrdup("# Out of order targets"));
	for (size_t i = 0; i < array_len(targets); i++) {
		char *name = array_get(targets, i);
		if (is_known_target(parser, name)) {
			char *buf;
			xasprintf(&buf, "%s:", name);
			array_append(origin, buf);
		}
	}

	array_sort(targets, compare_target_order, parser);

	struct Array *target = array_new();
	if (status_var) {
		array_append(target, xstrdup(""));
	}
	array_append(target, xstrdup("# Out of order targets"));
	for (size_t i = 0; i < array_len(targets); i++) {
		char *name = array_get(targets, i);
		if (is_known_target(parser, name)) {
			char *buf;
			xasprintf(&buf, "%s:", name);
			array_append(target, buf);
		}
	}

	struct Array *unknowns = array_new();
	for (size_t i = 0; i< array_len(targets); i++) {
		char *name = array_get(targets, i);
		if (!is_known_target(parser, name) && name[0] != '_') {
			char *buf;
			xasprintf(&buf, "%s:", name);
			array_append(unknowns, buf);
		}
	}
	array_free(targets);

	int status_target = 0;
	if ((status_target = output_diff(parser, origin, target, no_color)) == -1) {
		goto cleanup;
	}

	if (array_len(unknowns) > 0) {
		if (status_var || status_target) {
			parser_enqueue_output(parser, "\n");
		}
		status_target = 1;
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_CYAN);
		}
		parser_enqueue_output(parser, "# Unknown targets");
		if (!no_color) {
			parser_enqueue_output(parser, ANSI_COLOR_RESET);
		}
		parser_enqueue_output(parser, "\n");
		for (size_t i = 0; i < array_len(unknowns); i++) {
			char *name = array_get(unknowns, i);
			parser_enqueue_output(parser, name);
			parser_enqueue_output(parser, "\n");
		}
	}

cleanup:
	for (size_t i = 0; i < array_len(unknowns); i++) {
		free(array_get(unknowns, i));
	}
	array_free(unknowns);

	return status_target;
}

static int
output_diff(struct Parser *parser, struct Array *origin, struct Array *target, int no_color)
{
	int status = 0;
	struct diff p;
	int rc = array_diff(origin, target, &p, str_compare, NULL);
	if (rc <= 0) {
		status = -1;
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
		status = 0;
		goto done;
	}

	status = 1;
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

	return status;
}

struct Array *
lint_order(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	int *status = (int*)userdata;
	struct ParserSettings settings = parser_settings(parser);
	if (!(settings.behavior & PARSER_OUTPUT_RAWLINES)) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		xasprintf(error_msg, "needs PARSER_OUTPUT_RAWLINES");
		return NULL;
	}
	int no_color = settings.behavior & PARSER_OUTPUT_NO_COLOR;

	int status_var;
	if ((status_var = check_variable_order(parser, tokens, no_color)) == -1) {
		*error = PARSER_ERROR_EDIT_FAILED;
		*error_msg = xstrdup("lint_order: cannot compute difference");
		return NULL;
	}

	int status_target;
	if ((status_target = check_target_order(parser, tokens, no_color, status_var)) == -1) {
		*error = PARSER_ERROR_EDIT_FAILED;
		*error_msg = xstrdup("lint_order: cannot compute difference");
		return NULL;
	}

	if (status != NULL && (status_var > 0 || status_target > 0)) {
		*status = 1;
	}

	return NULL;
}

