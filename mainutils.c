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

#if HAVE_CAPSICUM
# include <sys/capsicum.h>
# include "capsicum_helpers.h"
#endif
#include <sys/stat.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "mainutils.h"
#include "parser.h"
#include "util.h"

int
can_use_colors(FILE *fp)
{
	if (getenv("CLICOLOR_FORCE") == NULL && isatty(fileno(fp)) == 0) {
		return 0;
	}

	// NO_COLOR takes precedence even when CLICOLOR_FORCE is set
	if (getenv("NO_COLOR") != NULL) {
		return 0;
	}

	return 1;
}

void
enter_sandbox()
{
#if HAVE_CAPSICUM
	if (caph_limit_stderr() < 0) {
		err(1, "caph_limit_stderr");
	}

	if (caph_enter() < 0) {
		err(1, "caph_enter");
	}
#endif
#if HAVE_PLEDGE
	if (pledge("stdio", NULL) == -1) {
		err(1, "pledge");
	}
#endif
}

int
read_common_args(int *argc, char ***argv, struct ParserSettings *settings, const char *optstr, struct Array *expressions)
{
	int ch;
	while ((ch = getopt(*argc, *argv, optstr)) != -1) {
		switch (ch) {
		case 'D':
			settings->behavior |= PARSER_OUTPUT_DIFF;
			break;
		case 'd':
			settings->behavior |= PARSER_OUTPUT_DUMP_TOKENS;
			break;
		case 'e':
			if (expressions) {
				array_append(expressions, xstrdup(optarg));
			} else {
				return 0;
			}
			break;
		case 'i':
			settings->behavior |= PARSER_OUTPUT_INPLACE;
			break;
		case 't':
			settings->behavior |= PARSER_FORMAT_TARGET_COMMANDS;
			break;
		case 'u':
			settings->behavior |= PARSER_UNSORTED_VARIABLES;
			break;
		case 'w': {
			const char *errstr = NULL;
			settings->wrapcol = strtonum(optarg, -1, INT_MAX, &errstr);
			if (errstr != NULL) {
				errx(1, "strtonum: %s", errstr);
			}
			break;
		} default:
			return 0;
		}
	}
	*argc -= optind;
	*argv += optind;

	if (settings->behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		settings->behavior &= ~PARSER_OUTPUT_INPLACE;
	}

	return 1;
}

int
open_file(int *argc, char ***argv, struct ParserSettings *settings, FILE **fp_in, FILE **fp_out, int keep_stdin_open)
{
#if HAVE_CAPSICUM
	closefrom(STDERR_FILENO + 1);
#endif

	if (*argc > 1 || ((settings->behavior & PARSER_OUTPUT_INPLACE) && *argc == 0)) {
		return 0;
	} else if (*argc == 1) {
		struct stat sb;
		if (stat(*argv[0], &sb) == -1) {
			*fp_in = NULL;
			return 0;
		}
		char *filename;
		if (S_ISDIR(sb.st_mode)) {
			xasprintf(&filename, "%s/Makefile", *argv[0]);
		} else {
			filename = xstrdup(*argv[0]);
		}
		settings->filename = filename;
		if (settings->behavior & PARSER_OUTPUT_INPLACE) {
			if (!keep_stdin_open) {
				close(STDIN_FILENO);
			}
			close(STDOUT_FILENO);

			*fp_in = fopen(filename, "r+");
			*fp_out = *fp_in;
			if (*fp_in == NULL) {
				return 0;
			}
#if HAVE_CAPSICUM
			if (caph_limit_stream(fileno(*fp_in), CAPH_READ | CAPH_WRITE | CAPH_FTRUNCATE) < 0) {
				return 0;
			}
#endif
		} else  {
			if (!keep_stdin_open) {
				close(STDIN_FILENO);
			}
			*fp_in = fopen(filename, "r");
			if (*fp_in == NULL) {
				return 0;
			}
#if HAVE_CAPSICUM
			if (caph_limit_stream(fileno(*fp_in), CAPH_READ) < 0) {
				return 0;
			}
			if (caph_limit_stdio() < 0) {
				return 0;
			}
#endif
		}
	} else {
#if HAVE_CAPSICUM
		if (caph_limit_stdio() < 0) {
			return 0;
		}
#endif
	}

	*argc -= 1;
	*argv += 1;

	return 1;
}
