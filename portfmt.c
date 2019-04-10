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

#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/uio.h>
#if HAVE_SBUF
# include <sys/sbuf.h>
#endif
#if HAVE_CAPSICUM
# include <sys/capsicum.h>
# include "capsicum_helpers.h"
#endif
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
#include "conditional.h"
#include "rules.h"
#include "target.h"
#include "util.h"
#include "variable.h"

enum ParserBehavior {
	PARSER_DEFAULT = 0,
	PARSER_COLLAPSE_ADJACENT_VARIABLES = 2,
	PARSER_SANITIZE_APPEND = 4,
	PARSER_UNSORTED_VARIABLES = 8,
};

enum TokenType {
	COMMENT = 0,
	CONDITIONAL_END,
	CONDITIONAL_TOKEN,
	CONDITIONAL_START,
	EMPTY,
	INLINE_COMMENT,
	PORT_MK,
	PORT_OPTIONS_MK,
	PORT_PRE_MK,
	TARGET_COMMAND_END,
	TARGET_COMMAND_START,
	TARGET_COMMAND_TOKEN,
	TARGET_END,
	TARGET_START,
	VARIABLE_END,
	VARIABLE_START,
	VARIABLE_TOKEN,
};

struct Range {
	size_t start;
	size_t end;
};

struct Token {
	enum TokenType type;
	struct sbuf *data;
	struct Conditional *cond;
	struct Variable *var;
	struct Target *target;
	int goalcol;
	struct Range lines;
	int ignore;
};

struct Parser {
	enum ParserBehavior behavior;
	int continued;
	int in_target;
	struct Range lines;
	int skip;
	struct sbuf *inbuf;
	struct sbuf *condname;
	struct sbuf *targetname;
	struct sbuf *varname;

	struct Array *tokens;
	struct Array *result;
};

