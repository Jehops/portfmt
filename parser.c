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
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
#include "conditional.h"
#include "parser.h"
#include "rules.h"
#include "target.h"
#include "util.h"
#include "variable.h"

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
	char *data;
	struct Conditional *cond;
	struct Variable *var;
	struct Target *target;
	int goalcol;
	struct Range lines;
	int ignore;
};

struct Parser {
	struct ParserSettings settings;
	int continued;
	int in_target;
	struct Range lines;
	int skip;
	char *inbuf;
	char *condname;
	char *targetname;
	char *varname;

	struct Array *tokens;
	struct Array *result;
	struct Array *rawlines;
};

#define INBUF_SIZE 65536

static size_t consume_comment(const char *);
static size_t consume_conditional(const char *);
static size_t consume_target(const char *);
static size_t consume_token(struct Parser *, const char *, size_t, char, char, int);
static size_t consume_var(const char *);
static void parser_append_token(struct Parser *, enum TokenType, const char *);
static void parser_collapse_adjacent_variables(struct Parser *);
static void parser_enqueue_output(struct Parser *, const char *);
static void parser_find_goalcols(struct Parser *);
static void parser_output_dump_tokens(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct Range *);
static void parser_output_print_target_command(struct Parser *, struct Array *);
static void parser_output_reformatted_helper(struct Parser *, struct Array *);
static void parser_output_reformatted(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read_internal(struct Parser *, const char *);
static void parser_sanitize_append_modifier(struct Parser *);
static void parser_tokenize(struct Parser *, const char *, enum TokenType, size_t);
static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
static char *range_tostring(struct Range *);
static int tokcompare(const void *, const void *);
static struct Token *parser_get_token(struct Parser *, size_t);

size_t
consume_comment(const char *buf)
{
	size_t pos = 0;
	if (str_startswith(buf, "#")) {
		pos = strlen(buf);
	}
	return pos;
}

size_t
consume_conditional(const char *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_CONDITIONAL, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

size_t
consume_target(const char *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_TARGET, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

size_t
consume_token(struct Parser *parser, const char *line, size_t pos,
	      char startchar, char endchar, int eol_ok)
{
	int counter = 0;
	int escape = 0;
	size_t i = pos;
	for (; i < strlen(line); i++) {
		char c = line[i];
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
		errx(1, "tokenizer: %s: expected %c", range_tostring(&parser->lines), endchar);
	} else {
		return i;
	}
}

size_t
consume_var(const char *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_VAR, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}
	return pos;
}

char *
range_tostring(struct Range *range)
{
	assert(range);
	assert(range->start < range->end);

	char *s;
	if (range->start == range->end - 1) {
		if (asprintf(&s, "%zu", range->start) < 0) {
			warn("asprintf");
			abort();
		}
	} else {
		if (asprintf(&s, "%zu-%zu", range->start, range->end - 1) < 0) {
			warn("asprintf");
			abort();
		}
	}

	return s;
}

void
parser_init_settings(struct ParserSettings *settings)
{
	settings->behavior = 0;
	settings->target_command_format_threshold = 8;
	settings->target_command_format_wrapcol = 65;
	settings->wrapcol = 80;
}

struct Parser *
parser_new(struct ParserSettings *settings)
{
	struct Parser *parser = xmalloc(sizeof(struct Parser));

	parser->rawlines = array_new(sizeof(char *));
	parser->result = array_new(sizeof(char *));
	parser->tokens = array_new(sizeof(struct Token *));
	parser->lines.start = 1;
	parser->lines.end = 1;
	parser->inbuf = xmalloc(INBUF_SIZE);
	parser->settings = *settings;

	compile_regular_expressions();

	return parser;
}

void
parser_free(struct Parser *parser)
{
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (t->data) {
			free(t->data);
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
		free(array_get(parser->result, i));
	}
	array_free(parser->result);

	for (size_t i = 0; i < array_len(parser->rawlines); i++) {
		free(array_get(parser->rawlines, i));
	}
	array_free(parser->rawlines);

	free(parser->inbuf);
	free(parser);
}

void
parser_append_token(struct Parser *parser, enum TokenType type, const char *v)
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

	char *data = NULL;
	if (v) {
		data = xstrdup(v);
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
parser_enqueue_output(struct Parser *parser, const char *s)
{
	array_append(parser->result, xstrdup(s));
}

struct Token *
parser_get_token(struct Parser *parser, size_t i)
{
	return array_get(parser->tokens, i);
}

void
parser_tokenize(struct Parser *parser, const char *line, enum TokenType type, size_t start)
{
	int dollar = 0;
	int escape = 0;
	char *token = NULL;
	size_t i = start;
	size_t queued_tokens = 0;
	for (; i < strlen(line); i++) {
		assert(i >= start);
		char c = line[i];
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
				fprintf(stderr, "%s\n", line);
				errx(1, "tokenizer: %s: expected {", range_tostring(&parser->lines));
			}
		} else {
			if (c == ' ' || c == '\t') {
				char *tmp = str_substr_dup(line, start, i);
				token = str_strip_dup(tmp);
				free(tmp);
				if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
					parser_append_token(parser, type, token);
					queued_tokens++;
				}
				free(token);
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
				char *tmp = str_substr_dup(line, i, strlen(line));
				token = str_strip_dup(tmp);
				free(tmp);
				if (strcmp(token, "#") == 0 ||
				    strcmp(token, "# empty") == 0 ||
				    strcmp(token, "#none") == 0 ||
				    strcmp(token, "# none") == 0) {
					parser_append_token(parser, type, token);
					queued_tokens++;
				} else {
					parser_append_token(parser, INLINE_COMMENT, token);
				}

				free(token);
				token = NULL;
				goto cleanup;
			}
		}
	}
	char *tmp = str_substr_dup(line, start, i);
	token = str_strip_dup(tmp);
	free(tmp);
	if (strcmp(token, "") != 0) {
		parser_append_token(parser, type, token);
		queued_tokens++;
	}

	free(token);
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
	size_t last = 0;
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
			if (str_startswith(o->data, "#")) {
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
	char sep[512] = {};
	struct Token *o = array_get(arr, 0);
	assert(o && o->data != NULL);
	assert(strlen(o->data) != 0);
	switch (o->type) {
	case VARIABLE_TOKEN: {
		char *start = variable_tostring(o->var);
		size_t ntabs = ceil((MAX(16, o->goalcol) - strlen(start)) / 8.0);
		xstrlcpy(sep, start, sizeof(sep));
		xstrlcat(sep, repeat('\t', ntabs), sizeof(sep));
		break;
	} case CONDITIONAL_TOKEN:
		sep[0] = 0;
		break;
	case TARGET_COMMAND_TOKEN:
		sep[0] = '\t';
		sep[1] = 0;
		break;
	default:
		errx(1, "unhandled token type: %i", o->type);
	}

	const char *end = " \\\n";
	for (size_t i = 0; i < array_len(arr); i++) {
		struct Token *o = array_get(arr, i);
		char *line = o->data;
		if (!line || strlen(line) == 0) {
			continue;
		}
		if (i == array_len(arr) - 1) {
			end = "\n";
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		parser_enqueue_output(parser, end);
		switch (o->type) {
		case VARIABLE_TOKEN:
			if (i == 0) {
				size_t ntabs = ceil(MAX(16, o->goalcol) / 8.0);
				xstrlcpy(sep, repeat('\t', ntabs), sizeof(sep));
			}
			break;
		case CONDITIONAL_TOKEN:
			sep[0] = '\t';
			sep[1] = 0;
			break;
		case TARGET_COMMAND_TOKEN:
			xstrlcpy(sep, repeat('\t', 2), sizeof(sep));
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
	return strcasecmp(ao->data, bo->data);
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
	size_t wrapcol;
	if (o->var && ignore_wrap_col(o->var)) {
		wrapcol = 99999999;
	} else {
		/* Minus ' \' at end of line */
		wrapcol = parser->settings.wrapcol - o->goalcol - 2;
	}

	char row[1024] = {};
	struct Token *token = NULL;
	struct Array *gc = array_new(sizeof(void *));
	for (size_t i = 0; i < array_len(tokens); i++) {
		token = array_get(tokens, i);
		if (strlen(token->data) == 0) {
			continue;
		}
		if ((strlen(row) + strlen(token->data)) > wrapcol) {
			if (strlen(row) == 0) {
				array_append(arr, token);
				continue;
			} else {
				struct Token *o = xmalloc(sizeof(struct Token));
				array_append(gc, o);
				memcpy(o, token, sizeof(struct Token));
				o->data = xstrdup(row);
				array_append(gc, o->data);
				array_append(arr, o);
				row[0] = 0;
			}
		}
		if (strlen(row) == 0) {
			xstrlcpy(row, token->data, sizeof(row));
		} else {
			xstrlcat(row, " ", sizeof(row));
			xstrlcat(row, token->data, sizeof(row));
		}
	}
	if (token && strlen(row) > 0 && array_len(arr) < array_len(tokens)) {
		struct Token *o = xmalloc(sizeof(struct Token));
		array_append(gc, o);
		memcpy(o, token, sizeof(struct Token));
		o->data = xstrdup(row);
		array_append(gc, o->data);
		array_append(arr, o);
	}
	print_newline_array(parser, arr);

	array_free(arr);
	for (size_t i = 0; i < array_len(gc); i++) {
		free(array_get(gc, i));
	}
	array_free(gc);
}

void
parser_output_print_rawlines(struct Parser *parser, struct Range *lines)
{
	for (size_t i = lines->start; i < lines->end; i++) {
		parser_enqueue_output(parser, array_get(parser->rawlines, i - 1));
		parser_enqueue_output(parser, "\n");
	}
}

void
parser_output_print_target_command(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) == 0) {
		return;
	}

	const char *endline = "\n";
	const char *endnext = " \\\n";
	const char *endword = " ";
	const char *startlv0 = "";
	const char *startlv1 = "\t";
	const char *startlv2 = "\t\t";
	const char *start = startlv0;

	/* Find the places we need to wrap to the next line.
	 * TODO: This is broken as wrapping changes the next place we need to wrap
	 */
	struct Array *wraps = array_new(sizeof(int));
	size_t column = 8;
	int complexity = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		char *word = t->data;

		assert(t->type == TARGET_COMMAND_TOKEN);
		assert(strlen(word) != 0);

		for (char *c = word; *c != 0; c++) {
			switch (*c) {
			case '`':
			case '(':
			case ')':
			case '[':
			case ']':
			case ';':
				complexity++;
				break;
			}
		}

		if (start == startlv1 || start == startlv2) {
			start = startlv0;
		}

		column += strlen(start) * 8 + strlen(word);
		if (column > parser->settings.target_command_format_wrapcol ||
		    target_command_should_wrap(word)) {
			if (i + 1 < array_len(tokens)) {
				struct Token *next = array_get(tokens, i + 1);
				if (target_command_should_wrap(next->data)) {
					continue;
				}
			}
			start = startlv2;
			column = 16;
			array_append(wraps, (void*)i);
		}
	}

	if (!(parser->settings.behavior & PARSER_FORMAT_TARGET_COMMANDS) ||
	    complexity > parser->settings.target_command_format_threshold) {
		struct Token *t = array_get(tokens, 0);
		parser_output_print_rawlines(parser, &t->lines);
		goto cleanup;
	}

	parser_enqueue_output(parser, startlv1);
	int wrapped = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		char *word = t->data;

		if (wrapped) {
			if (i == 0) {
				parser_enqueue_output(parser, startlv1);
			} else {
				parser_enqueue_output(parser, startlv2);
			}
		}
		wrapped = array_find(wraps, (void*)i, NULL) > -1;

		parser_enqueue_output(parser, word);
		if (wrapped) {
			if (i == array_len(tokens) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				parser_enqueue_output(parser, endnext);
			}
		} else {
			if (i == array_len(tokens) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				parser_enqueue_output(parser, endword);
			}
		}
	}

cleanup:
	array_free(wraps);
}

