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
#if HAVE_SBUF
# include <sys/sbuf.h>
#endif
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "rules.h"
#include "util.h"

enum OutputType {
	OUTPUT_COMMENT,
	OUTPUT_INLINE_COMMENT,
	OUTPUT_TOKENS,
};

struct Output {
	enum OutputType type;
	struct sbuf *data;
	struct sbuf *var;
	int goalcol;
};

struct Parser {
	int in_target;
	size_t lineno;
	int skip;
	struct sbuf *varname;
	struct Output output[4096];
	size_t output_length;
};

static size_t consume_token(struct Parser *, struct sbuf *, size_t, char, char, int);
static size_t consume_var(struct sbuf *);
static void parser_append(struct Parser *, enum OutputType, struct sbuf *);
static void parser_find_goalcols(struct Parser *);
static void parser_output(struct Parser *);
static void parser_reset(struct Parser *);
static void parser_tokenize(struct Parser *, struct sbuf *);

static void print_newline_array(struct Output **, size_t);
static void print_token_array(struct Output **, size_t);
static void propagate_goalcol(struct Output *, size_t, size_t, int);
static int tokcompare(const void *, const void *);
static void usage(void);

static struct sbuf *assign_variable(struct sbuf *);
static char *repeat(char c, size_t n);

static int ALL_UNSORTED = 1;
static int WRAPCOL = 80;

