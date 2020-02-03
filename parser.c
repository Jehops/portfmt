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
#include "diff.h"
#include "diffutil.h"
#include "parser.h"
#include "parser/plugin.h"
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
	char *error_msg;
	char *inbuf;
	char *condname;
	char *targetname;
	char *varname;

	struct Array *edited; /* unowned struct Token * */
	struct Array *tokengc;
	struct Array *tokens;
	struct Array *result;
	struct Array *rawlines;

	struct Array *port_options;
	struct Array *port_options_groups;
	int port_options_looked_up;

#if PORTFMT_SUBPACKAGES
	struct Array *subpackages;
	int subpackages_looked_up;
#endif

	int read_finished;
};

#define INBUF_SIZE 131072

static size_t consume_comment(const char *);
static size_t consume_conditional(const char *);
static size_t consume_target(const char *);
static size_t consume_token(struct Parser *, const char *, size_t, char, char, int);
static size_t consume_var(const char *);
static int is_empty_line(const char *);
static void parser_append_token(struct Parser *, enum TokenType, const char *);
static void parser_find_goalcols(struct Parser *);
static struct Variable *parser_lookup_variable_internal(struct Parser *, const char *, struct Array **, struct Array **, int);
static void parser_output_dump_tokens(struct Parser *);
static void parser_output_prepare(struct Parser *);
static void parser_output_print_rawlines(struct Parser *, struct Range *);
static void parser_output_print_target_command(struct Parser *, struct Array *);
static struct Array *parser_output_sort_opt_use(struct Parser *, struct Array *);
static struct Array *parser_output_reformatted_helper(struct Parser *, struct Array *);
static void parser_output_reformatted(struct Parser *);
static void parser_output_diff(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read_internal(struct Parser *);
static void parser_read_line(struct Parser *, char *);
static void parser_tokenize(struct Parser *, const char *, enum TokenType, size_t);
static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
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
	struct Regexp *re = regexp_new(regex(RE_CONDITIONAL));
	if (regexp_exec(re, buf) == 0) {
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
	struct Regexp *re = regexp_new(regex(RE_TARGET));
	if (regexp_exec(re, buf) == 0) {
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
		free(parser->error_msg);
		xasprintf(&parser->error_msg, "%c", endchar);
		return 0;
	} else {
		return i;
	}
}

size_t
consume_var(const char *buf)
{
	size_t pos = 0;
	struct Regexp *re = regexp_new(regex(RE_VAR));
	if (regexp_exec(re, buf) == 0) {
		pos = regexp_length(re, 0);
	}
	regexp_free(re);
	return pos;
}

int
is_empty_line(const char *buf)
{
	for (const char *p = buf; *p != 0; p++) {
		if (!isspace(*p)) {
			return 0;
		}
	}

	return 1;
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
	settings->filename = NULL;
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

	parser->edited = array_new();
	parser->tokengc = array_new();
	parser->rawlines = array_new();
	parser->result = array_new();
	parser->tokens = array_new();
	parser->port_options = array_new();
	parser->port_options_groups = array_new();
	parser->error = PARSER_ERROR_OK;
	parser->error_msg = NULL;
	parser->lines.start = 1;
	parser->lines.end = 1;
	parser->inbuf = xmalloc(INBUF_SIZE);
	parser->settings = *settings;
	if (settings->filename) {
		char *filename = settings->filename;
		// XXX: We could sanitize a lot more here
		if (str_startswith(filename, "./")) {
			filename += 2;
		}
		parser->settings.filename = xstrdup(filename);
	} else {
		parser->settings.filename = xstrdup("/dev/stdin");
	}
#if PORTFMT_SUBPACKAGES
	parser->subpackages = array_new();

	// There is always a main subpackage
	array_append(parser->subpackages, xstrdup("main"));
#endif

	if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser->settings.behavior &= ~PARSER_COLLAPSE_ADJACENT_VARIABLES;
	}

	return parser;
}