static size_t consume_comment(struct sbuf *);
static size_t consume_conditional(struct sbuf *);
static size_t consume_target(struct sbuf *);
static size_t consume_token(struct Parser *, struct sbuf *, size_t, char, char, int);
static size_t consume_var(struct sbuf *);
static struct sbuf *range_tostring(struct Range *);
static struct Parser *parser_new(enum ParserBehavior);
static void parser_append_token(struct Parser *, enum TokenType, struct sbuf *);
static void parser_enqueue_output(struct Parser *, struct sbuf *);
static void parser_free(struct Parser *);
static struct Token *parser_get_token(struct Parser *, size_t);
static void parser_find_goalcols(struct Parser *);
static void parser_generate_output_helper(struct Parser *, struct Array *);
static void parser_generate_output(struct Parser *);
static void parser_dump_tokens(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read(struct Parser *, char *);
static void parser_read_internal(struct Parser *, struct sbuf *);
static void parser_read_finish(struct Parser *);
static void parser_collapse_adjacent_variables(struct Parser *);
static void parser_sanitize_append_modifier(struct Parser *);
static void parser_write(struct Parser *, int);
static void parser_tokenize(struct Parser *, struct sbuf *, enum TokenType, ssize_t);

static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
static int tokcompare(const void *, const void *);
static void usage(void);

static int WRAPCOL = 80;

size_t
consume_comment(struct sbuf *buf)
{
	size_t pos = 0;
	if (sbuf_startswith(buf, "#")) {
		pos = sbuf_len(buf);
	}
	return pos;
}

size_t
consume_conditional(struct sbuf *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_CONDITIONAL, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

size_t
consume_target(struct sbuf *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_TARGET, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

size_t
consume_token(struct Parser *parser, struct sbuf *line, size_t pos,
	      char startchar, char endchar, int eol_ok)
{
	char *linep = sbuf_data(line);
	int counter = 0;
	int escape = 0;
	ssize_t i = pos;
	for (; i < sbuf_len(line); i++) {
		char c = linep[i];
		if (escape) {
			escape = 0;
			continue;
		}
		if (startchar == endchar) {
			if (c == startchar) {
				if (counter == 1) {
					return i;
				} else {
					counter++;
				}
			} else if (c == '\\') {
				escape = 1;
			}
		} else {
			if (c == startchar) {
				counter++;
			} else if (c == endchar && counter == 1) {
				return i;
			} else if (c == endchar) {
				counter--;
			} else if (c == '\\') {
				escape = 1;
			}
		}
	}
	if (!eol_ok) {
		errx(1, "tokenizer: %s: expected %c", sbuf_data(range_tostring(&parser->lines)), endchar);
	} else {
		return i;
	}
}

size_t
consume_var(struct sbuf *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_VAR, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

struct sbuf *
range_tostring(struct Range *range)
{
	assert(range);
	assert(range->start < range->end);

	struct sbuf *s = sbuf_dup(NULL);
	if (range->start == range->end - 1) {
		sbuf_printf(s, "%zu", range->start);
	} else {
		sbuf_printf(s, "%zu-%zu", range->start, range->end - 1);
	}
	sbuf_finishx(s);

	return s;
}

struct Parser *
parser_new(enum ParserBehavior behavior)
{
	struct Parser *parser = calloc(1, sizeof(struct Parser));
	if (parser == NULL) {
		err(1, "calloc");
	}

	parser->behavior = behavior;
	parser->result = array_new(sizeof(struct sbuf *));
	parser->tokens = array_new(sizeof(struct Token *));
	parser->lines.start = 1;
	parser->lines.end = 1;
	parser->inbuf = sbuf_dupstr(NULL);

	return parser;
}

void
parser_free(struct Parser *parser)
{
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (t->data) {
			sbuf_delete(t->data);
		}
		if (t->var) {
			variable_free(t->var);
		}
		if (t->target) {
			target_free(t->target);
		}
		free(t);
	}
	array_free(parser->tokens);

	for (size_t i = 0; i < array_len(parser->result); i++) {
		struct sbuf *s = array_get(parser->result, i);
		sbuf_delete(s);
	}
	array_free(parser->result);

	sbuf_delete(parser->inbuf);
	free(parser);
}

void
parser_append_token(struct Parser *parser, enum TokenType type, struct sbuf *v)
{
	struct Target *target = NULL;
	if (parser->targetname) {
		target = target_new(parser->targetname);
	}

	struct Conditional *cond = NULL;
	if (parser->condname) {
		cond = conditional_new(parser->condname);
	}

	struct Variable *var = NULL;
	if (parser->varname) {
		var = variable_new(parser->varname);
	}

	struct sbuf *data = NULL;
	if (v) {
		data = sbuf_dup(v);
		sbuf_finishx(data);
	}

	struct Token *o = xmalloc(sizeof(struct Token));
	o->type = type;
	o->data = data;
	o->cond = cond;
	o->target = target;
	o->var = var;
	o->goalcol = 0;
	o->lines = parser->lines;
	o->ignore = 0;
	array_append(parser->tokens, o);
}

void
parser_enqueue_output(struct Parser *parser, struct sbuf *s)
{
	if (!sbuf_done(s)) {
		sbuf_finishx(s);
	}
	array_append(parser->result, s);
}

struct Token *
parser_get_token(struct Parser *parser, size_t i)
{
	return array_get(parser->tokens, i);
}

void
parser_tokenize(struct Parser *parser, struct sbuf *line, enum TokenType type, ssize_t start)
{
	int dollar = 0;
	int escape = 0;
	char *linep = sbuf_data(line);
	struct sbuf *token = NULL;
	ssize_t i = start;
	size_t queued_tokens = 0;
	for (; i < sbuf_len(line); i++) {
		assert(i >= start);
		char c = linep[i];
		if (escape) {
			escape = 0;
			if (c == '#' || c == '\\' || c == '$') {
				continue;
			}
		}
		if (dollar) {
			if (dollar == 2) {
				if (c == '(') {
					i = consume_token(parser, line, i - 2, '(', ')', 0);
					continue;
				} else {
					dollar = 0;
				}
			} else if (c == '{') {
				i = consume_token(parser, line, i, '{', '}', 0);
				dollar = 0;
			} else if (c == '$') {
				dollar++;
			} else {
				fprintf(stderr, "%s\n", linep);
				errx(1, "tokenizer: %s: expected {", sbuf_data(range_tostring(&parser->lines)));
			}
		} else {
			if (c == ' ' || c == '\t') {
				struct sbuf *tmp = sbuf_substr_dup(line, start, i);
				sbuf_finishx(tmp);
				token = sbuf_strip_dup(tmp);
				sbuf_finishx(token);
				sbuf_delete(tmp);
				if (sbuf_strcmp(token, "") != 0 && sbuf_strcmp(token, "\\") != 0) {
					parser_append_token(parser, type, token);
					queued_tokens++;
				}
				sbuf_delete(token);
				token = NULL;
				start = i;
			} else if (c == '"') {
				i = consume_token(parser, line, i, '"', '"', 1);
			} else if (c == '\'') {
				i = consume_token(parser, line, i, '\'', '\'', 1);
			} else if (c == '`') {
				i = consume_token(parser, line, i, '`', '`', 1);
			} else if (c == '$') {
				dollar++;
			} else if (c == '\\') {
				escape = 1;
			} else if (c == '#') {
				/* Try to push end of line comments out of the way above
				 * the variable as a way to preserve them.  They clash badly
				 * with sorting tokens in variables.  We could add more
				 * special cases for this, but often having them at the top
				 * is just as good.
				 */
				struct sbuf *tmp = sbuf_substr_dup(line, i, sbuf_len(line));
				sbuf_finishx(tmp);
				token = sbuf_strip_dup(tmp);
				sbuf_finishx(token);
				sbuf_delete(tmp);
				if (sbuf_strcmp(token, "#") == 0 ||
				    sbuf_strcmp(token, "# empty") == 0 ||
				    sbuf_strcmp(token, "#none") == 0 ||
				    sbuf_strcmp(token, "# none") == 0) {
					parser_append_token(parser, type, token);
					queued_tokens++;
				} else {
					parser_append_token(parser, INLINE_COMMENT, token);
				}

				sbuf_delete(token);
				token = NULL;
				goto cleanup;
			}
		}
	}
	struct sbuf *tmp = sbuf_substr_dup(line, start, i);
	sbuf_finishx(tmp);
	token = sbuf_strip_dup(tmp);
	sbuf_finishx(token);
	sbuf_delete(tmp);
	if (sbuf_strcmp(token, "") != 0) {
		parser_append_token(parser, type, token);
		queued_tokens++;
	}

	sbuf_delete(token);
	token = NULL;
cleanup:
	if (queued_tokens == 0 && type == VARIABLE_TOKEN) {
		parser_append_token(parser, EMPTY, NULL);
	}
}

void
parser_propagate_goalcol(struct Parser *parser, size_t start, size_t end,
			 int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		struct Token *o = parser_get_token(parser, k);
		if (!o->ignore && o->var && !skip_goalcol(o->var)) {
			o->goalcol = moving_goalcol;
		}
	}
}

void
parser_find_goalcols(struct Parser *parser)
{
	int moving_goalcol = 0;
	int last = 0;
	ssize_t tokens_start = -1;
	ssize_t tokens_end = -1;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = parser_get_token(parser, i);
		if (o->ignore) {
			continue;
		}
		switch(o->type) {
		case VARIABLE_END:
		case VARIABLE_START:
			break;
		case VARIABLE_TOKEN:
			if (tokens_start == -1) {
				tokens_start = i;
			}
			tokens_end = i;

			if (o->var && skip_goalcol(o->var)) {
				o->goalcol = indent_goalcol(o->var);
			} else {
				moving_goalcol = MAX(indent_goalcol(o->var), moving_goalcol);
			}
			break;
		case TARGET_END:
		case TARGET_START:
		case CONDITIONAL_END:
		case CONDITIONAL_START:
		case TARGET_COMMAND_END:
		case TARGET_COMMAND_START:
		case TARGET_COMMAND_TOKEN:
			break;
		case COMMENT:
		case CONDITIONAL_TOKEN:
		case PORT_MK:
		case PORT_OPTIONS_MK:
		case PORT_PRE_MK:
			/* Ignore comments in between variables and
			 * treat variables after them as part of the
			 * same block, i.e., indent them the same way.
			 */
			if (sbuf_startswith(o->data, "#")) {
				continue;
			}
			if (tokens_start != -1) {
				parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
				moving_goalcol = 0;
				last = i;
				tokens_start = -1;
			}
			break;
		case EMPTY:
		case INLINE_COMMENT:
			break;
		default:
			errx(1, "Unhandled token type: %i", o->type);
		}
	}
	if (tokens_start != -1) {
		parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
	}
}

void
print_newline_array(struct Parser *parser, struct Array *arr)
{
	struct Token *o = array_get(arr, 0);
	assert(o && o->data != NULL);
	assert(sbuf_len(o->data) != 0);
	struct sbuf *sep;
	switch (o->type) {
	case VARIABLE_TOKEN: {
		struct sbuf *start = variable_tostring(o->var);
		size_t ntabs = ceil((MAX(16, o->goalcol) - sbuf_len(start)) / 8.0);
		sep = sbuf_dup(start);
		sbuf_cat(sep, repeat('\t', ntabs));
		break;
	} case CONDITIONAL_TOKEN:
		sep = sbuf_dupstr(NULL);
		break;
	case TARGET_COMMAND_TOKEN:
		sep = sbuf_dupstr("\t");
		break;
	default:
		errx(1, "unhandled token type: %i", o->type);
	}

	struct sbuf *end = sbuf_dupstr(" \\\n");
	for (size_t i = 0; i < array_len(arr); i++) {
		struct Token *o = array_get(arr, i);
		struct sbuf *line = o->data;
		if (!line || sbuf_len(line) == 0) {
			continue;
		}
		if (i == array_len(arr) - 1) {
			end = sbuf_dupstr("\n");
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		parser_enqueue_output(parser, end);
		switch (o->type) {
		case VARIABLE_TOKEN:
			if (i == 0) {
				size_t ntabs = ceil(MAX(16, o->goalcol) / 8.0);
				sep = sbuf_dupstr(repeat('\t', ntabs));
			}
			break;
		case CONDITIONAL_TOKEN:
			sep = sbuf_dupstr("\t");
			break;
		case TARGET_COMMAND_TOKEN:
			sep = sbuf_dupstr(repeat('\t', 2));
			break;
		default:
			errx(1, "unhandled token type: %i", o->type);
		}
	}
}

int
tokcompare(const void *a, const void *b)
{
	struct Token *ao = *(struct Token**)a;
	struct Token *bo = *(struct Token**)b;
	if (variable_cmp(ao->var, bo->var) == 0) {
		return compare_tokens(ao->var, ao->data, bo->data);
	}
	return strcasecmp(sbuf_data(ao->data), sbuf_data(bo->data));
#if 0
	# Hack to treat something like ${PYTHON_PKGNAMEPREFIX} or
	# ${RUST_DEFAULT} as if they were PYTHON_PKGNAMEPREFIX or
	# RUST_DEFAULT for the sake of approximately sorting them
	# correctly in *_DEPENDS.
	gsub(/[\$\{\}]/, "", a)
	gsub(/[\$\{\}]/, "", b)
#endif
}

void
print_token_array(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) < 2) {
		print_newline_array(parser, tokens);
		return;
	}

	struct Array *arr = array_new(sizeof(struct Token *));
	struct Token *o = array_get(tokens, 0);
	int wrapcol;
	if (o->var && ignore_wrap_col(o->var)) {
		wrapcol = 99999999;
	} else {
		/* Minus ' \' at end of line */
		wrapcol = WRAPCOL - o->goalcol - 2;
	}

	struct sbuf *row = sbuf_dupstr(NULL);
	sbuf_finishx(row);

	struct Token *token;
	for (size_t i = 0; i < array_len(tokens); i++) {
		token = array_get(tokens, i);
		if (sbuf_len(token->data) == 0) {
			continue;
		}
		if ((sbuf_len(row) + sbuf_len(token->data)) > wrapcol) {
			if (sbuf_len(row) == 0) {
				array_append(arr, token);
				continue;
			} else {
				struct Token *o = xmalloc(sizeof(struct Token));
				memcpy(o, token, sizeof(struct Token));
				o->data = row;
				array_append(arr, o);
				row = sbuf_dupstr(NULL);
				//sbuf_finishx(row);
			}
		}
		if (sbuf_len(row) == 0) {
			row = token->data;
		} else {
			struct sbuf *s = sbuf_dup(row);
			sbuf_putc(s, ' ');
			sbuf_cat(s, sbuf_data(token->data));
			sbuf_finishx(s);
			row = s;
		}
	}
	if (sbuf_len(row) > 0 && array_len(arr) < array_len(tokens)) {
		struct Token *o = xmalloc(sizeof(struct Token));
		memcpy(o, token, sizeof(struct Token));
		o->data = row;
		array_append(arr, o);
	}
	print_newline_array(parser, arr);
	for (size_t i = 0; i < array_len(arr); i++) {
		struct Token *t = array_get(arr, i);
		free(t);
	}
	array_free(arr);
}

