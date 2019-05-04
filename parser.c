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
#include "token.h"
#include "util.h"
#include "variable.h"

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

	struct Array *edited; /* unowned struct Token * */
	struct Array *tokengc;
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
static void parser_output_edited(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct Range *);
static void parser_output_print_target_command(struct Parser *, struct Array *);
static struct Array *parser_output_sort_opt_use(struct Parser *, struct Array *);
static struct Array *parser_output_reformatted_helper(struct Parser *, struct Array *);
static void parser_output_reformatted(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read_internal(struct Parser *, const char *);
static void parser_sanitize_append_modifier(struct Parser *);
static void parser_tokenize(struct Parser *, const char *, enum TokenType, size_t);
static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
static char *range_tostring(struct Range *);
static int tokcompare(const void *, const void *);
static int parser_has_variable(struct Parser *, const char *);

size_t
consume_comment(const char *buf)
{
	for (const char *bufp = buf; *bufp != 0; bufp++) {
		if (*bufp == '#') {
			return strlen(buf);
		} else if (!isspace(*bufp)) {
			break;
		}
	}
	return 0;
}

size_t
consume_conditional(const char *buf)
{
	size_t pos = 0;
	regmatch_t match[1];
	if (matches(RE_CONDITIONAL, buf, match)) {
		pos = match->rm_eo - match->rm_so;
	}

	if(pos > 0 && (buf[pos - 1] == '(' || buf[pos - 1] == '!')) {
		pos--;
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
		errx(1, "%s: expected %c", range_tostring(&parser->lines), endchar);
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
	settings->behavior = PARSER_DEFAULT;
	settings->target_command_format_threshold = 8;
	settings->target_command_format_wrapcol = 65;
	settings->wrapcol = 80;
}

struct Parser *
parser_new(struct ParserSettings *settings)
{
	struct Parser *parser = xmalloc(sizeof(struct Parser));

	parser->edited = array_new(sizeof(void *));
	parser->tokengc = array_new(sizeof(void *));
	parser->rawlines = array_new(sizeof(char *));
	parser->result = array_new(sizeof(char *));
	parser->tokens = array_new(sizeof(struct Token *));
	parser->lines.start = 1;
	parser->lines.end = 1;
	parser->inbuf = xmalloc(INBUF_SIZE);
	parser->settings = *settings;

	compile_regular_expressions();

	if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser->settings.behavior &= ~PARSER_COLLAPSE_ADJACENT_VARIABLES;
	}

	return parser;
}

void
parser_free(struct Parser *parser)
{
	array_free(parser->edited);

	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		token_free(t);
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

	for (size_t i = 0; i < array_len(parser->tokengc); i++) {
		struct Token *t = array_get(parser->tokengc, i);
		token_free(t);
	}
	array_free(parser->tokengc);

	free(parser->inbuf);
	free(parser);
}

void
parser_append_token(struct Parser *parser, enum TokenType type, const char *data)
{
	struct Token *t = token_new(type, &parser->lines, data, parser->varname,
				    parser->condname, parser->targetname);
	array_append(parser->tokens, t);
}

void
parser_enqueue_output(struct Parser *parser, const char *s)
{
	array_append(parser->result, xstrdup(s));
}

void
parser_tokenize(struct Parser *parser, const char *line, enum TokenType type, size_t start)
{
	int dollar = 0;
	int escape = 0;
	char *token = NULL;
	size_t i = start;
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
			} else if (c == '(') {
				i = consume_token(parser, line, i, '(', ')', 0);
				dollar = 0;
			} else if (isalnum(c) || c == '@' || c == '<' || c == '>') {
				dollar = 0;
			} else if (c == 1) {
				dollar = 0;
			} else if (c == '$') {
				dollar++;
			} else {
				errx(1, "%s: unterminated $", range_tostring(&parser->lines));
			}
		} else {
			if (c == ' ' || c == '\t') {
				char *tmp = str_substr_dup(line, start, i);
				token = str_strip_dup(tmp);
				free(tmp);
				if (strcmp(token, "") != 0 && strcmp(token, "\\") != 0) {
					parser_append_token(parser, type, token);
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
				char *tmp = str_substr_dup(line, i, strlen(line));
				token = str_strip_dup(tmp);
				free(tmp);
				parser_append_token(parser, type, token);

				free(token);
				token = NULL;
				return;
			}
		}
	}
	char *tmp = str_substr_dup(line, start, i);
	token = str_strip_dup(tmp);
	free(tmp);
	if (strcmp(token, "") != 0) {
		parser_append_token(parser, type, token);
	}

	free(token);
	token = NULL;
}