void
parser_free(struct Parser *parser)
{
	if (parser == NULL) {
		return;
	}

	for (size_t i = 0; i < array_len(parser->port_options); i++) {
		free(array_get(parser->port_options, i));
	}
	array_free(parser->port_options);

	for (size_t i = 0; i < array_len(parser->port_options_groups); i++) {
		free(array_get(parser->port_options_groups, i));
	}
	array_free(parser->port_options_groups);

#if PORTFMT_SUBPACKAGES
	for (size_t i = 0; i < array_len(parser->subpackages); i++) {
		free(array_get(parser->subpackages, i));
	}
	array_free(parser->subpackages);
#endif

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

	free(parser->settings.filename);
	free(parser->error_msg);
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
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: buffer too small: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: buffer too small", lines);
		}
		break;
	case PARSER_ERROR_DIFFERENCES_FOUND:
		xasprintf(&buf, "differences found");
		break;
	case PARSER_ERROR_EDIT_FAILED:
		if (parser->error_msg) {
			xasprintf(&buf, "%s", parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: edit failed", lines);
		}
		break;
	case PARSER_ERROR_EXPECTED_CHAR:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: expected char: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: expected char", lines);
		}
		break;
	case PARSER_ERROR_EXPECTED_INT:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: expected integer: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: expected integer", lines);
		}
		break;
	case PARSER_ERROR_EXPECTED_TOKEN:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: expected %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: expected token", lines);
		}
		break;
	case PARSER_ERROR_INVALID_ARGUMENT:
		if (parser->error_msg) {
			xasprintf(&buf, "invalid argument: %s", parser->error_msg);
		} else {
			xasprintf(&buf, "invalid argument");
		}
		break;
	case PARSER_ERROR_INVALID_REGEXP:
		if (parser->error_msg) {
			xasprintf(&buf, "invalid regexp: %s", parser->error_msg);
		} else {
			xasprintf(&buf, "invalid regexp");
		}
		break;
	case PARSER_ERROR_IO:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: IO error: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: IO error", lines);
		}
		break;
	case PARSER_ERROR_NOT_FOUND:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: not found: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: not found", lines);
		}
		break;
	case PARSER_ERROR_UNHANDLED_TOKEN_TYPE:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: unhandled token type: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: unhandled token type", lines);
		}
		break;
	case PARSER_ERROR_UNSPECIFIED:
		if (parser->error_msg) {
			xasprintf(&buf, "line %s: parse error: %s", lines, parser->error_msg);
		} else {
			xasprintf(&buf, "line %s: parse error", lines);
		}
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
		parser->error = PARSER_ERROR_EXPECTED_TOKEN;
		parser->error_msg = xstrdup(token_type_tostring(type));
		return;
	}
	parser_mark_for_gc(parser, t);
	array_append(parser->tokens, t);
}

