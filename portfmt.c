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
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
#include "rules.h"
#include "util.h"
#include "variable.h"

enum TokenType {
	COMMENT = 0,
	CONDITIONAL,
	EMPTY,
	INLINE_COMMENT,
	TARGET,
	TOKEN,
	PORT_MK,
	PORT_OPTIONS_MK,
	PORT_PRE_MK,
};

struct Token {
	enum TokenType type;
	struct sbuf *data;
	struct Variable *var;
	int goalcol;
	size_t lineno;
};

struct Parser {
	int in_target;
	size_t lineno;
	int skip;
	struct sbuf *varname;

	struct Array *tokens;
	struct Array *result;
};

static size_t consume_token(struct Parser *, struct sbuf *, size_t, char, char, int);
static size_t consume_var(struct sbuf *);
static struct Parser *parser_new(void);
static void parser_append_token(struct Parser *, enum TokenType, struct sbuf *);
static void parser_enqueue_output(struct Parser *, struct sbuf *);
static struct Token *parser_get_token(struct Parser *, size_t);
static void parser_find_goalcols(struct Parser *);
static void parser_generate_output_helper(struct Parser *, struct Array *);
static void parser_generate_output(struct Parser *);
static void parser_dump_tokens(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read(struct Parser *, const char *line);
static void parser_write(struct Parser *, int);
static void parser_reset(struct Parser *);
static void parser_tokenize(struct Parser *, struct sbuf *);

static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
static int tokcompare(const void *, const void *);
static void usage(void);

static int ALL_UNSORTED = 0;
static int WRAPCOL = 80;

size_t
consume_token(struct Parser *parser, struct sbuf *line, size_t pos,
	      char startchar, char endchar, int eol_ok)
{
	char *linep = sbuf_data(line);
	int counter = 0;
	int escape = 0;
	ssize_t i = pos;
	for (; i <= sbuf_len(line); i++) {
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
		errx(1, "tokenizer: %zu: expected %c", parser->lineno, endchar);
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

struct Parser *
parser_new()
{
	struct Parser *parser = calloc(1, sizeof(struct Parser));
	if (parser == NULL) {
		err(1, "calloc");
	}

	parser->result = array_new(sizeof(struct sbuf *));
	parser->tokens = array_new(sizeof(struct Token *));

	parser_reset(parser);

	return parser;
}

void
parser_append_token(struct Parser *parser, enum TokenType type, struct sbuf *v)
{
	struct Variable *var = NULL;
	if (parser->varname) {
		var = variable_new(parser->varname);
	}
	struct sbuf *data = NULL;
	if (v) {
		data = sbuf_dup(v);
		sbuf_finishx(data);
	}
	struct Token *o = malloc(sizeof(struct Token));
	if (o == NULL) {
		err(1, "malloc");
	}
	o->type = type;
	o->data = data;
	o->var = var;
	o->goalcol = 0;
	o->lineno = parser->lineno;
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
parser_tokenize(struct Parser *parser, struct sbuf *line)
{
	ssize_t pos = consume_var(line);
	if (pos != 0) {
		if (pos > sbuf_len(line)) {
			errx(1, "parser->varname too small");
		}
		if (parser->varname) {
			sbuf_delete(parser->varname);
		}
		parser->varname = sbuf_substr_dup(line, 0, pos);
		sbuf_finishx(parser->varname);
	}

	int dollar = 0;
	int escape = 0;
	ssize_t start = pos;
	char *linep = sbuf_data(line);
	struct sbuf *token = NULL;
	ssize_t i = pos;
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
				errx(1, "tokenizer: %zu: expected {", parser->lineno);
			}
		} else {
			if (c == ' ' || c == '\t') {
				struct sbuf *tmp = sbuf_substr_dup(line, start, i);
				sbuf_finishx(tmp);
				token = sbuf_strip_dup(tmp);
				sbuf_finishx(token);
				sbuf_delete(tmp);
				if (sbuf_strcmp(token, "") != 0 && sbuf_strcmp(token, "\\") != 0) {
					parser_append_token(parser, TOKEN, token);
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
					parser_append_token(parser, TOKEN, token);
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
	/* Ignore backslash at end of line */
	if (escape) {
		i--;
		queued_tokens++;
	}
	struct sbuf *tmp = sbuf_substr_dup(line, start, i);
	sbuf_finishx(tmp);
	token = sbuf_strip_dup(tmp);
	sbuf_finishx(token);
	sbuf_delete(tmp);
	if (sbuf_strcmp(token, "") != 0) {
		parser_append_token(parser, TOKEN, token);
		queued_tokens++;
	}

	sbuf_delete(token);
	token = NULL;
cleanup:
	if (queued_tokens == 0) {
		parser_append_token(parser, EMPTY, NULL);
	}
	sbuf_delete(line);
}

void
parser_reset(struct Parser *parser)
{
	parser->in_target = 0;
	if (parser->varname) {
		sbuf_delete(parser->varname);
	}
	parser->varname = NULL;
}

void
parser_propagate_goalcol(struct Parser *parser, size_t start, size_t end,
			 int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		struct Token *o = parser_get_token(parser, k);
		if (o->var && !skip_goalcol(o->var)) {
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
		switch(o->type) {
		case TOKEN:
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
		case COMMENT:
		case CONDITIONAL:
		case PORT_MK:
		case PORT_PRE_MK:
		case PORT_OPTIONS_MK:
		case TARGET:
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
	struct sbuf *start = variable_tostring(o->var);
	size_t ntabs = ceil((MAX(16, o->goalcol) - sbuf_len(start)) / 8.0);
	struct sbuf *sep = sbuf_dup(start);
	sbuf_cat(sep, repeat('\t', ntabs));

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
		if (i == 0) {
			ntabs = ceil(MAX(16, o->goalcol) / 8.0);
			sep = sbuf_dupstr(repeat('\t', ntabs));
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
	if (ignore_wrap_col(o->var)) {
		wrapcol = 99999999;
	} else {
		wrapcol = WRAPCOL - o->goalcol;
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
				struct Token *o = malloc(sizeof(struct Token));
				if (o == NULL) {
					err(1, "malloc");
				}
				o->data = row;
				o->var = token->var;
				o->goalcol = token->goalcol;
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
		struct Token *o = malloc(sizeof(struct Token));
		if (o == NULL) {
			err(1, "malloc");
		}
		o->data = row;
		o->var = token->var;
		o->goalcol = token->goalcol;
		array_append(arr, o);
	}
	print_newline_array(parser, arr);
	free(arr);
}

void
parser_generate_output_helper(struct Parser *parser, struct Array *arr)
{
	if (array_len(arr) == 0) {
		return;
	}
	struct Token *arr0 = array_get(arr, 0);
	if (!ALL_UNSORTED && !leave_unsorted(arr0->var)) {
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
	struct Array *arr = array_new(sizeof(struct Token *));
	struct Variable *last_var = NULL;
	int after_port_options_mk = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		switch (o->type) {
		case TOKEN:
			if (last_var == NULL || variable_cmp(o->var, last_var) != 0) {
				parser_generate_output_helper(parser, arr);
			}
			array_append(arr, o);
			break;
		case PORT_OPTIONS_MK:
			after_port_options_mk = 1;
		case COMMENT:
		case CONDITIONAL:
		case PORT_MK:
		case PORT_PRE_MK:
		case TARGET:
			parser_generate_output_helper(parser, arr);
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
		last_var = o->var;
	}
	parser_generate_output_helper(parser, arr);
	free(arr);
}

void
parser_dump_tokens(struct Parser *parser)
{
	ssize_t maxvarlen = 0;
	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		if (o->type == TOKEN && o->var) {
			struct sbuf *var = variable_tostring(o->var);
			maxvarlen = MAX(maxvarlen, sbuf_len(var));
			sbuf_delete(var);
		}
	}

	for (size_t i = 0; i < array_len(parser->tokens); i++) {
		struct Token *o = array_get(parser->tokens, i);
		const char *type;
		switch (o->type) {
		case TOKEN:
			type = "token";
			break;
		case TARGET:
			type = "target";
			break;
		case CONDITIONAL:
			type = "conditional";
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
		if (o->type == TOKEN && o->var) {
			var = variable_tostring(o->var);
			len = maxvarlen - sbuf_len(var);
		} else {
			len = maxvarlen - 1;
		}
		printf("%-15s %4zu %s", type, o->lineno, var ? sbuf_data(var) : "-");
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
parser_read(struct Parser *parser, const char *line)
{
	parser->lineno++;
	struct sbuf *buf = sbuf_dupstr(line);
	sbuf_trim(buf);
	sbuf_finishx(buf);

	if (matches(RE_EMPTY_LINE, buf, NULL)) {
		parser->skip = 1;
		parser->in_target = 0;
	} else if (matches(RE_TARGET, buf, NULL)) {
		parser->skip = 1;
		parser->in_target = 1;
	} else if (sbuf_startswith(buf, "#") || matches(RE_CONDITIONAL, buf, NULL) || parser->in_target) {
		parser->skip = 1;
		if (sbuf_endswith(buf, "\\") || matches(RE_CONDITIONAL, buf, NULL)) {
			parser->skip++;
		}
	} else if (matches(RE_VAR, buf, NULL)) {
		parser_reset(parser);
	}

	if (parser->skip) {
		if (parser->in_target) {
			parser_append_token(parser, TARGET, buf);
		} else if (matches(RE_CONDITIONAL, buf, NULL)) {
			if (sbuf_endswith(buf, "<bsd.port.options.mk>")) {
				parser_append_token(parser, PORT_OPTIONS_MK, buf);
			} else if (sbuf_endswith(buf, "<bsd.port.pre.mk>")) {
				parser_append_token(parser, PORT_PRE_MK, buf);
			} else if (sbuf_endswith(buf, "<bsd.port.post.mk>") ||
				   sbuf_endswith(buf, "<bsd.port.mk>")) {
				parser_append_token(parser, PORT_MK, buf);
			} else {
				parser_append_token(parser, CONDITIONAL, buf);
			}
		} else {
			parser_append_token(parser, COMMENT, buf);
		}
		if (!sbuf_endswith(buf, "\\") && !matches(RE_CONDITIONAL, buf, NULL)) {
			parser->skip--;
		}
	} else {
		parser_tokenize(parser, buf);
		if (parser->varname == NULL) {
			errx(1, "parser error on line %zu", parser->lineno);
		}
	}
	sbuf_delete(buf);
}

void
parser_write(struct Parser *parser, int fd)
{
	struct iovec *iov = reallocarray(NULL, array_len(parser->result),
					 sizeof(struct iovec));
	if (iov == NULL) {
		err(1, "reallocarray");
	}
	for (size_t i = 0; i < array_len(parser->result); i++) {
		struct sbuf *s = array_get(parser->result, i);
		iov[i].iov_base = sbuf_data(s);
		iov[i].iov_len = sbuf_len(s);
	}
	if (writev(fd, iov, array_len(parser->result)) < 0) {
		err(1, "writev");
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
	fprintf(stderr, "usage: portfmt [-i] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int fd_in = STDIN_FILENO;
	int fd_out = STDOUT_FILENO;
	int dflag = 0;
	int iflag = 0;
	while (getopt(argc, argv, "diuw:") != -1) {
		switch (optopt) {
		case 'd':
			dflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'u':
			ALL_UNSORTED = 1;
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

	struct Parser *parser = parser_new();
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
		parser_read(parser, line);
	}

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
	free(parser);

	return 0;
}
