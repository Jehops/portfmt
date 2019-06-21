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
#include <errno.h>
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
#include "parser.h"
#include "regexp.h"
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
	enum ParserError error;
	char *error_supplement;
	char *inbuf;
	char *condname;
	char *targetname;
	char *varname;

	struct Array *edited; /* unowned struct Token * */
	struct Array *tokengc;
	struct Array *tokens;
	struct Array *result;
	struct Array *rawlines;

	int read_finished;
};

#define INBUF_SIZE 131072

static size_t consume_comment(const char *);
static size_t consume_conditional(const char *);
static size_t consume_target(const char *);
static size_t consume_token(struct Parser *, const char *, size_t, char, char, int);
static size_t consume_var(const char *);
static void parser_append_token(struct Parser *, enum TokenType, const char *);
static void parser_find_goalcols(struct Parser *);
static enum ParserError parser_output_dump_tokens(struct Parser *);
static enum ParserError parser_output_edited(struct Parser *);
static enum ParserError parser_output_prepare(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct Range *);
static void parser_output_print_target_command(struct Parser *, struct Array *);
static struct Array *parser_output_sort_opt_use(struct Parser *, struct Array *);
static struct Array *parser_output_reformatted_helper(struct Parser *, struct Array *);
static enum ParserError parser_output_reformatted(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static enum ParserError parser_read_internal(struct Parser *);
static enum ParserError parser_read_line(struct Parser *, char *);
static enum ParserError parser_tokenize(struct Parser *, const char *, enum TokenType, size_t);
static enum ParserError print_newline_array(struct Parser *, struct Array *);
static enum ParserError print_token_array(struct Parser *, struct Array *);
static char *range_tostring(struct Range *);

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
	struct Regexp *re = regexp_new(regex(RE_CONDITIONAL), buf);
	if (regexp_exec(re) == 0) {
		pos = regexp_length(re, 0);
	}
	regexp_free(re);

	if(pos > 0 && (buf[pos - 1] == '(' || buf[pos - 1] == '!')) {
		pos--;
	}

	return pos;
}

size_t
consume_target(const char *buf)
{
	// Variable assignments are prioritized and can be ambigious
	// due to :=, so check for it first.  Targets can also not
	// start with a tab which implies a conditional.
	if (consume_var(buf) > 0 || *buf == '\t') {
		return 0;
	}

	size_t pos = 0;
	struct Regexp *re = regexp_new(regex(RE_TARGET), buf);
	if (regexp_exec(re) == 0) {
		pos = regexp_length(re, 0);
	}
	regexp_free(re);
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
		parser->error = PARSER_ERROR_EXPECTED_CHAR;
		if (parser->error_supplement) {
			free(parser->error_supplement);
		}
		xasprintf(&parser->error_supplement, "%c", endchar);
		return 0;
	} else {
		return i;
	}
}

size_t
consume_var(const char *buf)
{
	size_t pos = 0;
	struct Regexp *re = regexp_new(regex(RE_VAR), buf);
	if (regexp_exec(re) == 0) {
		pos = regexp_length(re, 0);
	}
	regexp_free(re);
	return pos;
}

char *
range_tostring(struct Range *range)
{
	assert(range);
	assert(range->start < range->end);

	char *s;
	if (range->start == range->end - 1) {
		xasprintf(&s, "%zu", range->start);
	} else {
		xasprintf(&s, "%zu-%zu", range->start, range->end - 1);
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
	rules_init();

	struct Parser *parser = xmalloc(sizeof(struct Parser));

	parser->edited = array_new(sizeof(void *));
	parser->tokengc = array_new(sizeof(void *));
	parser->rawlines = array_new(sizeof(char *));
	parser->result = array_new(sizeof(char *));
	parser->tokens = array_new(sizeof(struct Token *));
	parser->error = PARSER_ERROR_OK;
	parser->error_supplement = NULL;
	parser->lines.start = 1;
	parser->lines.end = 1;
	parser->inbuf = xmalloc(INBUF_SIZE);
	parser->settings = *settings;

	if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser->settings.behavior &= ~PARSER_COLLAPSE_ADJACENT_VARIABLES;
	}

	return parser;
}

void
parser_free(struct Parser *parser)
{
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
	array_free(parser->edited);
	array_free(parser->tokens);

	if (parser->error_supplement) {
		free(parser->error_supplement);
	}
	free(parser->inbuf);
	free(parser);
}

