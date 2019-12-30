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

#include <sys/param.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <assert.h>
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "parser/plugin.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

struct VariableMergeParameter {
	enum ParserMergeBehavior behavior;
	struct Variable *var;
	struct Array *nonvars;
	struct Array *values;
};

static void append_empty_line(struct Parser *, struct Array *, struct Range *);
static void append_new_variable(struct Parser *, struct Array *, struct Variable *, struct Range *);
static struct Token *find_next_token(struct Array *, size_t, int, int, int);
static struct Array *extract_tokens(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
static struct Array *insert_variable(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
static struct Array *merge_existent_var(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
static void append_tokens(struct Parser *, struct Array *, struct Array *);
static void append_values(struct Parser *, struct Array *, enum VariableModifier, const struct VariableMergeParameter *);
static void assign_values(struct Parser *, struct Array *, enum VariableModifier, const struct VariableMergeParameter *);

struct Array *
extract_tokens(struct Parser *subparser, struct Array *subtokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	struct Array **tokens = (struct Array **)userdata;

	*tokens = subtokens;
	return NULL;
}

void
append_values(struct Parser *parser, struct Array *tokens, enum VariableModifier mod, const struct VariableMergeParameter *params)
{
	for (size_t j = 0; j < array_len(params->values); j++) {
		struct Token *v = array_get(params->values, j);
		switch (token_type(v)) {
		case VARIABLE_TOKEN:
			if (variable_cmp(params->var, token_variable(v)) == 0) {
				struct Token *edited = token_clone(v, NULL);
				variable_set_modifier(token_variable(edited), mod);
				array_append(tokens, edited);
				parser_mark_edited(parser, edited);
			}
			break;
		default:
			break;
		}
	}
}

void
assign_values(struct Parser *parser, struct Array *tokens, enum VariableModifier mod, const struct VariableMergeParameter *params)
{
	for (size_t j = 0; j < array_len(params->values); j++) {
		struct Token *v = array_get(params->values, j);
		switch (token_type(v)) {
		case VARIABLE_START:
		case VARIABLE_TOKEN:
		case VARIABLE_END:
			if (variable_cmp(params->var, token_variable(v)) == 0) {
				struct Token *edited = token_clone(v, NULL);
				variable_set_modifier(token_variable(edited), mod);
				array_append(tokens, edited);
				parser_mark_edited(parser, edited);
			}
			break;
		default:
			break;
		}
	}
}

void
append_tokens(struct Parser *parser, struct Array *tokens, struct Array *nonvars)
{
	for (size_t i = 0; i < array_len(nonvars); i++) {
		struct Token *c = token_clone(array_get(nonvars, i), NULL);
		array_append(tokens, c);
		parser_mark_edited(parser, c);
	}
	array_truncate(nonvars);
}

void
append_empty_line(struct Parser *parser, struct Array *tokens, struct Range *lines)
{
	struct Token *t = token_new_comment(lines, "", NULL);
	array_append(tokens, t);
	parser_mark_edited(parser, t);
}

void
append_new_variable(struct Parser *parser, struct Array *tokens, struct Variable *var, struct Range *lines) 
{
	struct Token *t = token_new_variable_start(lines, var);
	array_append(tokens, t);
	parser_mark_edited(parser, t);
	t = token_new_variable_end(lines, var);
	array_append(tokens, t);
	parser_mark_edited(parser, t);
}

struct Token *
find_next_token(struct Array *tokens, size_t start, int a1, int a2, int a3)
{
	for (size_t i = start; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		int type = token_type(t);
		if (type == a1 || type == a2 || type == a3) {
			return t;
		}
	}

	return NULL;
}

struct Array *
insert_variable(struct Parser *parser, struct Array *ptokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	struct Variable *var = (struct Variable *)userdata;
	enum BlockType varblock = variable_order_block(parser, variable_name(var));
	ssize_t ptokenslen = array_len(ptokens);

	ssize_t insert_after = -1;
	enum BlockType block_before = BLOCK_UNKNOWN;
	for (ssize_t i = 0; i < ptokenslen; i++) {
		struct Token *t = array_get(ptokens, i);
		if (token_type(t) != VARIABLE_END) {
			continue;
		}

		char *a = variable_name(token_variable(t));
		char *b = variable_name(var);
		int cmp = compare_order(&a, &b, parser);
		assert(cmp != 0);
		if (cmp < 0) {
			block_before = variable_order_block(parser, a);
			insert_after = i;
		}
	}

	if (insert_after == -1) {
		// No variable found where we could insert our new
		// var.  Insert it before any conditional or target
		// if there are any.
		struct Array *tokens = array_new();
		int added = 0;
		for (ssize_t i = 0; i < ptokenslen; i++) {
			struct Token *t = array_get(ptokens, i);
			if (!added && (token_type(t) == CONDITIONAL_START ||
				       token_type(t) == TARGET_START)) {
				append_new_variable(parser, tokens, var, token_lines(t));
				append_empty_line(parser, tokens, token_lines(t));
				added = 1;
			}
			array_append(tokens, t);
		}
		if (!added) {
			// Prepend it instead if there are no conditionals or targets
			array_truncate(tokens);
			struct Range *lines = &(struct Range){ 0, 1 };
			if (array_len(ptokens) > 0) {
				lines = token_lines(array_get(ptokens, array_len(ptokens) - 1));
			}
			append_new_variable(parser, tokens, var, lines);
			int empty_line_added = 0;
			for (ssize_t i = 0; i < ptokenslen; i++) {
				struct Token *t = array_get(ptokens, i);
				if (!empty_line_added && token_type(t) == VARIABLE_START &&
				    variable_order_block(parser, variable_name(token_variable(t))) != varblock) {
					append_empty_line(parser, tokens, token_lines(t));
					empty_line_added = 1;
				}
				array_append(tokens, t);
			}
		}
		return tokens;
	}

	assert(insert_after > -1);
	assert(insert_after < ptokenslen);

	struct Array *tokens = array_new();
	int insert_flag = 0;
	int added = 0;
	for (ssize_t i = 0; i < ptokenslen; i++) {
		struct Token *t = array_get(ptokens, i);
		if (insert_flag) {
			insert_flag = 0;
			if (block_before != varblock) {
				append_empty_line(parser, tokens, token_lines(t));
				append_new_variable(parser, tokens, var, token_lines(t));
				added = 1;
				struct Token *next = find_next_token(ptokens, i, CONDITIONAL_START, TARGET_START, VARIABLE_START);
				if (next && token_type(next) != VARIABLE_START) {
					append_empty_line(parser, tokens, token_lines(t));
				}
				if (token_type(t) == COMMENT && strcmp(token_data(t), "") == 0) {
					next = find_next_token(ptokens, i, VARIABLE_START, -1, -1);
					if (next) {
						enum BlockType block_next = variable_order_block(parser, variable_name(token_variable(next)));
						if (block_next == varblock) {
							continue;
						}
					} else {
						continue;
					}
				}
			} else {
				append_new_variable(parser, tokens, var, token_lines(t));
				added = 1;
			}
		} else if (token_type(t) == VARIABLE_END && insert_after == i) {
			insert_flag = 1;
		}

		array_append(tokens, t);
	}

	if (!added) {
		struct Range *lines = &(struct Range){ 0, 1 };
		if (array_len(ptokens) > 0) {
			lines = token_lines(array_get(ptokens, array_len(ptokens) - 1));
		}
		if (block_before != varblock) {
			append_empty_line(parser, tokens, lines);
		}
		append_new_variable(parser, tokens, var, lines);
	}

	return tokens;
}

struct Array *
merge_existent_var(struct Parser *parser, struct Array *ptokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	const struct VariableMergeParameter *params = userdata;
	struct Array *tokens = array_new();

	int found = 0;
	enum VariableModifier mod = variable_modifier(params->var);
	for (size_t i = 0; i < array_len(ptokens); i++) {
		struct Token *t = array_get(ptokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			if (variable_cmp(params->var, token_variable(t)) == 0) {
				found = 1;
				if (mod == MODIFIER_ASSIGN ||
				    (mod == MODIFIER_OPTIONAL && (params->behavior & PARSER_MERGE_OPTIONAL_LIKE_ASSIGN))) {
					append_tokens(parser, tokens, params->nonvars);
					assign_values(parser, tokens, variable_modifier(token_variable(t)), params);
				} else if (mod == MODIFIER_APPEND) {
					append_tokens(parser, tokens, params->nonvars);
					array_append(tokens, t);
					parser_mark_edited(parser, t);
				} else if (mod == MODIFIER_SHELL) {
					parser_mark_for_gc(parser, t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case VARIABLE_TOKEN:
			if (found) {
				if (mod == MODIFIER_ASSIGN || mod == MODIFIER_OPTIONAL) {
					// nada
				} else if (mod == MODIFIER_SHELL) {
					parser_mark_for_gc(parser, t);
				} else if (mod == MODIFIER_APPEND) {
					array_append(tokens, t);
					parser_mark_edited(parser, t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		case VARIABLE_END:
			if (found) {
				found = 0;
				if (mod == MODIFIER_APPEND) {
					append_values(parser, tokens, variable_modifier(token_variable(t)), params);
					array_append(tokens, t);
					parser_mark_edited(parser, t);
				} else if (mod == MODIFIER_SHELL) {
					parser_mark_for_gc(parser, t);
				}
			} else {
				array_append(tokens, t);
			}
			break;
		default:
			array_append(tokens, t);
			break;
		}
	}

	append_tokens(parser, tokens, params->nonvars);

	return tokens;
}

static struct Array *
edit_merge(struct Parser *parser, struct Array *ptokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	const struct ParserPluginEdit *params = userdata;
	if (params == NULL ||
	    params->arg1 != NULL ||
	    params->subparser == NULL) {
		*error = PARSER_ERROR_INVALID_ARGUMENT;
		return NULL;
	}

	struct Array *subtokens = NULL;
	parser_edit_with_fn(params->subparser, extract_tokens, &subtokens);

	struct Variable *var = NULL;
	int merge = 0;
	struct Array *mergetokens = array_new();
	struct Array *nonvars = array_new();
	for (size_t i = 0; i < array_len(subtokens); i++) {
		struct Token *t = array_get(subtokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			var = token_variable(t);
			switch (variable_modifier(var)) {
			case MODIFIER_SHELL:
				if (!(params->merge_behavior & PARSER_MERGE_SHELL_IS_DELETE)) {
					break;
				}
				/* fallthrough */
			case MODIFIER_OPTIONAL:
				if (variable_modifier(var) == MODIFIER_OPTIONAL &&
				    !(params->merge_behavior & PARSER_MERGE_OPTIONAL_LIKE_ASSIGN)) {
					break;
				}
				/* fallthrough */
			case MODIFIER_APPEND:
			case MODIFIER_ASSIGN:
				if (!parser_lookup_variable(parser, variable_name(var), NULL, NULL)) {
					*error = parser_edit_with_fn(parser, insert_variable, var);
					if (*error != PARSER_ERROR_OK) {
						goto cleanup;
					}
					parser_edit_with_fn(parser, extract_tokens, &ptokens);
				}
				merge = 1;
				array_append(mergetokens, t);
				break;
			default:
				merge = 0;
				break;
			}
			break;
		case VARIABLE_TOKEN:
			if (merge) {
				array_append(mergetokens, t);
			}
			break;
		case VARIABLE_END:
			if (merge) {
				array_append(mergetokens, t);
				struct VariableMergeParameter par;
				par.behavior = params->merge_behavior;
				par.var = var;
				par.nonvars = nonvars;
				par.values = mergetokens;
				*error = parser_edit_with_fn(parser, merge_existent_var, &par);
				if (*error != PARSER_ERROR_OK) {
					goto cleanup;
				}
				parser_edit_with_fn(parser, extract_tokens, &ptokens);
				array_truncate(nonvars);
			}
			var = NULL;
			merge = 0;
			array_truncate(mergetokens);
			break;
		case COMMENT:
			if ((params->merge_behavior & PARSER_MERGE_COMMENTS) &&
			    (array_len(nonvars) > 0 || strcmp(token_data(t), "") != 0)) {
				array_append(nonvars, t);
			}
			break;
		default:
			break;
		}
	}

cleanup:
	array_free(nonvars);
	array_free(mergetokens);
	return NULL;
}

PLUGIN("edit.merge", edit_merge);