void
parser_generate_output_helper(struct Parser *parser, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return;
	}
	struct Token *arr0 = array_get(arr, 0);
	if (!(parser->behavior & PARSER_UNSORTED_VARIABLES) && !leave_unsorted(arr0->var)) {
		array_sort(arr, tokcompare);
	}
	if (print_as_newlines(arr0->var)) {
		print_newline_array(parser, arr);
	} else {
		print_token_array(parser, arr);
	}
	array_truncate(arr);
}

void
parser_generate_output(struct Parser *parser)
{
	struct Array *cond_arr = array_new(sizeof(struct Token *));
	struct Array *target_arr = array_new(sizeof(struct Token *));
	struct Array *variable_arr = array_new(sizeof(struct Token *));
	int after_port_options_mk = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		switch (o->type) {
		case CONDITIONAL_END:
			print_token_array(parser, cond_arr);
			array_truncate(cond_arr);
			break;
		case CONDITIONAL_START:
			array_truncate(cond_arr);
			break;
		case CONDITIONAL_TOKEN:
			array_append(cond_arr, o);
			break;
		case VARIABLE_END:
			parser_generate_output_helper(parser, variable_arr);
			break;
		case VARIABLE_START:
			array_truncate(variable_arr);
			break;
		case VARIABLE_TOKEN:
			array_append(variable_arr, o);
			break;
		case TARGET_COMMAND_END:
			print_token_array(parser, target_arr);
			array_truncate(target_arr);
			break;
		case TARGET_COMMAND_START:
			array_truncate(target_arr);
			break;
		case TARGET_COMMAND_TOKEN:
			array_append(target_arr, o);
			break;
		case TARGET_END:
			break;
		case PORT_OPTIONS_MK:
			after_port_options_mk = 1;
		case COMMENT:
		case PORT_MK:
		case PORT_PRE_MK:
		case TARGET_START:
			parser_generate_output_helper(parser, variable_arr);
			parser_enqueue_output(parser, o->data);
			parser_enqueue_output(parser, sbuf_dupstr("\n"));
			break;
		case EMPTY: {
			struct sbuf *v = variable_tostring(o->var);
			sbuf_putc(v, '\n');
			sbuf_finishx(v);
			parser_enqueue_output(parser, v);
			break;
		} case INLINE_COMMENT:
			parser_enqueue_output(parser, o->data);
			parser_enqueue_output(parser, sbuf_dupstr("\n"));
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
	}
	if (array_len(cond_arr) > 0) {
		print_token_array(parser, cond_arr);
		array_truncate(cond_arr);
	}
	if (array_len(target_arr) > 0) {
		print_token_array(parser, target_arr);
		array_truncate(target_arr);
	}
	parser_generate_output_helper(parser, variable_arr);
	array_free(cond_arr);
	array_free(target_arr);
	array_free(variable_arr);
}