char *
parser_error_tostring(struct Parser *parser)
{
	char *buf;
	char *lines = range_tostring(&parser->lines);
	switch (parser->error) {
	case PARSER_ERROR_OK:
		xasprintf(&buf, "line %s: no error", lines);
		break;
	case PARSER_ERROR_BUFFER_TOO_SMALL:
		xasprintf(&buf, "line %s: buffer too small", lines);
		break;
	case PARSER_ERROR_EDIT_FAILED:
		if (parser->error_supplement) { 
			xasprintf(&buf, "edit failed: %s", parser->error_supplement);
		} else {
			xasprintf(&buf, "line %s: edit failed", lines);
		}
		break;
	case PARSER_ERROR_EXPECTED_CHAR:
		if (parser->error_supplement) { 
			xasprintf(&buf, "line %s: expected '%s'", lines, parser->error_supplement);
		} else {
			xasprintf(&buf, "line %s: expected char", lines);
		}
		break;
	case PARSER_ERROR_IO:
		if (parser->error_supplement) {
			xasprintf(&buf, "line %s: IO error: %s", lines, parser->error_supplement);
		} else {
			xasprintf(&buf, "line %s: IO error", lines);
		}
		break;
	case PARSER_ERROR_UNHANDLED_TOKEN_TYPE:
		xasprintf(&buf, "line %s: unhandled token type", lines);
		break;
	case PARSER_ERROR_UNSPECIFIED:
		xasprintf(&buf, "line %s: parse error", lines);
		break;
	}

	free(lines);
	return buf;
}

void
parser_append_token(struct Parser *parser, enum TokenType type, const char *data)
{
	struct Token *t = token_new(type, &parser->lines, data, parser->varname,
				    parser->condname, parser->targetname);
	if (t == NULL) {
		return;
	}
	array_append_unique(parser->tokengc, t, NULL);
	array_append(parser->tokens, t);
}

void
parser_enqueue_output(struct Parser *parser, const char *s)
{
	assert(s != NULL);
	array_append(parser->result, xstrdup(s));
}

enum ParserError
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
			if (dollar > 1) {
				if (c == '(') {
					i = consume_token(parser, line, i - 2, '(', ')', 0);
					if (parser->error != PARSER_ERROR_OK) {
						return parser->error;
					}
					dollar = 0;
					continue;
				} else if (c == '$') {
					dollar++;
				} else {
					dollar = 0;
				}
			} else if (c == '{') {
				i = consume_token(parser, line, i, '{', '}', 0);
				dollar = 0;
			} else if (c == '(') {
				i = consume_token(parser, line, i, '(', ')', 0);
				dollar = 0;
			} else if (isalnum(c) || c == '@' || c == '<' || c == '>' || c == '/' ||
				   c == '?' || c == '*' || c == '^' || c == '-' || c == '_') {
				dollar = 0;
			} else if (c == ' ' || c == '\\') {
				/* '$ ' or '$\' are ignored by make for some reason instead of making
				 * it an error, so we do the same...
				 */
				dollar = 0;
				i--;
			} else if (c == 1) {
				dollar = 0;
			} else if (c == '$') {
				dollar++;
			} else {
				parser->error = PARSER_ERROR_EXPECTED_CHAR;
				if (parser->error_supplement) {
					free(parser->error_supplement);
				}
				parser->error_supplement = xstrdup("$");
			}
			if (parser->error != PARSER_ERROR_OK) {
				return parser->error;
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
				return PARSER_ERROR_OK;
			}
			if (parser->error != PARSER_ERROR_OK) {
				return parser->error;
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

	return PARSER_ERROR_OK;
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
			if (is_comment(t)) {
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
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			return;
		}
	}
	if (tokens_start != -1) {
		parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
	}
}

enum ParserError
print_newline_array(struct Parser *parser, struct Array *arr)
{
	struct Token *o = array_get(arr, 0);
	assert(o && token_data(o) != NULL);
	assert(strlen(token_data(o)) != 0);
	assert(token_type(o) == VARIABLE_TOKEN);

	if (array_len(arr) == 0) {
		char *var = variable_tostring(token_variable(o));
		parser_enqueue_output(parser, var);
		parser_enqueue_output(parser, "\n");
		return PARSER_ERROR_OK;
	}

	char *start = variable_tostring(token_variable(o));
	parser_enqueue_output(parser, start);
	size_t ntabs = ceil((MAX(16, token_goalcol(o)) - strlen(start)) / 8.0);
	char *sep = repeat('\t', ntabs);

	const char *end = " \\\n";
	for (size_t i = 0; i < array_len(arr); i++) {
		struct Token *o = array_get(arr, i);
		char *line = token_data(o);
		if (!line || strlen(line) == 0) {
			continue;
		}
		if (i == array_len(arr) - 1) {
			end = "\n";
		}
		parser_enqueue_output(parser, sep);
		parser_enqueue_output(parser, line);
		parser_enqueue_output(parser, end);
		switch (token_type(o)) {
		case VARIABLE_TOKEN:
			if (i == 0) {
				size_t ntabs = ceil(MAX(16, token_goalcol(o)) / 8.0);
				free(sep);
				sep = repeat('\t', ntabs);
			}
			break;
		case CONDITIONAL_TOKEN:
			free(sep);
			sep = xstrdup("\t");
			break;
		case TARGET_COMMAND_TOKEN:
			free(sep);
			sep = xstrdup("\t\t");
			break;
		default:
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			goto cleanup;
		}
	}
cleanup:
	free(sep);

	return parser->error;
}