void
parser_output_prepare(struct Parser *parser)
{
	if (parser->settings.behavior & PARSER_DUMP_TOKENS) {
		parser_output_dump_tokens(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_REFORMAT) {
		parser_output_reformatted(parser);
	}
}

void
parser_output_reformatted_helper(struct Parser *parser, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return;
	}
	struct Token *arr0 = array_get(arr, 0);
	if (!(parser->settings.behavior & PARSER_UNSORTED_VARIABLES) && !leave_unsorted(arr0->var)) {
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
parser_output_reformatted(struct Parser *parser)
{
	parser_find_goalcols(parser);

	struct Array *target_arr = array_new(sizeof(struct Token *));
	struct Array *variable_arr = array_new(sizeof(struct Token *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		switch (o->type) {
		case CONDITIONAL_END:
			parser_output_print_rawlines(parser, &o->lines);
			break;
		case CONDITIONAL_START:
		case CONDITIONAL_TOKEN:
			break;
		case VARIABLE_END:
			parser_output_reformatted_helper(parser, variable_arr);
			break;
		case VARIABLE_START:
			array_truncate(variable_arr);
			break;
		case VARIABLE_TOKEN:
			array_append(variable_arr, o);
			break;
		case TARGET_COMMAND_END:
			parser_output_print_target_command(parser, target_arr);
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
		case COMMENT:
		case PORT_OPTIONS_MK:
		case PORT_MK:
		case PORT_PRE_MK:
		case TARGET_START:
			parser_output_reformatted_helper(parser, variable_arr);
			parser_output_print_rawlines(parser, &o->lines);
			break;
		case EMPTY: {
			char *v = variable_tostring(o->var);
			parser_enqueue_output(parser, v);
			free(v);
			parser_enqueue_output(parser, "\n");
			break;
		} case INLINE_COMMENT:
			parser_enqueue_output(parser, o->data);
			parser_enqueue_output(parser, "\n");
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
	}
	if (array_len(target_arr) > 0) {
		print_token_array(parser, target_arr);
		array_truncate(target_arr);
	}
	parser_output_reformatted_helper(parser, variable_arr);
	array_free(target_arr);
	array_free(variable_arr);
}

void
parser_output_dump_tokens(struct Parser *parser)
{
	size_t maxvarlen = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->ignore) {
			continue;
		}
		if (o->type == VARIABLE_START && o->var) {
			char *var = variable_tostring(o->var);
			maxvarlen = MAX(maxvarlen, strlen(var));
			free(var);
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
		char *var = NULL;
		ssize_t len = maxvarlen;
		if (o->var &&
		    (o->type == VARIABLE_TOKEN ||
		     o->type == VARIABLE_START ||
		     o->type == VARIABLE_END)) {
			var = variable_tostring(o->var);
			len = maxvarlen - strlen(var);
		} else if (o->cond &&
			   (o->type == CONDITIONAL_END ||
			    o->type == CONDITIONAL_START ||
			    o->type == CONDITIONAL_TOKEN)) {
			var = conditional_tostring(o->cond);
			len = maxvarlen - strlen(var);
		} else if (o->target &&
			   (o->type == TARGET_COMMAND_END ||
			    o->type == TARGET_COMMAND_START ||
			    o->type == TARGET_COMMAND_TOKEN ||
			    o->type == TARGET_START ||
			    o->type == TARGET_END)) {
			var = xstrdup(target_name(o->target));
			len = maxvarlen - strlen(var);
		} else {
			len = maxvarlen - 1;
		}
		char *range = range_tostring(&o->lines);
		char *buf;
		if (asprintf(&buf, "%-20s %8s ", type, range) < 0) {
			warn("asprintf");
			abort();
		}
		free(range);
		parser_enqueue_output(parser, buf);
		free(buf);

		if (var) {
			parser_enqueue_output(parser, var);
			free(var);
		} else {
			parser_enqueue_output(parser, "-");
		}

		if (len > 0) {
			buf = xmalloc(len + 1);
			memset(buf, ' ', len);
			parser_enqueue_output(parser, buf);
			free(buf);
		}
		parser_enqueue_output(parser, " ");

		if (o->data) {
			parser_enqueue_output(parser, o->data);
		} else {
			parser_enqueue_output(parser, "-");
		}
		parser_enqueue_output(parser, "\n");
	}
}

void
parser_read(struct Parser *parser, char *line)
{
	size_t linelen = strlen(line);

	array_append(parser->rawlines, xstrdup(line));

	parser->lines.end++;

	int will_continue = matches(RE_CONTINUE_LINE, line, NULL);
	if (will_continue) {
		line[linelen - 1] = 0;
	}

	if (parser->continued) {
		/* Replace all whitespace at the beginning with a single
		 * space which is what make seems to do.
		*/
		for (;isblank(*line); line++);
		if (strlen(line) < 1) {
			xstrlcat(parser->inbuf, " ", INBUF_SIZE);
		}
	}

	xstrlcat(parser->inbuf, line, INBUF_SIZE);

	if (!will_continue) {
		parser_read_internal(parser, str_trim(parser->inbuf));
		parser->lines.start = parser->lines.end;
		memset(parser->inbuf, 0, INBUF_SIZE);
	}

	parser->continued = will_continue;
}

void
parser_read_internal(struct Parser *parser, const char *buf)
{
	size_t pos;

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
				free(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = str_trim(str_substr_dup(buf, 0, pos));

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
			free(parser->targetname);
			parser->targetname = NULL;
		}
		parser->targetname = xstrdup(buf);
		parser_append_token(parser, TARGET_START, buf);
		goto next;
	}

	pos = consume_conditional(buf);
	if (pos > 0) {
		if (str_endswith(buf, "<bsd.port.options.mk>")) {
			parser_append_token(parser, PORT_OPTIONS_MK, buf);
			goto next;
		} else if (str_endswith(buf, "<bsd.port.pre.mk>")) {
			parser_append_token(parser, PORT_PRE_MK, buf);
			goto next;
		} else if (str_endswith(buf, "<bsd.port.post.mk>") ||
			   str_endswith(buf, "<bsd.port.mk>")) {
			parser_append_token(parser, PORT_MK, buf);
			goto next;
		} else {
			if (parser->condname) {
				free(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = str_trim(str_substr_dup(buf, 0, pos));

			parser_append_token(parser, CONDITIONAL_START, NULL);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, NULL);
		}
		goto next;
	}

	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > strlen(buf)) {
			errx(1, "parser->varname too small");
		}
		parser->varname = str_substr_dup(buf, 0, pos);
		parser_append_token(parser, VARIABLE_START, NULL);
	}
	parser_tokenize(parser, buf, VARIABLE_TOKEN, pos);
	if (parser->varname == NULL) {
		errx(1, "parser error on line %s", range_tostring(&parser->lines));
	}
next:
	if (parser->varname) {
		parser_append_token(parser, VARIABLE_END, NULL);
		free(parser->varname);
		parser->varname = NULL;
	}
}

void
parser_read_finish(struct Parser *parser)
{
	parser->lines.end++;

	if (strlen(parser->inbuf) > 0) {
		parser_read_internal(parser, str_trim(parser->inbuf));
	}

	if (parser->in_target) {
		parser_append_token(parser, TARGET_END, NULL);
	}

	if (parser->settings.behavior & PARSER_COLLAPSE_ADJACENT_VARIABLES) {
		parser_collapse_adjacent_variables(parser);
	}

	if (parser->settings.behavior & PARSER_SANITIZE_APPEND) {
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
			if (last_var != NULL && variable_cmp(o->var, last_var) == 0 &&
			    variable_modifier(last_var) != MODIFIER_EXPAND &&
			    variable_modifier(o->var) != MODIFIER_EXPAND) {
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
				if (strcmp(variable_name(o->var), "CXXFLAGS") != 0 &&
				    strcmp(variable_name(o->var), "CFLAGS") != 0 &&
				    strcmp(variable_name(o->var), "LDFLAGS") != 0 &&
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
parser_output_write(struct Parser *parser, int fd)
{
	size_t len = array_len(parser->result);
	if (len == 0) {
		return;
	}

	size_t iov_len = MIN(len, IOV_MAX);
	struct iovec *iov = recallocarray(NULL, 0, iov_len, sizeof(struct iovec));
	if (iov == NULL) {
		warn("recallocarray");
		abort();
	}

	for (size_t i = 0; i < len;) {
		size_t j = 0;
		for (; i < len && j < iov_len; j++) {
			char *s = array_get(parser->result, i++);
			iov[j].iov_base = s;
			iov[j].iov_len = strlen(s);
		}
		if (writev(fd, iov, j) < 0) {
			err(1, "writev");
		}
	}

	/* Collect garbage */
	for (size_t i = 0; i < array_len(parser->result); i++) {
		free(array_get(parser->result, i));
	}
	array_truncate(parser->result);
	free(iov);
}

char *
parser_lookup_variable(struct Parser *parser, const char *name)
{
	struct Array *tokens = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (t->type) {
		case VARIABLE_START:
			array_truncate(tokens);
			break;
		case VARIABLE_TOKEN:
			if (strcmp(variable_name(t->var), name) == 0) {
				if (t->data) {
					array_append(tokens, t->data);
				}
			}
			break;
		case VARIABLE_END:
			if (strcmp(variable_name(t->var), name) == 0) {
				goto found;
			}
			break;
		default:
			break;
		}
	}

	array_free(tokens);
	return NULL;

	size_t sz;
found:
	sz = array_len(tokens) + 1;
	for (size_t i = 0; i < array_len(tokens); i++) {
		char *s = array_get(tokens, i);
		sz += strlen(s);
	}

	char *buf = xmalloc(sz);
	for (size_t i = 0; i < array_len(tokens); i++) {
		char *s = array_get(tokens, i);
		xstrlcat(buf, s, sz);
		if (i != array_len(tokens) - 1) {
			xstrlcat(buf, " ", sz);
		}
	}

	array_free(tokens);
	return buf;
}