void
parser_dump_tokens(struct Parser *parser)
{
	ssize_t maxvarlen = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		if (o->type == VARIABLE_START && o->var) {
			struct sbuf *var = variable_tostring(o->var);
			maxvarlen = MAX(maxvarlen, sbuf_len(var));
			sbuf_delete(var);
		}
	}

	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		const char *type;
		switch (o->type) {
		case VARIABLE_END:
			type = "variable-end";
			break;
		case VARIABLE_START:
			type = "variable-start";
			break;
		case VARIABLE_TOKEN:
			type = "variable-token";
			break;
		case TARGET_COMMAND_END:
			type = "target-command-end";
			break;
		case TARGET_COMMAND_START:
			type = "target-command-start";
			break;
		case TARGET_COMMAND_TOKEN:
			type = "target-command-token";
			break;
		case TARGET_END:
			type = "target-end";
			break;
		case TARGET_START:
			type = "target-start";
			break;
		case CONDITIONAL_END:
			type = "conditional-end";
			break;
		case CONDITIONAL_START:
			type = "conditional-start";
			break;
		case CONDITIONAL_TOKEN:
			type = "conditional-token";
			break;
		case EMPTY:
			type = "empty";
			break;
		case INLINE_COMMENT:
			type = "inline-comment";
			break;
		case COMMENT:
			type = "comment";
			break;
		case PORT_MK:
			type = "port-mk";
			break;
		case PORT_PRE_MK:
			type = "port-pre-mk";
			break;
		case PORT_OPTIONS_MK:
			type = "port-options-mk";
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
		struct sbuf *var = NULL;
		ssize_t len = maxvarlen;
		if (o->var &&
		    (o->type == VARIABLE_TOKEN ||
		     o->type == VARIABLE_START ||
		     o->type == VARIABLE_END)) {
			var = variable_tostring(o->var);
			len = maxvarlen - sbuf_len(var);
		} else if (o->cond &&
			   (o->type == CONDITIONAL_END ||
			    o->type == CONDITIONAL_START ||
			    o->type == CONDITIONAL_TOKEN)) {
			var = conditional_tostring(o->cond);
			len = maxvarlen - sbuf_len(var);
		} else if (o->target &&
			   (o->type == TARGET_COMMAND_END ||
			    o->type == TARGET_COMMAND_START ||
			    o->type == TARGET_COMMAND_TOKEN ||
			    o->type == TARGET_START ||
			    o->type == TARGET_END)) {
			var = sbuf_dup(target_name(o->target));
			len = maxvarlen - sbuf_len(var);
		} else {
			len = maxvarlen - 1;
		}
		struct sbuf *range = range_tostring(&o->lines);
		printf("%-20s %8s %s", type, sbuf_data(range), var ? sbuf_data(var) : "-");
		sbuf_delete(range);
		for (ssize_t j = 0; j < len; j++) {
			putchar(' ');
		}
		printf(" %s\n", o->data ? sbuf_data(o->data) : "-");
		if (var) {
			sbuf_delete(var);
		}
	}
}

