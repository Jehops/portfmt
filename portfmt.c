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
#endif
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
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

	struct Array *output;
	struct Array *result;
};

static size_t consume_token(struct Parser *, struct sbuf *, size_t, char, char, int);
static size_t consume_var(struct sbuf *);
static struct Parser *parser_new(void);
static void parser_append(struct Parser *, enum OutputType, struct sbuf *);
static void parser_enqueue_output(struct Parser *, struct sbuf *s);
static struct Output *parser_get_output(struct Parser *, size_t i);
static void parser_find_goalcols(struct Parser *);
static void parser_generate_output(struct Parser *);
static void parser_propagate_goalcol(struct Parser *, size_t, size_t, int);
static void parser_read(struct Parser *, const char *line);
static void parser_write(struct Parser *, int fd_out);
static void parser_reset(struct Parser *);
static void parser_tokenize(struct Parser *, struct sbuf *);

static void print_newline_array(struct Parser *, struct Array *);
static void print_token_array(struct Parser *, struct Array *);
static int tokcompare(const void *, const void *);
static void usage(void);

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

struct Parser *
parser_new()
{
	struct Parser *parser = calloc(1, sizeof(struct Parser));
	if (parser == NULL) {
		err(1, "calloc");
	}

	parser->result = array_new(sizeof(struct sbuf *));
	parser->output = array_new(sizeof(struct Output *));

	parser_reset(parser);

	return parser;
}

void
parser_append(struct Parser *parser, enum OutputType type, struct sbuf *v)
{
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
	struct Output *o = malloc(sizeof(struct Output));
	if (o == NULL) {
		err(1, "malloc");
	}
	o->type = type;
	o->data = data;
	o->var = var;
	o->goalcol = 0;
	array_append(parser->output, o);
}

void
parser_enqueue_output(struct Parser *parser, struct sbuf *s)
{
	if (!sbuf_done(s)) {
		sbuf_finishx(s);
	}
	array_append(parser->result, s);
}