enum ParserError
print_token_array(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) < 2) {
		return print_newline_array(parser, tokens);
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

	size_t rowsz = 8192;
	char *row = xmalloc(rowsz);
	struct Token *token = NULL;
	for (size_t i = 0; i < array_len(tokens); i++) {
		token = array_get(tokens, i);
		size_t tokenlen = strlen(token_data(token));
		if (tokenlen == 0) {
			continue;
		}
		if ((strlen(row) + tokenlen) > wrapcol) {
			if (strlen(row) == 0) {
				array_append(arr, token);
				continue;
			} else {
				struct Token *t = token_clone(token, row);
				array_append_unique(parser->tokengc, t, NULL);
				array_append(arr, t);
				row[0] = 0;
			}
		}
		size_t len;
		if (strlen(row) == 0) {
			if ((len = strlcpy(row, token_data(token), rowsz)) >= rowsz) {
				parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
				goto cleanup;
			}
		} else {
			if ((len = strlcat(row, " ", rowsz)) >= rowsz) {
				parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
				goto cleanup;
			}
			if ((len = strlcat(row, token_data(token), rowsz)) >= rowsz) {
				parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
				goto cleanup;
			}
		}
	}
	if (token && strlen(row) > 0 && array_len(arr) < array_len(tokens)) {
		struct Token *t = token_clone(token, row);
		array_append_unique(parser->tokengc, t, NULL);
		array_append(arr, t);
	}
	print_newline_array(parser, arr);

cleanup:
	free(row);
	array_free(arr);

	return parser->error;
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

enum ParserError
parser_output_prepare(struct Parser *parser)
{
	if (!parser->read_finished) {
		parser_read_finish(parser);
	}

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		parser_output_dump_tokens(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_RAWLINES) {
		/* no-op */
	} else if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser_output_edited(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_REFORMAT) {
		parser_output_reformatted(parser);
	}

	return parser->error;
}

enum ParserError
parser_output_edited(struct Parser *parser)
{
	parser_find_goalcols(parser);
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

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
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			if (array_find(parser->edited, o, NULL) == -1) {
				parser_output_print_rawlines(parser, token_lines(o));
			} else {
				parser_enqueue_output(parser, token_data(o));
				parser_enqueue_output(parser, "\n");
			}
			break;
		case TARGET_START:
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		default:
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			goto cleanup;
		}
	}
	variable_arr = parser_output_reformatted_helper(parser, variable_arr);
cleanup:
	array_free(variable_arr);

	return parser->error;
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
	if (matches(RE_OPT_USE, variable_name(token_variable(t)))) {
		opt_use = 1;
	} else if (matches(RE_OPT_VARS, variable_name(token_variable(t)))) {
		opt_use = 0;
	} else {
		return arr;
	}

	struct Array *up = array_new(sizeof(struct Token *));
	for (size_t i = 0; i < array_len(arr); i++) {
		t = array_get(arr, i);
		assert(token_type(t) == VARIABLE_TOKEN);
		if (!matches(RE_OPT_USE_PREFIX, token_data(t))) {
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
			xasprintf(&var, "USE_%s", prefix);
			xstrlcpy(buf, prefix, bufsz);
			char *tmp, *s, *token;
			tmp = s = xstrdup(suffix);
			while ((token = strsep(&s, ",")) != NULL) {
				struct Token *t2 = token_new(VARIABLE_TOKEN, token_lines(t), token, var, NULL, NULL);
				if (t2 != NULL) {
					array_append(values, t2);
				}
			}
			free(tmp);
			free(var);
			tmp = var = NULL;

			array_sort(values, compare_tokens);
			for (size_t j = 0; j < array_len(values); j++) {
				struct Token *t2 = array_get(values, j);
				xstrlcat(buf, token_data(t2), bufsz);
				if (j < array_len(values) - 1) {
					xstrlcat(buf, ",", bufsz);
				}
				token_free(t2);
			}
			array_free(values);
		} else {
			xstrlcpy(buf, prefix, bufsz);
			xstrlcat(buf, suffix, bufsz);
		}
		free(prefix);

		struct Token *t2 = token_clone(t, buf);
		array_append_unique(parser->tokengc, t2, NULL);
		array_append(up, t2);
		free(buf);
	}
	array_free(arr);
	return up;
}