void
parser_read(struct Parser *parser, char *line)
{
	size_t linelen = strlen(line);
	struct sbuf *buf = sbuf_dupstr(line);
	sbuf_finishx(buf);

	parser->lines.end++;

	int will_continue = matches(RE_CONTINUE_LINE, buf, NULL);
	if (will_continue) {
		line[linelen - 1] = 0;
	}

	if (parser->continued) {
		/* Replace all whitespace at the beginning with a single
		 * space which is what make seems to do.
		*/
		for (;isblank(*line); line++);
		if (strlen(line) < 1) {
			sbuf_putc(parser->inbuf, ' ');
		}
	}

	sbuf_cat(parser->inbuf, line);

	if (!will_continue) {
		sbuf_trim(parser->inbuf);
		sbuf_finishx(parser->inbuf);
		parser_read_internal(parser, parser->inbuf);
		parser->lines.start = parser->lines.end;
		sbuf_delete(parser->inbuf);
		parser->inbuf = sbuf_dupstr(NULL);
	}

	parser->continued = will_continue;
	sbuf_delete(buf);
}

void
parser_read_internal(struct Parser *parser, struct sbuf *buf)
{
	ssize_t pos;

	pos = consume_comment(buf);
	if (pos > 0) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	} else if (matches(RE_EMPTY_LINE, buf, NULL)) {
		if (parser->in_target) {
			parser_append_token(parser, TARGET_END, NULL);
			parser_append_token(parser, COMMENT, buf);
			parser->in_target = 0;
			goto next;
		} else {
			parser_append_token(parser, COMMENT, buf);
			goto next;
		}
	}

	if (parser->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			if (parser->condname) {
				sbuf_delete(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = sbuf_substr_dup(buf, 0, pos);
			sbuf_trim(parser->condname);
			sbuf_finishx(parser->condname);

			parser_append_token(parser, CONDITIONAL_START, NULL);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, NULL);
			goto next;
		}
		pos = consume_var(buf);
		if (pos == 0) {
			parser_append_token(parser, TARGET_COMMAND_START, NULL);
			parser_tokenize(parser, buf, TARGET_COMMAND_TOKEN, pos);
			parser_append_token(parser, TARGET_COMMAND_END, NULL);
			goto next;
		}
		parser_append_token(parser, TARGET_END, NULL);
		parser->in_target = 0;
	}

	pos = consume_target(buf);
	if (pos > 0) {
		parser->in_target = 1;
		if (parser->targetname) {
			sbuf_delete(parser->targetname);
			parser->targetname = NULL;
		}
		parser->targetname = sbuf_dup(buf);
		sbuf_finishx(parser->targetname);
		parser_append_token(parser, TARGET_START, buf);
		goto next;
	}

	pos = consume_conditional(buf);
	if (pos > 0) {
		if (sbuf_endswith(buf, "<bsd.port.options.mk>")) {
			parser_append_token(parser, PORT_OPTIONS_MK, buf);
			goto next;
		} else if (sbuf_endswith(buf, "<bsd.port.pre.mk>")) {
			parser_append_token(parser, PORT_PRE_MK, buf);
			goto next;
		} else if (sbuf_endswith(buf, "<bsd.port.post.mk>") ||
			   sbuf_endswith(buf, "<bsd.port.mk>")) {
			parser_append_token(parser, PORT_MK, buf);
			goto next;
		} else {
			if (parser->condname) {
				sbuf_delete(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = sbuf_substr_dup(buf, 0, pos);
			sbuf_trim(parser->condname);
			sbuf_finishx(parser->condname);

			parser_append_token(parser, CONDITIONAL_START, NULL);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, NULL);
		}
		goto next;
	}

	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > sbuf_len(buf)) {
			errx(1, "parser->varname too small");
		}
		parser->varname = sbuf_substr_dup(buf, 0, pos);
		sbuf_finishx(parser->varname);
		parser_append_token(parser, VARIABLE_START, NULL);
	}
	parser_tokenize(parser, buf, VARIABLE_TOKEN, pos);
	if (parser->varname == NULL) {
		errx(1, "parser error on line %s", sbuf_data(range_tostring(&parser->lines)));
	}