void
parser_propagate_goalcol(struct Parser *parser, size_t start, size_t end,
			 int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		struct Token *t = array_get(parser->tokens, k);
		if (token_variable(t) && !skip_goalcol(token_variable(t))) {
			token_set_goalcol(t, moving_goalcol);
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
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_END:
		case VARIABLE_START:
			break;
		case VARIABLE_TOKEN:
			if (tokens_start == -1) {
				tokens_start = i;
			}
			tokens_end = i;

			struct Variable *var = token_variable(t);
			if (var && skip_goalcol(var)) {
				token_set_goalcol(t, indent_goalcol(var));
			} else {
				moving_goalcol = MAX(indent_goalcol(var), moving_goalcol);
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
			/* Ignore comments in between variables and
			 * treat variables after them as part of the
			 * same block, i.e., indent them the same way.
			 */
			if (str_startswith(token_data(t), "#")) {
				continue;
			}
			if (tokens_start != -1) {
				parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
				moving_goalcol = 0;
				last = i;
				tokens_start = -1;
			}
			break;
		default:
			errx(1, "Unhandled token type: %i", token_type(t));
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
	assert(o && token_data(o) != NULL);
	assert(strlen(token_data(o)) != 0);
	assert(token_type(o) == VARIABLE_TOKEN);

	char *start = variable_tostring(token_variable(o));
	size_t ntabs = ceil((MAX(16, token_goalcol(o)) - strlen(start)) / 8.0);
	xstrlcpy(sep, start, sizeof(sep));
	xstrlcat(sep, repeat('\t', ntabs), sizeof(sep));
	free(start);

	size_t arrlen = array_len(arr);
	if (!(parser->settings.behavior & PARSER_KEEP_EOL_COMMENTS) &&
	    arrlen > 0 && (o = array_get(arr, arrlen - 1)) != NULL &&
	    !preserve_eol_comment(token_data(o))) {
		/* Try to push end of line comments out of the way above
		 * the variable as a way to preserve them.  They clash badly
		 * with sorting tokens in variables.  We could add more
		 * special cases for this, but often having them at the top
		 * is just as good.
		 */
		arrlen--;
		parser_enqueue_output(parser, token_data(o));
		parser_enqueue_output(parser, "\n");
	}
	if (arrlen == 0) {
		char *var = variable_tostring(token_variable(o));
		parser_enqueue_output(parser, var);
		parser_enqueue_output(parser, "\n");
		return;
	}

	const char *end = " \\\n";
	for (size_t i = 0; i < arrlen; i++) {
		struct Token *o = array_get(arr, i);
		char *line = token_data(o);
		if (!line || strlen(line) == 0) {
			continue;
		}
		if (i == arrlen - 1) {
			end = "\n";
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		parser_enqueue_output(parser, end);
		switch (token_type(o)) {
		case VARIABLE_TOKEN:
			if (i == 0) {
				size_t ntabs = ceil(MAX(16, token_goalcol(o)) / 8.0);
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
			errx(1, "unhandled token type: %i", token_type(o));
		}
	}
}

int
tokcompare(const void *a, const void *b)
{
	struct Token *ao = *(struct Token**)a;
	struct Token *bo = *(struct Token**)b;
	if (variable_cmp(token_variable(ao), token_variable(bo)) == 0) {
		return compare_tokens(token_variable(ao), token_data(ao), token_data(bo));
	}
	return strcasecmp(token_data(ao), token_data(bo));
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

	struct Token *eol_comment;
	size_t tokenslen = array_len(tokens);
	if (!(parser->settings.behavior & PARSER_KEEP_EOL_COMMENTS) &&
	    tokenslen > 0 && (eol_comment = array_get(tokens, tokenslen - 1)) != NULL &&
	    (token_type(eol_comment) == VARIABLE_TOKEN) && !preserve_eol_comment(token_data(eol_comment))) {
		/* Try to push end of line comments out of the way above
		 * the variable as a way to preserve them.  They clash badly
		 * with sorting tokens in variables.  We could add more
		 * special cases for this, but often having them at the top
		 * is just as good.
		 */
		tokenslen--;
	} else {
		eol_comment = NULL;
	}

	struct Array *arr = array_new(sizeof(struct Token *));
	struct Token *o = array_get(tokens, 0);
	size_t wrapcol;
	if (token_variable(o) && ignore_wrap_col(token_variable(o))) {
		wrapcol = 99999999;
	} else {
		/* Minus ' \' at end of line */
		wrapcol = parser->settings.wrapcol - token_goalcol(o) - 2;
	}

	char row[1024] = {};
	struct Token *token = NULL;
	for (size_t i = 0; i < tokenslen; i++) {
		token = array_get(tokens, i);
		if (strlen(token_data(token)) == 0) {
			continue;
		}
		if ((strlen(row) + strlen(token_data(token))) > wrapcol) {
			if (strlen(row) == 0) {
				array_append(arr, token);
				continue;
			} else {
				struct Token *t = token_clone(token, row);
				array_append(parser->tokengc, t);
				array_append(arr, t);
				row[0] = 0;
			}
		}
		if (strlen(row) == 0) {
			xstrlcpy(row, token_data(token), sizeof(row));
		} else {
			xstrlcat(row, " ", sizeof(row));
			xstrlcat(row, token_data(token), sizeof(row));
		}
	}
	if (token && strlen(row) > 0 && array_len(arr) < tokenslen) {
		struct Token *t = token_clone(token, row);
		array_append(parser->tokengc, t);
		array_append(arr, t);
	}
	if (eol_comment) {
		array_append(arr, eol_comment);
	}
	print_newline_array(parser, arr);

	array_free(arr);
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
		char *word = token_data(t);

		assert(token_type(t) == TARGET_COMMAND_TOKEN);
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
				if (target_command_should_wrap(token_data(next))) {
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
		parser_output_print_rawlines(parser, token_lines(t));
		goto cleanup;
	}

	parser_enqueue_output(parser, startlv1);
	int wrapped = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		char *word = token_data(t);

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
	if (parser->settings.behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		parser_output_dump_tokens(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_RAWLINES) {
		/* no-op */
	} else if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser_output_edited(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_REFORMAT) {
		parser_output_reformatted(parser);
	}
}

void
parser_output_edited(struct Parser *parser)
{
	parser_find_goalcols(parser);

	struct Array *variable_arr = array_new(sizeof(struct Token *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		switch (token_type(o)) {
		case CONDITIONAL_END:
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		case CONDITIONAL_START:
		case CONDITIONAL_TOKEN:
			break;
		case VARIABLE_END:
			if (array_len(variable_arr) == 0) {
				char *var = variable_tostring(token_variable(o));
				parser_enqueue_output(parser, var);
				free(var);
				parser_enqueue_output(parser, "\n");
				array_truncate(variable_arr);
			} else {
				variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			}
			break;
		case VARIABLE_START:
			array_truncate(variable_arr);
			break;
		case VARIABLE_TOKEN:
			array_append(variable_arr, o);
			break;
		case TARGET_COMMAND_END:
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		case TARGET_COMMAND_START:
		case TARGET_COMMAND_TOKEN:
		case TARGET_END:
			break;
		case COMMENT:
		case TARGET_START:
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		default:
			errx(1, "Unhandled output type: %i", token_type(o));
		}
	}
	variable_arr = parser_output_reformatted_helper(parser, variable_arr);
	array_free(variable_arr);
}

struct Array *
parser_output_sort_opt_use(struct Parser *parser, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return arr;
	}

	struct Token *t = array_get(arr, 0);
	assert(token_type(t) == VARIABLE_TOKEN);
	int opt_use = 0;
	if (matches(RE_OPT_USE, variable_name(token_variable(t)), NULL)) {
		opt_use = 1;
	} else if (matches(RE_OPT_VARS, variable_name(token_variable(t)), NULL)) {
		opt_use = 0;
	} else {
		return arr;
	}

	struct Array *up = array_new(sizeof(struct Token *));
	for (size_t i = 0; i < array_len(arr); i++) {
		t = array_get(arr, i);
		assert(token_type(t) == VARIABLE_TOKEN);
		if (!matches(RE_OPT_USE_PREFIX, token_data(t), NULL)) {
			array_append(up, t);
			continue;
		}
		char *suffix = strchr(token_data(t), '=');
		if (suffix == NULL ) {
			array_append(up, t);
			continue;
		}
		suffix++;

		char *prefix = xstrndup(token_data(t), suffix - token_data(t));
		for (char *prefixp = prefix; *prefixp != 0; prefixp++) {
			*prefixp = toupper(*prefixp);
		}
		struct Array *values = array_new(sizeof(char *));
		size_t bufsz = strlen(token_data(t)) + 1;
		char *buf = xmalloc(bufsz);
		if (opt_use) {
			char *var;
			if (asprintf(&var, "USE_%s", prefix) < 0) {
				warn("asprintf");
				abort();
			}
			xstrlcpy(buf, prefix, bufsz);
			char *tmp, *s, *token;
			tmp = s = xstrdup(suffix);
			while ((token = strsep(&s, ",")) != NULL) {
				struct Token *t2 = token_new(VARIABLE_TOKEN, token_lines(t), token, var, NULL, NULL);
				array_append(values, t2);
			}
			free(tmp);
			tmp = var = NULL;

			array_sort(values, tokcompare);
			for (size_t j = 0; j < array_len(values); j++) {
				struct Token *t2 = array_get(values, j);
				xstrlcat(buf, token_data(t2), bufsz);
				if (j < array_len(values) - 1) {
					xstrlcat(buf, ",", bufsz);
				}
			}
			array_free(values);
		} else {
			xstrlcpy(buf, prefix, bufsz);
			xstrlcat(buf, suffix, bufsz);
		}
		free(prefix);

		array_append(parser->tokengc, t);
		array_append(up, token_clone(t, buf));
		free(buf);
	}
	array_free(arr);
	return up;
}

struct Array *
parser_output_reformatted_helper(struct Parser *parser, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return arr;
	}
	struct Token *t0 = array_get(arr, 0);

	/* Leave variables unformatted that have $\ in them. */
	if (array_len(arr) == 1 && strstr(token_data(t0), "$\001") != NULL) {
		parser_output_print_rawlines(parser, token_lines(t0));
		goto cleanup;
	}

	if (array_find(parser->edited, t0, NULL) == -1 && (parser->settings.behavior & PARSER_OUTPUT_EDITED)) {
		parser_output_print_rawlines(parser, token_lines(t0));
		goto cleanup;
	}

	if (!(parser->settings.behavior & PARSER_UNSORTED_VARIABLES) &&
	    !leave_unsorted(token_variable(t0))) {
		arr = parser_output_sort_opt_use(parser, arr);
		array_sort(arr, tokcompare);
	}

	t0 = array_get(arr, 0);
	if (print_as_newlines(token_variable(t0))) {
		print_newline_array(parser, arr);
	} else {
		print_token_array(parser, arr);
	}

cleanup:
	array_truncate(arr);
	return arr;
}

void
parser_output_reformatted(struct Parser *parser)
{
	parser_find_goalcols(parser);

	struct Array *target_arr = array_new(sizeof(struct Token *));
	struct Array *variable_arr = array_new(sizeof(struct Token *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		switch (token_type(o)) {
		case CONDITIONAL_END:
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		case CONDITIONAL_START:
		case CONDITIONAL_TOKEN:
			break;
		case VARIABLE_END:
			if (array_len(variable_arr) == 0) {
				char *var = variable_tostring(token_variable(o));
				parser_enqueue_output(parser, var);
				free(var);
				parser_enqueue_output(parser, "\n");
				array_truncate(variable_arr);
			} else {
				variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			}
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
		case TARGET_START:
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		default:
			errx(1, "Unhandled output type: %i", token_type(o));
		}
	}
	if (array_len(target_arr) > 0) {
		print_token_array(parser, target_arr);
		array_truncate(target_arr);
	}
	variable_arr = parser_output_reformatted_helper(parser, variable_arr);
	array_free(target_arr);
	array_free(variable_arr);
}

void
parser_output_dump_tokens(struct Parser *parser)
{
	size_t maxvarlen = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (token_type(o) == VARIABLE_START && token_variable(o)) {
			char *var = variable_tostring(token_variable(o));
			maxvarlen = MAX(maxvarlen, strlen(var));
			free(var);
		}
	}

	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		const char *type;
		switch (token_type(t)) {
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
			break;
		case COMMENT:
			type = "comment";
			break;
		default:
			errx(1, "Unhandled output type: %i", token_type(t));
		}
		char *var = NULL;
		ssize_t len;
		if (token_variable(t) &&
		    (token_type(t) == VARIABLE_TOKEN ||
		     token_type(t) == VARIABLE_START ||
		     token_type(t) == VARIABLE_END)) {
			var = variable_tostring(token_variable(t));
			len = maxvarlen - strlen(var);
		} else if (token_conditional(t) &&
			   (token_type(t) == CONDITIONAL_END ||
			    token_type(t) == CONDITIONAL_START ||
			    token_type(t) == CONDITIONAL_TOKEN)) {
			var = conditional_tostring(token_conditional(t));
			len = maxvarlen - strlen(var);
		} else if (token_target(t) &&
			   (token_type(t) == TARGET_COMMAND_END ||
			    token_type(t) == TARGET_COMMAND_START ||
			    token_type(t) == TARGET_COMMAND_TOKEN ||
			    token_type(t) == TARGET_START ||
			    token_type(t) == TARGET_END)) {
			var = xstrdup(target_name(token_target(t)));
			len = maxvarlen - strlen(var);
		} else {
			len = maxvarlen - 1;
		}
		char *range = range_tostring(token_lines(t));
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

		if (token_data(t) &&
		    (token_type(t) != CONDITIONAL_START &&
		     token_type(t) != CONDITIONAL_END)) {
			parser_enqueue_output(parser, token_data(t));
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
 		if (linelen > 2 && line[linelen - 2] == '$' && line[linelen - 3] != '$') {
			/* Hack to "handle" things like $\ in variable values */
			line[linelen - 1] = 1;
		} else if (linelen > 1 && !isspace(line[linelen - 2])) {
			/* "Handle" lines that end without a preceding space before '\'. */
			line[linelen - 1] = ' ';
		} else {
			line[linelen - 1] = 0;
		}
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
		parser_append_token(parser, COMMENT, buf);
		goto next;
	}

	if (parser->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			if (parser->condname) {
				free(parser->condname);
				parser->condname = NULL;
			}
			parser->condname = str_trim(str_substr_dup(buf, 0, pos));

			parser_append_token(parser, CONDITIONAL_START, parser->condname);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, parser->condname);
			goto next;
		}
		if (consume_var(buf) == 0 && consume_target(buf) == 0) {
			parser_append_token(parser, TARGET_COMMAND_START, NULL);
			parser_tokenize(parser, buf, TARGET_COMMAND_TOKEN, 0);
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
		if (parser->condname) {
			free(parser->condname);
			parser->condname = NULL;
		}
		parser->condname = str_trim(str_substr_dup(buf, 0, pos));

		parser_append_token(parser, CONDITIONAL_START, parser->condname);
		parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
		parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
		parser_append_token(parser, CONDITIONAL_END, parser->condname);
		goto next;
	}

	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > strlen(buf)) {
			errx(1, "parser->varname too small");
		}
		char *tmp = str_substr_dup(buf, 0, pos);
		parser->varname = str_strip_dup(tmp);
		free(tmp);
		parser_append_token(parser, VARIABLE_START, NULL);
	}
	parser_tokenize(parser, buf, VARIABLE_TOKEN, pos);
	if (parser->varname == NULL) {
		errx(1, "parse error on line %s", range_tostring(&parser->lines));
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
	struct Array *tokens = array_new(sizeof(struct Array *));
	struct Variable *last_var = NULL;
	struct Token *last_end = NULL;
	struct Token *last_token = NULL;
	struct Array *ignored_tokens = array_new(sizeof(struct Array *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			if (last_var != NULL &&
			    variable_cmp(token_variable(t), last_var) == 0 &&
			    variable_modifier(last_var) != MODIFIER_EXPAND &&
			    variable_modifier(token_variable(t)) != MODIFIER_EXPAND) {
				array_append(ignored_tokens, t);
				if (last_end) {
					array_append(ignored_tokens, last_end);
					last_end = NULL;
				}
			}
			break;
		case VARIABLE_TOKEN:
			last_token = t;
			break;
		case VARIABLE_END:
			if (!last_token || !str_startswith(token_data(last_token), "#")) {
				last_end = t;
			}
			last_token = NULL;
			last_var = token_variable(t);
			break;
		default:
			last_var = NULL;
			break;
		}
	}

	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (array_find(ignored_tokens, t, NULL) == -1) {
			array_append(tokens, t);
		}
	}

	array_free(ignored_tokens);
	array_free(parser->tokens);
	parser->tokens = tokens;
}

void
parser_sanitize_append_modifier(struct Parser *parser)
{
	/* Sanitize += before bsd.options.mk */
	ssize_t start = -1;
	struct Array *seen = array_new(sizeof(struct Variable *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			start = i;
			break;
		case VARIABLE_END: {
			if (start < 0) {
				continue;
			}
			if (array_find(seen, token_variable(t), (ArrayCompareFn)&variable_cmp) != -1) {
				start = -1;
				continue;
			} else {
				array_append(seen, token_variable(t));
			}
			for (size_t j = start; j <= i; j++) {
				struct Token *o = array_get(parser->tokens, j);
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

int
parser_has_variable(struct Parser *parser, const char *var)
{
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		if (token_type(t) == VARIABLE_START &&
		    strcmp(variable_name(token_variable(t)), var) == 0) {
			return 1;
		}
	}
	return 0;
}

int
parser_output_variable_value(struct Parser *parser, const char *name)
{
	parser->settings.behavior |= PARSER_OUTPUT_RAWLINES;
	int found = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_TOKEN:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				found = 1;
				if (token_data(t)) {
					parser_enqueue_output(parser, token_data(t));
					parser_enqueue_output(parser, "\n");
				}
			}
			break;
		default:
			break;
		}
	}

	return !found;
}

int
parser_edit_set_variable(struct Parser *parser, const char *name, const char *value, const char *after)
{
	struct Array *tokens = array_new(sizeof(char *));
	if (parser_has_variable(parser, name)) {
		for (size_t i = 0; i < array_len(parser->tokens); i++) {
			struct Token *t = array_get(parser->tokens, i);
			if (token_type(t) == VARIABLE_TOKEN &&
			    strcmp(variable_name(token_variable(t)), name) == 0) {
					array_append(parser->tokengc, t);
					struct Token *et = token_clone(t, value);
					array_append(parser->edited, et);
					array_append(tokens, et);
			} else {
				array_append(tokens, t);
			}
		}
	} else if (after != NULL) {
		for (size_t i = 0; i < array_len(parser->tokens); i++) {
			struct Token *t = array_get(parser->tokens, i);
			array_append(tokens, t);
			if (token_type(t) == VARIABLE_END &&
			    strcmp(variable_name(token_variable(t)), after) == 0) {
				char *var;
				if (asprintf(&var, "%s=", name) < 0) {
					warn("asprintf");
					abort();
				}
				struct Token *token = token_new(VARIABLE_START, token_lines(t),
								NULL, var, NULL, NULL);
				array_append(parser->edited, token);
				array_append(tokens, token);

				token = token_new(VARIABLE_TOKEN, token_lines(t),
						  "1", var, NULL, NULL);
				array_append(parser->edited, token);
				array_append(tokens, token);

				token = token_new(VARIABLE_END, token_lines(t),
						  NULL, var, NULL, NULL);
				array_append(parser->edited, token);
				array_append(tokens, token);
			}
		}
	} else {
		errx(1, "cannot append: %s not currently set", name);
		array_free(tokens);
		return 1;
	}

	array_free(parser->tokens);
	parser->tokens = tokens;

	return 0;
}

int
parser_edit_bump_revision(struct Parser *parser)
{
	const char *after = "PORTVERSION";
	if (parser_has_variable(parser, "DISTVERSION")) {
		after = "DISTVERSION";
	}
	if (parser_has_variable(parser, "DISTVERSIONSUFFIX")) {
		after = "DISTVERSIONSUFFIX";
	}
	if (parser_has_variable(parser, "PORTVERSION") &&
	    !parser_has_variable(parser, "DISTVERSION")) {
		if (parser_has_variable(parser, "DISTVERSIONPREFIX")) {
			after = "DISTVERSIONPREFIX";
		}
		if (parser_has_variable(parser, "DISTVERSIONSUFFIX")) {
			after = "DISTVERSIONSUFFIX";
		}
	}

	char *current_revision = parser_lookup_variable(parser, "PORTREVISION");
	if (current_revision) {
		const char *errstr = NULL;
		int revision = strtonum(current_revision, 0, INT_MAX, &errstr);
		if (errstr == NULL) {
			revision++;
		}
		char *rev;
		if (asprintf(&rev, "%d", revision) < 0) {
			warn("asprintf");
			abort();
		}
		parser_edit_set_variable(parser, "PORTREVISION", rev, after);
		free(rev);
	} else {
		parser_edit_set_variable(parser, "PORTREVISION", "1", after);
	}

	return 0;
}

char *
parser_lookup_variable(struct Parser *parser, const char *name)
{
	struct Array *tokens = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			array_truncate(tokens);
			break;
		case VARIABLE_TOKEN:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				if (token_data(t)) {
					array_append(tokens, token_data(t));
				}
			}
			break;
		case VARIABLE_END:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
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

struct Array *
parser_get_all_variable_names(struct Parser *parser)
{
	struct Array *vars = array_new(sizeof(char *));
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			array_append(vars, variable_name(token_variable(t)));
			break;
		default:
			break;
		}
	}
	return vars;
}