struct Array *
parser_output_reformatted_helper(struct Parser *parser, struct Array *arr /* unowned struct Token */)
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
		array_sort(arr, compare_tokens);
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

enum ParserError
parser_output_reformatted(struct Parser *parser)
{
	parser_find_goalcols(parser);
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

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
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			if (array_find(parser->edited, o, NULL) == -1) {
				parser_output_print_rawlines(parser, token_lines(o));
			} else {
				parser_enqueue_output(parser, token_data(o));
				parser_enqueue_output(parser, "\n");
			}
			break;
		case TARGET_START:
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			parser_output_print_rawlines(parser, token_lines(o));
			break;
		default:
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			goto cleanup;
		}
		if (parser->error != PARSER_ERROR_OK) {
			goto cleanup;
		}
	}
	if (array_len(target_arr) > 0) {
		print_token_array(parser, target_arr);
		array_truncate(target_arr);
	}
	variable_arr = parser_output_reformatted_helper(parser, variable_arr);
cleanup:
	array_free(target_arr);
	array_free(variable_arr);

	return parser->error;
}

enum ParserError
parser_output_dump_tokens(struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

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
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			return parser->error;
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
		xasprintf(&buf, "%-20s %8s ", type, range);
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

	return PARSER_ERROR_OK;
}

enum ParserError
parser_read_line(struct Parser *parser, char *line)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	size_t linelen = strlen(line);

	array_append(parser->rawlines, xstrdup(line));

	parser->lines.end++;

	int will_continue = matches(RE_CONTINUE_LINE, line);
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
			if (strlcat(parser->inbuf, " ", INBUF_SIZE) >= INBUF_SIZE) {
				parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
				return parser->error;
			}
		}
	}

	if (strlcat(parser->inbuf, line, INBUF_SIZE) >= INBUF_SIZE) {
		parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
		return parser->error;
	}

	if (!will_continue) {
		enum ParserError error = parser_read_internal(parser);
		if (error != PARSER_ERROR_OK) {
			return error;
		}
		parser->lines.start = parser->lines.end;
		memset(parser->inbuf, 0, INBUF_SIZE);
	}

	parser->continued = will_continue;

	return PARSER_ERROR_OK;
}

enum ParserError
parser_read_from_file(struct Parser *parser, FILE *fp)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	ssize_t linelen;
	size_t linecap = 0;
	char *line = NULL;
	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		if (linelen > 0 && line[linelen - 1] == '\n') {
			line[linelen - 1] = 0;
		}
		enum ParserError error = parser_read_line(parser, line);
		if (error != PARSER_ERROR_OK) {
			free(line);
			return error;
		}
	}

	free(line);

	return PARSER_ERROR_OK;
}

enum ParserError
parser_read_internal(struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	char *buf = str_trim(parser->inbuf);
	size_t pos;

	pos = consume_comment(buf);
	if (pos > 0) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	} else if (matches(RE_EMPTY_LINE, buf)) {
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
			char *tmp = str_substr_dup(buf, 0, pos);
			parser->condname = str_trim(tmp);
			free(tmp);

			parser_append_token(parser, CONDITIONAL_START, parser->condname);
			parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
			parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
			parser_append_token(parser, CONDITIONAL_END, parser->condname);
			goto next;
		}
		if (consume_var(buf) == 0 && consume_target(buf) == 0 &&
		    *buf != 0 && *buf == '\t') {
			parser_append_token(parser, TARGET_COMMAND_START, NULL);
			parser_tokenize(parser, buf, TARGET_COMMAND_TOKEN, 0);
			parser_append_token(parser, TARGET_COMMAND_END, NULL);
			goto next;
		}
		if (consume_var(buf) > 0) {
			goto var;
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
		char *tmp = str_substr_dup(buf, 0, pos);
		parser->condname = str_trim(tmp);
		free(tmp);

		parser_append_token(parser, CONDITIONAL_START, parser->condname);
		parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
		parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
		parser_append_token(parser, CONDITIONAL_END, parser->condname);
		goto next;
	}