next:
	if (parser->varname) {
		parser_append_token(parser, VARIABLE_END, NULL);
		sbuf_delete(parser->varname);
		parser->varname = NULL;
	}
}

void
parser_read_finish(struct Parser *parser)
{
	parser->lines.end++;

	if (sbuf_len(parser->inbuf) > 0) {
		sbuf_trim(parser->inbuf);
		sbuf_finishx(parser->inbuf);
		parser_read_internal(parser, parser->inbuf);
	}

	if (parser->in_target) {
		parser_append_token(parser, TARGET_END, NULL);
	}

	if (parser->behavior & PARSER_COLLAPSE_ADJACENT_VARIABLES) {
		parser_collapse_adjacent_variables(parser);
	}

	if (parser->behavior & PARSER_SANITIZE_APPEND) {
		parser_sanitize_append_modifier(parser);
	}
}

void
parser_collapse_adjacent_variables(struct Parser *parser)
{
	struct Variable *last_var = NULL;
	struct Token *last = NULL;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		switch (o->type) {
		case VARIABLE_START:
			if (last_var != NULL && variable_cmp(o->var, last_var) == 0) {
				o->ignore = 1;
				if (last) {
					last->ignore = 1;
					last = NULL;
				}
			}
			break;
		case VARIABLE_END:
			last = o;
			break;
		default:
			last_var = o->var;
			break;
		}
	}
}