size_t
consume_token(struct Parser *parser, struct sbuf *line, size_t pos, char startchar, char endchar, int eol_ok) {
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

static void
parser_append(struct Parser *parser, enum OutputType type, struct sbuf *v)
{
	if (parser->output_length >= sizeof(parser->output)) {
		errx(1, "output array too small");
	}
	struct sbuf *var = NULL;
	if (parser->varname) {
		var = sbuf_dup(parser->varname);
		sbuf_finishx(var);
	}
	struct sbuf *data = NULL;
	if (v) {
		data = sbuf_dup(v);
		sbuf_finishx(data);
	}
	parser->output[parser->output_length++] = (struct Output){
		type, data, var, 0
	};
}

void
parser_tokenize(struct Parser *parser, struct sbuf *buf) {
	struct sbuf *line = sub(RE_BACKSLASH_AT_END, NULL, buf);
	ssize_t pos = consume_var(line);
	if (pos != 0) {
		if (pos > sbuf_len(line)) {
			errx(1, "parser->varname too small");
		}
		if (parser->varname) {
			sbuf_delete(parser->varname);
		}
		parser->varname = sbuf_substr_dup(line, 0, pos - 1);
		sbuf_finishx(parser->varname);
	}

	int dollar = 0;
	int escape = 0;
	ssize_t start = pos;
	char *linep = sbuf_data(line);
	struct sbuf *token = NULL;
	ssize_t i = pos;
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
			if (c == '{') {
				i = consume_token(parser, line, i, '{', '}', 0);
				dollar = 0;
			} else if (c == '$') {
				dollar = 0;
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
					parser_append(parser, OUTPUT_TOKENS, token);
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
					parser_append(parser, OUTPUT_TOKENS, token);
				} else {
					parser_append(parser, OUTPUT_INLINE_COMMENT, token);
					struct sbuf *tmp = sbuf_dupstr("");
					sbuf_finish(tmp);
					parser_append(parser, OUTPUT_TOKENS, tmp);
					sbuf_delete(tmp);
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
	if (sbuf_strcmp(token, "") != 0 && sbuf_strcmp(token, "\\") != 0) {
		parser_append(parser, OUTPUT_TOKENS, token);
	}
	sbuf_delete(token);
	token = NULL;

cleanup:
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
propagate_goalcol(struct Output *output, size_t start, size_t end, int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		if (output[k].var && !skip_goalcol(output[k].var)) {
			output[k].goalcol = moving_goalcol;
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
	for (size_t i = 0; i < parser->output_length; i++) {
		switch(parser->output[i].type) {
		case OUTPUT_TOKENS:
			if (tokens_start == -1) {
				tokens_start = i;
			}
			tokens_end = i;

			struct sbuf *var = parser->output[i].var;
			if (var && skip_goalcol(var)) {
				parser->output[i].goalcol = indent_goalcol(var);
			} else {
				moving_goalcol = MAX(indent_goalcol(var), moving_goalcol);
			}
			break;
		case OUTPUT_COMMENT:
			/* Ignore comments in between variables and
			 * treat variables after them as part of the
			 * same block, i.e., indent them the same way.
			 */
			if (matches(RE_COMMENT, parser->output[i].data, NULL)) {
				continue;
			}
			if (tokens_start != -1) {
				propagate_goalcol(parser->output, last, tokens_end, moving_goalcol);
				moving_goalcol = 0;
				last = i;
				tokens_start = -1;
			}
			break;
		case OUTPUT_INLINE_COMMENT:
			break;
		default:
			errx(1, "Unhandled output type: %i", parser->output[i].type);
		}
	}
	if (tokens_start != -1) {
		propagate_goalcol(parser->output, last, tokens_end, moving_goalcol);
	}
}

char *
repeat(char c, size_t n)
{
	static char buf[128];
	assert (n <= sizeof(buf));
	if (n > 0) {
		for (size_t i = 0; i < n; i++) {
			buf[i] = c;
		}
		buf[n] = '\0';
	} else {
		buf[0] = '\0';
	}
	return buf;
}

struct sbuf*
assign_variable(struct sbuf *var)
{
	struct sbuf *r = sbuf_dup(var);
	sbuf_putc(r, '=');
	sbuf_finishx(r);
	return r;
}

void
print_newline_array(struct Output **arr, size_t arrlen) {
	struct sbuf *start = assign_variable(arr[0]->var);
	/* Handle variables with empty values */
	if (arrlen == 1 && (arr[0]->data == NULL || sbuf_len(arr[0]->data) == 0)) {
		printf("%s\n", sbuf_data(start));
		sbuf_delete(start);
		return;
	}

	size_t ntabs = ceil((MAX(16, arr[0]->goalcol) - sbuf_len(start)) / 8.0);
	struct sbuf *sep = sbuf_dup(start);
	sbuf_cat(sep, repeat('\t', ntabs));
	sbuf_finishx(sep);

	struct sbuf *end = sbuf_dupstr(" \\\n");
	sbuf_finishx(end);

	for (size_t i = 0; i < arrlen; i++) {
		struct sbuf *line = arr[i]->data;
		if (!line || sbuf_len(line) == 0) {
			continue;
		}
		if (i == arrlen - 1) {
			sbuf_delete(end);
			end = sbuf_dupstr("\n");
			sbuf_finishx(end);
		}
		printf("%s%s%s", sbuf_data(sep), sbuf_data(line), sbuf_data(end));
		if (i == 0) {
			sbuf_delete(sep);
			ntabs = ceil(MAX(16, arr[i]->goalcol) / 8.0);
			sep = sbuf_dupstr(repeat('\t', ntabs));
			sbuf_finishx(sep);
		}
	}

	sbuf_delete(end);
	sbuf_delete(sep);
	sbuf_delete(start);
}

int
tokcompare(const void *a, const void *b)
{
	struct Output *ao = *(struct Output**)a;
	struct Output *bo = *(struct Output**)b;

	if (sbuf_strcmp(ao->var, "USE_QT") == 0 &&
	    sbuf_strcmp(bo->var, "USE_QT") == 0) {
		return compare_use_qt(ao->data, bo->data);
	} else if (matches(RE_LICENSE_PERMS, ao->var, NULL) &&
		   matches(RE_LICENSE_PERMS, bo->var, NULL)) {
		return compare_license_perms(ao->data, bo->data);
	} else if (matches(RE_PLIST_FILES, ao->var, NULL) &&
		   matches(RE_PLIST_FILES, bo->var, NULL)) {
		/* Ignore plist keywords */
		struct sbuf *as = sub(RE_PLIST_KEYWORDS, "", ao->data);
		struct sbuf *bs = sub(RE_PLIST_KEYWORDS, "", bo->data);
		int retval = strcasecmp(sbuf_data(as), sbuf_data(bs));
		sbuf_delete(as);
		sbuf_delete(bs);
		return retval;
	}

#if 0
	# Hack to treat something like ${PYTHON_PKGNAMEPREFIX} or
	# ${RUST_DEFAULT} as if they were PYTHON_PKGNAMEPREFIX or
	# RUST_DEFAULT for the sake of approximately sorting them
	# correctly in *_DEPENDS.
	gsub(/[\$\{\}]/, "", a)
	gsub(/[\$\{\}]/, "", b)
#endif

	return strcasecmp(sbuf_data(ao->data), sbuf_data(bo->data));
}

void
print_token_array(struct Output **tokens, size_t tokenslen) {
	static struct Output *arr[4096] = {};

	if (tokenslen < 2) {
		print_newline_array(tokens, tokenslen);
		return;
	}

	struct sbuf *var = tokens[0]->var;
	int wrapcol;
	if (ignore_wrap_col(var)) {
		wrapcol = 99999999;
	} else {
		wrapcol = WRAPCOL - tokens[0]->goalcol;
	}

	struct sbuf *row = sbuf_dupstr(NULL);
	sbuf_finishx(row);

	size_t arrlen = 0;
	struct Output *token;
	for (size_t i = 0; i < tokenslen; i++) {
		token = tokens[i];
		if (sbuf_len(token->data) == 0) {
			continue;
		}
		if ((sbuf_len(row) + sbuf_len(token->data)) > wrapcol) {
			if (sbuf_len(row) == 0) {
				arr[arrlen++] = token;
				continue;
			} else {
				struct Output *o = malloc(sizeof(struct Output));
				o->data = row;
				o->var = token->var;
				o->goalcol = token->goalcol;
				arr[arrlen++] = o;
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
	if (sbuf_len(row) > 0 && arrlen < tokenslen) {
		struct Output *o = malloc(sizeof(struct Output));
		o->data = row;
		o->var = token->var;
		o->goalcol = token->goalcol;
		arr[arrlen++] = o;
	}
	print_newline_array(arr, arrlen);
}

void
parser_output(struct Parser *parser) {
	static struct Output *arr[4096];

	struct sbuf *last_var = NULL;
	size_t arrlen = 0;
	for (size_t i = 0; i < parser->output_length; i++) {
		switch (parser->output[i].type) {
		case OUTPUT_TOKENS:
			if (last_var == NULL || sbuf_cmp(parser->output[i].var, last_var) != 0) {
				if (arrlen > 0) {
					if (!ALL_UNSORTED || !leave_unsorted(arr[0]->var)) {
						qsort(&arr, arrlen, sizeof(struct Output *), tokcompare);
					}
					if (print_as_newlines(arr[0]->var)) {
						print_newline_array(arr, arrlen);
					} else {
						print_token_array(arr, arrlen);
					}
					arrlen = 0;
				}
			}
			arr[arrlen++] = &parser->output[i];
			break;
		case OUTPUT_COMMENT:
			if (arrlen > 0) {
				if (!ALL_UNSORTED || !leave_unsorted(arr[0]->var)) {
					qsort(&arr, arrlen, sizeof(struct Output *), tokcompare);
				}
				if (print_as_newlines(arr[0]->var)) {
					print_newline_array(arr, arrlen);
				} else {
					print_token_array(arr, arrlen);
				}
				arrlen = 0;
			}
			printf("%s\n", sbuf_data(parser->output[i].data));
			break;
		case OUTPUT_INLINE_COMMENT:
			printf("%s\n", sbuf_data(parser->output[i].data));
			break;
		default:
			errx(1, "Unhandled output type: %i", parser->output[i].type);
		}
		last_var = parser->output[i].var;
	}
	if (arrlen > 0) {
		if (!ALL_UNSORTED || !leave_unsorted(arr[0]->var)) {
			qsort(&arr, arrlen, sizeof(struct Output *), tokcompare);
		}
		if (print_as_newlines(arr[0]->var)) {
			print_newline_array(arr, arrlen);
		} else {
			print_token_array(arr, arrlen);
		}
	}
}

void
usage() {
	fprintf(stderr, "usage: portfmt [-i] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	compile_regular_expressions();

	struct Parser *parser = calloc(1, sizeof(struct Parser));
	if (parser == NULL) {
		err(1, "calloc");
	}
	parser_reset(parser);

	while (getopt(argc, argv, "iuw:") != -1) {
		switch (optopt) {
		case 'i':
			//iflag = 1;
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

	ssize_t linelen;
	size_t linecap = 0;
	char *line = NULL;
	while ((linelen = getline(&line, &linecap, stdin)) > 0) {
		parser->lineno++;
		struct sbuf *buf = sbuf_dupstr(line);
		sbuf_trim(buf);
		sbuf_finishx(buf);

		if (matches(RE_EMPTY_LINE, buf, NULL)) {
			parser->skip = 1;
			parser->in_target = 0;
		} else if (matches(RE_TARGET, buf, NULL) && !matches(RE_TARGET_2, buf, NULL)) {
			parser->skip = 1;
			parser->in_target = 1;
		} else if (matches(RE_COMMENT, buf, NULL) || matches(RE_CONDITIONAL, buf, NULL) || parser->in_target) {
			parser->skip = 1;
			if (matches(RE_BACKSLASH_AT_END, buf, NULL) || matches(RE_CONDITIONAL, buf, NULL)) {
				parser->skip++;
			}
		} else if (matches(RE_VAR, buf, NULL)) {
			parser_reset(parser);
		}

		if (parser->skip) {
			parser_append(parser, OUTPUT_COMMENT, buf);
			if (!matches(RE_BACKSLASH_AT_END, buf, NULL) && !matches(RE_CONDITIONAL, buf, NULL)) {
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

	parser_find_goalcols(parser);
	parser_output(parser);

	free(line);
	free(parser);

	return 0;
}