var:
	pos = consume_var(buf);
	if (pos != 0) {
		if (pos > strlen(buf)) {
			parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
			goto next;
		}
		char *tmp = str_substr_dup(buf, 0, pos);
		parser->varname = str_strip_dup(tmp);
		free(tmp);
		parser_append_token(parser, VARIABLE_START, NULL);
	}
	parser_tokenize(parser, buf, VARIABLE_TOKEN, pos);
	if (parser->varname == NULL) {
		parser->error = PARSER_ERROR_UNSPECIFIED;
	}
next:
	if (parser->varname) {
		parser_append_token(parser, VARIABLE_END, NULL);
		free(parser->varname);
		parser->varname = NULL;
	}
	free(buf);

	return parser->error;
}

enum ParserError
parser_read_finish(struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	if (!parser->continued) {
		parser->lines.end++;
	}

	if (strlen(parser->inbuf) > 0 &&
	    PARSER_ERROR_OK != parser_read_internal(parser)) {
		return parser->error;
	}

	if (parser->in_target) {
		parser_append_token(parser, TARGET_END, NULL);
	}

	// Set it now to avoid recursion in parser_edit()
	parser->read_finished = 1;

	if (!(parser->settings.behavior & PARSER_KEEP_EOL_COMMENTS) &&
	    PARSER_ERROR_OK != parser_edit(parser, refactor_sanitize_eol_comments, NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_COLLAPSE_ADJACENT_VARIABLES &&
	    PARSER_ERROR_OK != parser_edit(parser, refactor_collapse_adjacent_variables, NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_SANITIZE_APPEND &&
	    PARSER_ERROR_OK != parser_edit(parser, refactor_sanitize_append_modifier, NULL)) {
		return parser->error;
	}

	return parser->error;
}

enum ParserError
parser_output_write_to_file(struct Parser *parser, FILE *fp)
{
	parser_output_prepare(parser);
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	int fd = fileno(fp);
	if (parser->settings.behavior & PARSER_OUTPUT_INPLACE) {
		if (lseek(fd, 0, SEEK_SET) < 0) {
			parser->error = PARSER_ERROR_IO;
			if (parser->error_supplement) {
				free(parser->error_supplement);
			}
			xasprintf(&parser->error_supplement, "lseek: %s", strerror(errno));
			return parser->error;
		}
		if (ftruncate(fd, 0) < 0) {
			parser->error = PARSER_ERROR_IO;
			if (parser->error_supplement) {
				free(parser->error_supplement);
			}
			xasprintf(&parser->error_supplement, "ftruncate: %s", strerror(errno));
			return parser->error;
		}
	}

	size_t len = array_len(parser->result);
	if (len == 0) {
		return PARSER_ERROR_OK;
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
			parser->error = PARSER_ERROR_IO;
			if (parser->error_supplement) {
				free(parser->error_supplement);
			}
			xasprintf(&parser->error_supplement, "writev: %s", strerror(errno));
			free(iov);
			return parser->error;
		}
	}

	/* Collect garbage */
	for (size_t i = 0; i < array_len(parser->result); i++) {
		free(array_get(parser->result, i));
	}
	array_truncate(parser->result);
	free(iov);

	return PARSER_ERROR_OK;
}

enum ParserError
parser_read_from_buffer(struct Parser *parser, const char *input, size_t len)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	enum ParserError error = PARSER_ERROR_OK;
	char *buf, *bufp, *line;
	buf = bufp = xstrndup(input, len);
	while ((line = strsep(&bufp, "\n")) != NULL) {
		if ((error = parser_read_line(parser, line)) != PARSER_ERROR_OK) {
			break;
		}
	}
	free(buf);

	return error;
}

void
parser_mark_for_gc(struct Parser *parser, struct Token *t)
{
	array_append_unique(parser->tokengc, t, NULL);
}

void
parser_mark_edited(struct Parser *parser, struct Token *t)
{
	array_append(parser->edited, t);
}

enum ParserError
parser_edit(struct Parser *parser, ParserEditFn f, const void *userdata)
{
	if (!parser->read_finished) {
		parser_read_finish(parser);
	}

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	enum ParserError error = PARSER_ERROR_OK;
	struct Array *tokens = f(parser, parser->tokens, &error, userdata);
	if (tokens && tokens != parser->tokens) {
		array_free(parser->tokens);
		parser->tokens = tokens;
	}

	if (error != PARSER_ERROR_OK) {
		parser->error = error;
		if (parser->error_supplement) {
			free(parser->error_supplement);
		}
		parser->error_supplement = parser_error_tostring(parser);
		parser->error = PARSER_ERROR_EDIT_FAILED;
	}

	return parser->error;
}

struct ParserSettings parser_settings(struct Parser *parser)
{
	return parser->settings;
}