void
parser_sanitize_append_modifier(struct Parser *parser)
{
	/* Sanitize += before bsd.options.mk */
	ssize_t start = -1;
	struct Array *seen = array_new(sizeof(struct Variable *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (t->ignore) {
			continue;
		}
		switch(t->type) {
		case VARIABLE_START:
			start = i;
			break;
		case VARIABLE_END: {
			if (start < 0) {
				continue;
			}
			int found = 0;
			for (size_t j = 0; j < array_len(seen); j++) {
				struct Variable *var = array_get(seen, j);
				if (variable_cmp(t->var, var) == 0) {
					found = 1;
					break;
				}
			}
			if (found) {
				start = -1;
				continue;
			} else {
				array_append(seen, t->var);
			}
			for (size_t j = start; j <= i; j++) {
				struct Token *o = array_get(parser->tokens, j);
				if (sbuf_strcmp(variable_name(o->var), "CXXFLAGS") != 0 &&
				    sbuf_strcmp(variable_name(o->var), "CFLAGS") != 0 &&
				    sbuf_strcmp(variable_name(o->var), "LDFLAGS") != 0 &&
				    variable_modifier(o->var) == MODIFIER_APPEND) {
					variable_set_modifier(o->var, MODIFIER_ASSIGN);
				}
			}
			start = -1;
			break;
		} case PORT_OPTIONS_MK:
		case PORT_PRE_MK:
		case PORT_MK:
			goto end;
		default:
			break;
		}
	}
end:
	array_free(seen);
}

void
parser_write(struct Parser *parser, int fd)
{
	size_t len = array_len(parser->result);
	if (len == 0) {
		return;
	}

	size_t iov_len = MIN(len, IOV_MAX);
	struct iovec *iov = reallocarray(NULL, iov_len, sizeof(struct iovec));
	if (iov == NULL) {
		err(1, "reallocarray");
	}

	for (size_t i = 0; i < len;) {
		size_t j = 0;
		for (; i < len && j < iov_len; j++) {
			struct sbuf *s = array_get(parser->result, i++);
			iov[j].iov_base = sbuf_data(s);
			iov[j].iov_len = sbuf_len(s);
		}
		if (writev(fd, iov, j) < 0) {
			err(1, "writev");
		}
	}

	/* Collect garbage */
	for (size_t i = 0; i < array_len(parser->result); i++) {
		sbuf_delete((struct sbuf *)array_get(parser->result, i));
	}
	array_truncate(parser->result);
}

void
usage()
{
	fprintf(stderr, "usage: portfmt [-a] [-i] [-u] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	enum ParserBehavior behavior = PARSER_COLLAPSE_ADJACENT_VARIABLES;
	int fd_in = STDIN_FILENO;
	int fd_out = STDOUT_FILENO;
	int dflag = 0;
	int iflag = 0;
	while (getopt(argc, argv, "adiuw:") != -1) {
		switch (optopt) {
		case 'a':
			behavior |= PARSER_SANITIZE_APPEND;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'u':
			behavior |= PARSER_UNSORTED_VARIABLES;
			break;
		case 'w': {
			const char *errstr = NULL;
			WRAPCOL = strtonum(optarg, -1, INT_MAX, &errstr);
			if (errstr != NULL) {
				errx(1, "strtonum: %s", errstr);
			}
			break;
		} default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (dflag) {
		iflag = 0;
	}

	if (argc > 1) {
		usage();
	} else if (argc == 1) {
		fd_in = open(argv[0], iflag ? O_RDWR : O_RDONLY);
		if (fd_in < 0) {
			err(1, "open");
		}
		if (iflag) {
			fd_out = fd_in;
		}
	}

#if HAVE_CAPSICUM
	if (iflag) {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		if (caph_limit_stream(fd_in, CAPH_READ | CAPH_WRITE | CAPH_FTRUNCATE) < 0) {
			err(1, "caph_limit_stream");
		}
		if (caph_limit_stderr() < 0) {
			err(1, "caph_limit_stderr");
		}
	} else {
		if (caph_limit_stdio() < 0) {
			err(1, "caph_limit_stdio");
		}
	}

	if (caph_enter() < 0) {
		err(1, "caph_enter");
	}
#endif

	compile_regular_expressions();

	struct Parser *parser = parser_new(behavior);
	if (parser == NULL) {
		err(1, "calloc");
	}

	ssize_t linelen;
	size_t linecap = 0;
	char *line = NULL;
	FILE *fp = fdopen(fd_in, "r");
	if (fp == NULL) {
		err(1, "fdopen");
	}
	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		line[linelen - 1] = 0;
		parser_read(parser, line);
	}
	parser_read_finish(parser);

	if (dflag) {
		parser_dump_tokens(parser);
	} else {
		parser_find_goalcols(parser);
		parser_generate_output(parser);

		if (iflag) {
			if (lseek(fd_out, 0, SEEK_SET) < 0) {
				err(1, "lseek");
			}
			if (ftruncate(fd_out, 0) < 0) {
				err(1, "ftruncate");
			}
		}
		parser_write(parser, fd_out);
	}

	close(fd_out);
	close(fd_in);

	free(line);
	parser_free(parser);

	return 0;
}