void
parser_enqueue_output(struct Parser *parser, const char *s)
{
	assert(s != NULL);
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
			if (dollar > 1) {
				if (c == '(') {
					i = consume_token(parser, line, i - 2, '(', ')', 0);
					if (parser->error != PARSER_ERROR_OK) {
						return;
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
				   c == '?' || c == '*' || c == '^' || c == '-' || c == '_' ||
				   c == ')') {
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
				free(parser->error_msg);
				parser->error_msg = xstrdup("$");
			}
			if (parser->error != PARSER_ERROR_OK) {
				return;
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
				parser->error = PARSER_ERROR_OK;
				return;
			}
			if (parser->error != PARSER_ERROR_OK) {
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

	parser->error = PARSER_ERROR_OK;
}

void
parser_propagate_goalcol(struct Parser *parser, size_t start, size_t end,
			 int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		struct Token *t = array_get(parser->tokens, k);
		if (token_variable(t) && !skip_goalcol(parser, token_variable(t))) {
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
			if (var && skip_goalcol(parser, var)) {
				token_set_goalcol(t, indent_goalcol(var));
			} else {
				moving_goalcol = MAX(indent_goalcol(var), moving_goalcol);
			}
			break;
		case TARGET_END:
		case TARGET_START:
		case CONDITIONAL_END:
		case CONDITIONAL_START:
		case CONDITIONAL_TOKEN:
		case TARGET_COMMAND_END:
		case TARGET_COMMAND_START:
		case TARGET_COMMAND_TOKEN:
			break;
		case COMMENT:
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

void
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
		parser->error = PARSER_ERROR_OK;
		return;
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
}

void
print_token_array(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) < 2) {
		print_newline_array(parser, tokens);
		return;
	}

	struct Array *arr = array_new();
	struct Token *o = array_get(tokens, 0);
	size_t wrapcol;
	if (token_variable(o) && ignore_wrap_col(parser, token_variable(o))) {
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
				parser_mark_for_gc(parser, t);
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
		parser_mark_for_gc(parser, t);
		array_append(arr, t);
	}
	print_newline_array(parser, arr);

cleanup:
	free(row);
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

	struct Array *commands = array_new();
	struct Array *merge = array_new();
	char *command = NULL;
	int wrap_after = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
		char *word = token_data(t);
		assert(token_type(t) == TARGET_COMMAND_TOKEN);
		assert(word && strlen(word) != 0);

		if (command == NULL) {
			command = word;
			if (*command == '@') {
				command++;
			}
		}
		if (target_command_should_wrap(word)) {
			command = NULL;
		}

		if (command &&
		    (strcmp(command, "${SED}") == 0 ||
		     strcmp(command, "${REINPLACE_CMD}") == 0)) {
			if (strcmp(word, "-e") == 0) {
				array_append(merge, word);
				wrap_after = 1;
				continue;
			}
		}

		array_append(merge, word);
		array_append(commands, str_join(merge, " "));
		if (wrap_after) {
			// An empty string is abused as a "wrap line here" marker
			array_append(commands, xstrdup(""));
			wrap_after = 0;
		}
		array_truncate(merge);
	}
	if (array_len(merge) > 0) {
		array_append(commands, str_join(merge, " "));
		if (wrap_after) {
			// An empty string is abused as a "wrap line here" marker
			array_append(commands, xstrdup(""));
			wrap_after = 0;
		}
	}
	array_free(merge);
	merge = NULL;

	const char *endline = "\n";
	const char *endnext = "\\\n";
	const char *endword = " ";
	const char *startlv0 = "";
	const char *startlv1 = "\t";
	const char *startlv2 = "\t\t";
	const char *start = startlv0;

	// Find the places we need to wrap to the next line.
	struct Array *wraps = array_new();
	size_t column = 8;
	int complexity = 0;
	size_t command_i = 0;
	for (size_t i = 0; i < array_len(commands); i++) {
		char *word = array_get(commands, i);

		if (command == NULL) {
			command = word;
			command_i = i;
		}
		if (target_command_should_wrap(word)) {
			command = NULL;
			command_i = 0;
		}

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
		    strcmp(word, "") == 0 || target_command_should_wrap(word) ||
		    (command && i > command_i && target_command_wrap_after_each_token(command))) {
			if (i + 1 < array_len(commands)) {
				char *next = array_get(commands, i + 1);
				if (strcmp(next, "") == 0 || target_command_should_wrap(next)) {
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
		if (array_find(parser->edited, t, NULL, NULL) == -1) {
			parser_output_print_rawlines(parser, token_lines(t));
			goto cleanup;
		}
	}

	parser_enqueue_output(parser, startlv1);
	int wrapped = 0;
	for (size_t i = 0; i < array_len(commands); i++) {
		const char *word = array_get(commands, i);

		if (wrapped) {
			parser_enqueue_output(parser, startlv2);
		}
		wrapped = array_find(wraps, (void*)i, NULL, NULL) > -1;

		parser_enqueue_output(parser, word);
		if (wrapped) {
			if (i == array_len(tokens) - 1) {
				parser_enqueue_output(parser, endline);
			} else {
				if (strcmp(word, "") != 0) {
					parser_enqueue_output(parser, endword);
				}
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
	for (size_t i = 0; i < array_len(commands); i++) {
		free(array_get(commands, i));
	}
	array_free(commands);
	array_free(wraps);
}

void
parser_output_prepare(struct Parser *parser)
{
	if (!parser->read_finished) {
		parser_read_finish(parser);
	}

	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	if (parser->settings.behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		parser_output_dump_tokens(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_RAWLINES) {
		/* no-op */
	} else if (parser->settings.behavior & PARSER_OUTPUT_EDITED) {
		parser_output_reformatted(parser);
	} else if (parser->settings.behavior & PARSER_OUTPUT_REFORMAT) {
		parser_output_reformatted(parser);
	}

	if (parser->settings.behavior & PARSER_OUTPUT_DIFF) {
		parser_output_diff(parser);
	}
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
	char *helper = NULL;
	if (is_options_helper(parser, variable_name(token_variable(t)), NULL, &helper, NULL)) {
		if (strcmp(helper, "USE") == 0 || strcmp(helper, "USE_OFF") == 0)  {
			opt_use = 1;
		} else if (strcmp(helper, "VARS") == 0 || strcmp(helper, "VARS_OFF") == 0) {
			opt_use = 0;
		} else {
			free(helper);
			return arr;
		}
		free(helper);
	} else {
		return arr;
	}

	struct Array *up = array_new();
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
		size_t bufsz = strlen(token_data(t)) + 1;
		char *buf = xmalloc(bufsz);
		if (opt_use) {
			struct Array *values = array_new();
			char *var;
			xasprintf(&var, "USE_%s", prefix);
			xstrlcpy(buf, prefix, bufsz);
			char *tmp, *s, *token;
			tmp = s = xstrdup(suffix);
			while ((token = strsep(&s, ",")) != NULL) {
				struct Variable *v = variable_new(var);
				struct Token *t2 = token_new_variable_token(token_lines(t), v, token);
				if (t2 != NULL) {
					array_append(values, t2);
				}
				free(v);
			}
			free(tmp);
			free(var);
			tmp = var = NULL;

			array_sort(values, compare_tokens, parser);
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
		parser_mark_for_gc(parser, t2);
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
	if ((array_len(arr) == 1 && strstr(token_data(t0), "$\001") != NULL) ||
	    (leave_unformatted(parser, token_variable(t0)) &&
	     array_find(parser->edited, t0, NULL, NULL) == -1)) {
		parser_output_print_rawlines(parser, token_lines(t0));
		goto cleanup;
	}

	if (array_find(parser->edited, t0, NULL, NULL) == -1 && (parser->settings.behavior & PARSER_OUTPUT_EDITED)) {
		parser_output_print_rawlines(parser, token_lines(t0));
		goto cleanup;
	}

	if (!(parser->settings.behavior & PARSER_UNSORTED_VARIABLES) &&
	    !leave_unsorted(parser, token_variable(t0))) {
		arr = parser_output_sort_opt_use(parser, arr);
		array_sort(arr, compare_tokens, parser);
	}

	t0 = array_get(arr, 0);
	if (print_as_newlines(parser, token_variable(t0))) {
		print_newline_array(parser, arr);
	} else {
		print_token_array(parser, arr);
	}

cleanup:
	array_truncate(arr);
	return arr;
}

static void
parser_output_edited_insert_empty(struct Parser *parser, struct Token *prev)
{
	switch (token_type(prev)) {
	case CONDITIONAL_END: {
		enum ConditionalType type = conditional_type(token_conditional(prev));
		switch (type) {
		case COND_ENDFOR:
		case COND_ENDIF:
		case COND_ERROR:
		case COND_EXPORT_ENV:
		case COND_EXPORT_LITERAL:
		case COND_EXPORT:
		case COND_INCLUDE_POSIX:
		case COND_INCLUDE:
		case COND_SINCLUDE:
		case COND_UNDEF:
		case COND_UNEXPORT_ENV:
		case COND_UNEXPORT:
		case COND_WARNING:
			parser_enqueue_output(parser, "\n");
			break;
		default:
			break;
		}
		break;
	} case COMMENT:
	case TARGET_COMMAND_END:
	case TARGET_END:
	case TARGET_START:
		break;
	default:
		parser_enqueue_output(parser, "\n");
		break;
	}
}

void
parser_output_reformatted(struct Parser *parser)
{
	parser_find_goalcols(parser);
	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	struct Array *target_arr = array_new();
	struct Array *variable_arr = array_new();
	struct Token *prev = NULL;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		int edited = array_find(parser->edited, o, NULL, NULL) != -1;
		switch (token_type(o)) {
		case CONDITIONAL_END:
			if (edited) {
				parser_enqueue_output(parser, "\n");
			} else {
				parser_output_print_rawlines(parser, token_lines(o));
			}
			break;
		case CONDITIONAL_START:
			if (edited && prev) {
				parser_output_edited_insert_empty(parser, prev);
			}
			break;
		case CONDITIONAL_TOKEN:
			if (edited) {
				parser_enqueue_output(parser, token_data(o));
				parser_enqueue_output(parser, " ");
			}
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
			if (edited) {
				parser_enqueue_output(parser, token_data(o));
				parser_enqueue_output(parser, "\n");
			} else {
				parser_output_print_rawlines(parser, token_lines(o));
			}
			break;
		case TARGET_START:
			variable_arr = parser_output_reformatted_helper(parser, variable_arr);
			if (edited) {
				if (prev) {
					parser_output_edited_insert_empty(parser, prev);
				}
				parser_enqueue_output(parser, token_data(o));
				parser_enqueue_output(parser, "\n");
			} else {
				parser_output_print_rawlines(parser, token_lines(o));
			}
			break;
		default:
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			goto cleanup;
		}
		if (parser->error != PARSER_ERROR_OK) {
			goto cleanup;
		}
		prev = o;
	}
	if (array_len(target_arr) > 0) {
		print_token_array(parser, target_arr);
		array_truncate(target_arr);
	}
	variable_arr = parser_output_reformatted_helper(parser, variable_arr);
cleanup:
	array_free(target_arr);
	array_free(variable_arr);
}

void
parser_output_diff(struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	// Normalize result: one element = one line like parser->rawlines
	struct Array *lines = array_new();
	char *lines_buf = str_join(parser->result, "");
	char *token;
	while ((token = strsep(&lines_buf, "\n")) != NULL) {
		array_append(lines, token);
	}
	array_pop(lines);

	struct diff p;
	int rc = array_diff(parser->rawlines, lines, &p, str_compare, NULL);
	if (rc <= 0) {
		parser->error = PARSER_ERROR_UNSPECIFIED;
		free(parser->error_msg);
		xasprintf(&parser->error_msg, "could not create diff");
		return;
	}

	for (size_t i = 0; i < array_len(parser->result); i++) {
		char *line = array_get(parser->result, i);
		free(line);
	}
	array_truncate(parser->result);

	if (p.editdist > 0) {
		struct Array *new_result = diff_to_patch(&p, parser->settings.filename, parser->settings.filename, !(parser->settings.behavior & PARSER_OUTPUT_NO_COLOR));
		array_free(parser->result);
		parser->result = new_result;
		parser->error = PARSER_ERROR_DIFFERENCES_FOUND;
	}

	free(lines_buf);
	array_free(lines);
	free(p.ses);
	free(p.lcs);
}

void
parser_output_dump_tokens(struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK) {
		return;
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
		case COMMENT:
			type = "comment";
			break;
		default:
			parser->error = PARSER_ERROR_UNHANDLED_TOKEN_TYPE;
			return;
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

	parser->error = PARSER_ERROR_OK;
}

void
parser_read_line(struct Parser *parser, char *line)
{
	if (parser->error != PARSER_ERROR_OK) {
		return;
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
				return;
			}
		}
	}

	if (strlcat(parser->inbuf, line, INBUF_SIZE) >= INBUF_SIZE) {
		parser->error = PARSER_ERROR_BUFFER_TOO_SMALL;
		return;
	}

	if (!will_continue) {
		parser_read_internal(parser);
		if (parser->error != PARSER_ERROR_OK) {
			return;
		}
		parser->lines.start = parser->lines.end;
		memset(parser->inbuf, 0, INBUF_SIZE);
	}

	parser->continued = will_continue;
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
		parser_read_line(parser, line);
		if (parser->error != PARSER_ERROR_OK) {
			free(line);
			return parser->error;
		}
	}

	free(line);

	return PARSER_ERROR_OK;
}

void
parser_read_internal(struct Parser *parser)
{
	if (parser->error != PARSER_ERROR_OK) {
		return;
	}

	char *buf = str_trim(parser->inbuf);
	size_t pos;

	pos = consume_comment(buf);
	if (pos > 0) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	} else if (is_empty_line(buf)) {
		parser_append_token(parser, COMMENT, buf);
		goto next;
	}

	if (parser->in_target) {
		pos = consume_conditional(buf);
		if (pos > 0) {
			free(parser->condname);
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

	pos = consume_conditional(buf);
	if (pos > 0) {
		free(parser->condname);
		parser->condname = NULL;
		char *tmp = str_substr_dup(buf, 0, pos);
		parser->condname = str_trim(tmp);
		free(tmp);

		parser_append_token(parser, CONDITIONAL_START, parser->condname);
		parser_append_token(parser, CONDITIONAL_TOKEN, parser->condname);
		parser_tokenize(parser, buf, CONDITIONAL_TOKEN, pos);
		parser_append_token(parser, CONDITIONAL_END, parser->condname);
		goto next;
	}

	pos = consume_target(buf);
	if (pos > 0) {
		parser->in_target = 1;
		free(parser->targetname);
		parser->targetname = xstrdup(buf);
		parser_append_token(parser, TARGET_START, buf);
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

	if (strlen(parser->inbuf) > 0) {
		parser_read_internal(parser);
		if (parser->error != PARSER_ERROR_OK) {
			return parser->error;
		}
	}

	if (parser->in_target) {
		parser_append_token(parser, TARGET_END, NULL);
	}

	// Set it now to avoid recursion in parser_edit()
	parser->read_finished = 1;

	if (parser->settings.behavior & PARSER_SANITIZE_COMMENTS &&
	    PARSER_ERROR_OK != parser_edit(parser, "refactor.sanitize-comments", NULL)) {
		return parser->error;
	}

	if (!(parser->settings.behavior & PARSER_KEEP_EOL_COMMENTS) &&
	    PARSER_ERROR_OK != parser_edit(parser, "refactor.sanitize-eol-comments", NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_COLLAPSE_ADJACENT_VARIABLES &&
	    PARSER_ERROR_OK != parser_edit(parser, "refactor.collapse-adjacent-variables", NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_SANITIZE_APPEND &&
	    PARSER_ERROR_OK != parser_edit(parser, "refactor.sanitize-append-modifier", NULL)) {
		return parser->error;
	}

	if (parser->settings.behavior & PARSER_DEDUP_TOKENS &&
	    PARSER_ERROR_OK != parser_edit(parser, "refactor.dedup-tokens", NULL)) {
		return parser->error;
	}

	if (PARSER_ERROR_OK != parser_edit(parser, "refactor.remove-consecutive-empty-lines", NULL)) {
		return parser->error;
	}

	return parser->error;
}

enum ParserError
parser_output_write_to_file(struct Parser *parser, FILE *fp)
{
	parser_output_prepare(parser);
	if (parser->error != PARSER_ERROR_OK &&
	    parser->error != PARSER_ERROR_DIFFERENCES_FOUND) {
		return parser->error;
	}

	int error = parser->error;

	int fd = fileno(fp);
	if (parser->settings.behavior & PARSER_OUTPUT_INPLACE) {
		if (lseek(fd, 0, SEEK_SET) < 0) {
			parser->error = PARSER_ERROR_IO;
			free(parser->error_msg);
			xasprintf(&parser->error_msg, "lseek: %s", strerror(errno));
			return parser->error;
		}
		if (ftruncate(fd, 0) < 0) {
			parser->error = PARSER_ERROR_IO;
			free(parser->error_msg);
			xasprintf(&parser->error_msg, "ftruncate: %s", strerror(errno));
			return parser->error;
		}
	}

	size_t len = array_len(parser->result);
	if (len == 0) {
		return error;
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
			free(parser->error_msg);
			xasprintf(&parser->error_msg, "writev: %s", strerror(errno));
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

	return error;
}

enum ParserError
parser_read_from_buffer(struct Parser *parser, const char *input, size_t len)
{
	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	char *buf, *bufp, *line;
	buf = bufp = xstrndup(input, len);
	while ((line = strsep(&bufp, "\n")) != NULL) {
		parser_read_line(parser, line);
		if (parser->error != PARSER_ERROR_OK) {
			break;
		}
	}
	free(buf);

	return parser->error;
}

void
parser_mark_for_gc(struct Parser *parser, struct Token *t)
{
	if (array_find(parser->tokengc, t, NULL, NULL) == -1) {
		array_append(parser->tokengc, t);
	}
}

void
parser_mark_edited(struct Parser *parser, struct Token *t)
{
	array_append(parser->edited, t);
}

enum ParserError
parser_edit(struct Parser *parser, const char *edit, const void *userdata) {
	struct ParserPluginInfo *info = parser_plugin_info(edit);
	if (info == NULL) {
		parser->error = PARSER_ERROR_EDIT_FAILED;
		free(parser->error_msg);
		xasprintf(&parser->error_msg, "cannot find %s plugin", edit);
		return parser->error;
	}

	return parser_edit_with_fn(parser, info->edit_func, userdata);
}

enum ParserError
parser_edit_with_fn(struct Parser *parser, ParserEditFn f, const void *userdata)
{
	if (!parser->read_finished) {
		parser_read_finish(parser);
	}

	if (parser->error != PARSER_ERROR_OK) {
		return parser->error;
	}

	enum ParserError error = PARSER_ERROR_OK;
	char *error_msg = NULL;
	struct Array *tokens = f(parser, parser->tokens, &error, &error_msg, userdata);
	if (tokens && tokens != parser->tokens) {
		array_free(parser->tokens);
		parser->tokens = tokens;
	}

	if (error != PARSER_ERROR_OK) {
		parser->error = error;
		free(parser->error_msg);
		parser->error_msg = error_msg;
		error_msg = parser_error_tostring(parser);
		free(parser->error_msg);
		parser->error_msg = error_msg;
		parser->error = PARSER_ERROR_EDIT_FAILED;
	}

	return parser->error;
}

struct ParserSettings parser_settings(struct Parser *parser)
{
	return parser->settings;
}

static void
parser_port_options_add_from_group(struct Parser *parser, const char *groupname)
{
	struct Array *optmulti = NULL;
	if (parser_lookup_variable_all(parser, groupname, &optmulti, NULL)) {
		for (size_t i = 0; i < array_len(optmulti); i++) {
			char *optgroupname = array_get(optmulti, i);
			if (array_find(parser->port_options_groups, optgroupname, str_compare, NULL) == -1) {
				array_append(parser->port_options_groups, xstrdup(optgroupname));
			}
			char *optgroupvar;
			xasprintf(&optgroupvar, "%s_%s", groupname, optgroupname);
			struct Array *opts = NULL;
			if (parser_lookup_variable_all(parser, optgroupvar, &opts, NULL)) {
				for (size_t i = 0; i < array_len(opts); i++) {
					char *opt = array_get(opts, i);
					if (array_find(parser->port_options, opt, str_compare, NULL) == -1) {
						array_append(parser->port_options, xstrdup(opt));
					}
				}
				array_free(opts);
			}
			free(optgroupvar);
		}
		array_free(optmulti);
	}
}

static void
parser_port_options_add_from_var(struct Parser *parser, const char *var)
{
	struct Array *optdefine = NULL;
	if (parser_lookup_variable_all(parser, var, &optdefine, NULL)) {
		for (size_t i = 0; i < array_len(optdefine); i++) {
			char *opt = array_get(optdefine, i);
			if (array_find(parser->port_options, opt, str_compare, NULL) == -1) {
				array_append(parser->port_options, xstrdup(opt));
			}
		}
		array_free(optdefine);
	}
}

void
parser_port_options(struct Parser *parser, struct Array **groups, struct Array **options)
{
	if (parser->port_options_looked_up) {
		if (groups) {
			*groups = parser->port_options_groups;
		}
		if (options) {
			*options = parser->port_options;
		}
		return;
	}

#define FOR_EACH_ARCH(f, var) \
	f(parser, var "_" "aarch64"); \
	f(parser, var "_" "amd64"); \
	f(parser, var "_" "arm"); \
	f(parser, var "_" "armv6"); \
	f(parser, var "_" "armv7"); \
	f(parser, var "_" "i386"); \
	f(parser, var "_" "mips"); \
	f(parser, var "_" "mips64"); \
	f(parser, var "_" "mips64el"); \
	f(parser, var "_" "mips64elhf"); \
	f(parser, var "_" "mips64hf"); \
	f(parser, var "_" "mipsel"); \
	f(parser, var "_" "mipselhf"); \
	f(parser, var "_" "mipsn32"); \
	f(parser, var "_" "powerpc"); \
	f(parser, var "_" "powerpc64"); \
	f(parser, var "_" "powerpcspe"); \
	f(parser, var "_" "riscv64"); \
	f(parser, var "_" "sparc64")

	parser_port_options_add_from_var(parser, "OPTIONS_DEFINE");
	FOR_EACH_ARCH(parser_port_options_add_from_var, "OPTIONS_DEFINE");

	parser_port_options_add_from_group(parser, "OPTIONS_GROUP");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_GROUP");

	parser_port_options_add_from_group(parser, "OPTIONS_MULTI");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_MULTI");

	parser_port_options_add_from_group(parser, "OPTIONS_RADIO");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_RADIO");

	parser_port_options_add_from_group(parser, "OPTIONS_SINGLE");
	FOR_EACH_ARCH(parser_port_options_add_from_group, "OPTIONS_SINGLE");

#undef FOR_EACH_ARCH

	parser->port_options_looked_up = 1;
	if (groups) {
		*groups = parser->port_options_groups;
	}
	if (options) {
		*options = parser->port_options;
	}
}

#if PORTFMT_SUBPACKAGES
struct Array *
parser_subpackages(struct Parser *parser)
{
	if (parser->subpackages_looked_up) {
		return parser->subpackages;
	}

	struct Array *subpkgs = NULL;
	if (parser_lookup_variable_all(parser, "SUBPACKAGES", &subpkgs, NULL)) {
		for (size_t i = 0; i < array_len(subpkgs); i++) {
			char *subpkg = array_get(subpkgs, i);
			if (array_find(parser->subpackages, subpkg, str_compare, NULL) == -1) {
				array_append(parser->subpackages, xstrdup(subpkg));
			}
		}
		array_free(subpkgs);
		subpkgs = NULL;
	}

	struct Array *options = NULL;
	parser_port_options(parser, NULL, &options);
	for (size_t i = 0; i < array_len(options); i++) {
		char *opt = array_get(options, i);
		char *var;
		xasprintf(&var, "%s_SUBPACKAGES", opt);
		if (parser_lookup_variable_all(parser, var, &subpkgs, NULL)) {
			for (size_t j = 0; j < array_len(subpkgs); j++) {
				char *subpkg = array_get(subpkgs, j);
				if (array_find(parser->subpackages, subpkg, str_compare, NULL) == -1) {
					array_append(parser->subpackages, xstrdup(subpkg));
				}
			}
			array_free(subpkgs);
		}
		free(var);
	}

	return parser->subpackages;
}
#endif

struct Target *
parser_lookup_target(struct Parser *parser, const char *name, struct Array **retval)
{
	struct Target *target = NULL;
	struct Array *tokens = array_new();
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case TARGET_START:
			array_truncate(tokens);
			/* fallthrough */
		case TARGET_COMMAND_START:
		case TARGET_COMMAND_TOKEN:
		case TARGET_COMMAND_END:
			if (strcmp(target_name(token_target(t)), name) == 0) {
				array_append(tokens, token_data(t));
			}
			break;
		case TARGET_END:
			if (strcmp(target_name(token_target(t)), name) == 0) {
				target = token_target(t);
				goto found;
			}
			break;
		default:
			break;
		}
	}

	array_free(tokens);

	if (retval) {
		*retval = NULL;
	}

	return NULL;

found:
	if (retval) {
		*retval = tokens;
	} else {
		array_free(tokens);
	}

	return target;
}

struct Variable *
parser_lookup_variable_internal(struct Parser *parser, const char *name, struct Array **retval, struct Array **comment, int cont)
{
	struct Variable *var = NULL;
	struct Array *tokens = array_new();
	struct Array *comments = array_new();
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *t = array_get(parser->tokens, i);
		switch (token_type(t)) {
		case VARIABLE_START:
			if (!cont) {
				array_truncate(tokens);
			}
			break;
		case VARIABLE_TOKEN:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				if (is_comment(t)) {
					array_append(comments, token_data(t));
				} else {
					array_append(tokens, token_data(t));
				}
			}
			break;
		case VARIABLE_END:
			if (strcmp(variable_name(token_variable(t)), name) == 0) {
				var = token_variable(t);
				if (!cont) {
					goto found;
				}
			}
			break;
		default:
			break;
		}
	}

	if (var) {
		goto found;
	}

	array_free(comments);
	array_free(tokens);

	if (comment) {
		*comment = NULL;
	}
	if (retval) {
		*retval = NULL;
	}

	return NULL;

found:
	if (comment) {
		*comment = comments;
	} else {
		array_free(comments);
	}
	if (retval) {
		*retval = tokens;
	} else {
		array_free(tokens);
	}

	return var;
}

struct Variable *
parser_lookup_variable(struct Parser *parser, const char *name, struct Array **retval, struct Array **comment)
{
	return parser_lookup_variable_internal(parser, name, retval, comment, 0);
}

struct Variable *
parser_lookup_variable_all(struct Parser *parser, const char *name, struct Array **retval, struct Array **comment)
{
	return parser_lookup_variable_internal(parser, name, retval, comment, 1);
}

struct Variable *
parser_lookup_variable_str(struct Parser *parser, const char *name, char **retval, char **comment)
{
	struct Array *comments;
	struct Array *tokens;
	struct Variable *var;
	if ((var = parser_lookup_variable(parser, name, &tokens, &comments)) == NULL) {
		return NULL;
	}

	if (comment) {
		*comment = str_join(comments, " ");
	}

	if (retval) {
		*retval = str_join(tokens, " ");
	}

	array_free(comments);
	array_free(tokens);

	return var;
}

enum ParserError
parser_merge(struct Parser *parser, struct Parser *subparser, enum ParserMergeBehavior settings)
{
	struct ParserPluginEdit params = { subparser, NULL, settings };
	enum ParserError error = parser_edit(parser, "edit.merge", &params);

	if (error == PARSER_ERROR_OK &&
	    parser->settings.behavior & PARSER_DEDUP_TOKENS) {
		error = parser_edit(parser, "refactor.dedup-tokens", NULL);
	}

	if (error == PARSER_ERROR_OK) {
		error = parser_edit(parser, "refactor.remove-consecutive-empty-lines", NULL);
	}

	return error;
}