struct Output *
parser_get_output(struct Parser *parser, size_t i)
{
	return array_get(parser->output, i);
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
parser_propagate_goalcol(struct Parser *parser, size_t start, size_t end, int moving_goalcol)
{
	moving_goalcol = MAX(16, moving_goalcol);
	for (size_t k = start; k <= end; k++) {
		struct Output *o = parser_get_output(parser, k);
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
	for (size_t i = 0; i < array_len(parser->output); i++) {
		struct Output *o = parser_get_output(parser, i);
		switch(o->type) {
		case OUTPUT_TOKENS:
			if (tokens_start == -1) {
				tokens_start = i;
			}
			tokens_end = i;

			struct sbuf *var = o->var;
			if (var && skip_goalcol(var)) {
				o->goalcol = indent_goalcol(var);
			} else {
				moving_goalcol = MAX(indent_goalcol(var), moving_goalcol);
			}
			break;
		case OUTPUT_COMMENT:
			/* Ignore comments in between variables and
			 * treat variables after them as part of the
			 * same block, i.e., indent them the same way.
			 */
			if (matches(RE_COMMENT, o->data, NULL)) {
				continue;
			}
			if (tokens_start != -1) {
				parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
				moving_goalcol = 0;
				last = i;
				tokens_start = -1;
			}
			break;
		case OUTPUT_INLINE_COMMENT:
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
	}
	if (tokens_start != -1) {
		parser_propagate_goalcol(parser, last, tokens_end, moving_goalcol);
	}
}

void
print_newline_array(struct Parser *parser, struct Array *arr) {
	struct Output *o = array_get(arr, 0);
	struct sbuf *start = assign_variable(o->var);
	/* Handle variables with empty values */
	if (array_len(arr) == 1 && (o->data == NULL || sbuf_len(o->data) == 0)) {
		parser_enqueue_output(parser, start);
		parser_enqueue_output(parser, sbuf_dupstr("\n"));
		return;
	}

	size_t ntabs = ceil((MAX(16, o->goalcol) - sbuf_len(start)) / 8.0);
	struct sbuf *sep = sbuf_dup(start);
	sbuf_cat(sep, repeat('\t', ntabs));

	struct sbuf *end = sbuf_dupstr(" \\\n");

	for (size_t i = 0; i < array_len(arr); i++) {
		struct Output *o = array_get(arr, i);
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
print_token_array(struct Parser *parser, struct Array *tokens)
{
	if (array_len(tokens) < 2) {
		print_newline_array(parser, tokens);
		return;
	}

	struct Array *arr = array_new(sizeof(struct Output *));
	struct Output *o = array_get(tokens, 0);
	struct sbuf *var = o->var;
	int wrapcol;
	if (ignore_wrap_col(var)) {
		wrapcol = 99999999;
	} else {
		wrapcol = WRAPCOL - o->goalcol;
	}

	struct sbuf *row = sbuf_dupstr(NULL);
	sbuf_finishx(row);

	struct Output *token;
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
				struct Output *o = malloc(sizeof(struct Output));
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
		struct Output *o = malloc(sizeof(struct Output));
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
parser_generate_output(struct Parser *parser) {
	struct Array *arr = array_new(sizeof(struct Output *));
	struct sbuf *last_var = NULL;
	for (size_t i = 0; i < array_len(parser->output); i++) {
		struct Output *o = array_get(parser->output, i);
		switch (o->type) {
		case OUTPUT_TOKENS:
			if (last_var == NULL || sbuf_cmp(o->var, last_var) != 0) {
				if (array_len(arr) > 0) {
					struct Output *arr0 = array_get(arr, 0);
					if (!ALL_UNSORTED || !leave_unsorted(arr0->var)) {
						array_sort(arr, tokcompare);
					}
					if (print_as_newlines(arr0->var)) {
						print_newline_array(parser, arr);
					} else {
						print_token_array(parser, arr);
					}
					array_truncate(arr);
				}
			}
			array_append(arr, o);
			break;
		case OUTPUT_COMMENT:
			if (array_len(arr) > 0) {
				struct Output *arr0 = array_get(arr, 0);
				if (!ALL_UNSORTED || !leave_unsorted(arr0->var)) {
					array_sort(arr, tokcompare);
				}
				if (print_as_newlines(arr0->var)) {
					print_newline_array(parser, arr);
				} else {
					print_token_array(parser, arr);
				}
				array_truncate(arr);
			}
			parser_enqueue_output(parser, o->data);
			parser_enqueue_output(parser, sbuf_dupstr("\n"));
			break;
		case OUTPUT_INLINE_COMMENT:
			parser_enqueue_output(parser, o->data);
			parser_enqueue_output(parser, sbuf_dupstr("\n"));
			break;
		default:
			errx(1, "Unhandled output type: %i", o->type);
		}
		last_var = o->var;
	}
	if (array_len(arr) > 0) {
		struct Output *arr0 = array_get(arr, 0);
		if (!ALL_UNSORTED || !leave_unsorted(arr0->var)) {
			array_sort(arr, tokcompare);
		}
		if (print_as_newlines(arr0->var)) {
			print_newline_array(parser, arr);
		} else {
			print_token_array(parser, arr);
		}
	}

	free(arr);
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
usage() {
	fprintf(stderr, "usage: portfmt [-i] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int fd_in = STDIN_FILENO;
	int fd_out = STDOUT_FILENO;
	int iflag = 0;
	while (getopt(argc, argv, "iuw:") != -1) {
		switch (optopt) {
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
	cap_rights_t rights;

	if (iflag) {
		cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_FTRUNCATE, CAP_SEEK);
		/* fdopen */ cap_rights_set(&rights, CAP_FCNTL);
		/* ??? */ cap_rights_set(&rights, CAP_FSTAT);
		if (cap_rights_limit(fd_out, &rights) < 0 && errno != ENOSYS) {
			err(1, "cap_rights_limit");
		}
	} else {
		cap_rights_init(&rights, CAP_WRITE);
		if (cap_rights_limit(fd_out, &rights) < 0 && errno != ENOSYS) {
			err(1, "cap_rights_limit");
		}

		cap_rights_init(&rights, CAP_READ);
		/* fdopen */ cap_rights_set(&rights, CAP_FCNTL);
		/* ??? */ cap_rights_set(&rights, CAP_FSTAT);
		if (cap_rights_limit(fd_in, &rights) < 0 && errno != ENOSYS) {
			err(1, "cap_rights_limit");
		}
	}

	cap_rights_init(&rights, CAP_WRITE);
	if (cap_rights_limit(STDERR_FILENO, &rights) && errno != ENOSYS) {
		err(1, "cap_rights_limit");
	}

	if (cap_enter() && errno != ENOSYS) {
		err(1, "cap_enter");
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

	close(fd_out);
	close(fd_in);

	free(line);
	free(parser);

	return 0;
}
