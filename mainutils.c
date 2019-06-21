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
#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "mainutils.h"
#include "parser.h"

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
enter_sandbox(FILE *in, FILE *out)
{
#if HAVE_CAPSICUM
	int fd_in = fileno(in);
	int fd_out = fileno(out);

	if (fd_in == fd_out) {
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
#if HAVE_PLEDGE
	if (pledge("stdio", NULL) == -1) {
		err(1, "pledge");
	}
#endif
}

int
read_common_args(int *argc, char ***argv, struct ParserSettings *settings)
{
	int ch;
	while ((ch = getopt(*argc, *argv, "adituw:")) != -1) {
		switch (ch) {
		case 'a':
			settings->behavior |= PARSER_SANITIZE_APPEND;
			break;
		case 'd':
			settings->behavior |= PARSER_OUTPUT_DUMP_TOKENS;
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
open_file(int *argc, char ***argv, struct ParserSettings *settings, FILE **fp_in, FILE **fp_out)
{
	if (*argc > 1 || ((settings->behavior & PARSER_OUTPUT_INPLACE) && *argc == 0)) {
		return 0;
	} else if (*argc == 1) {
		if (settings->behavior & PARSER_OUTPUT_INPLACE) {
			*fp_in = fopen(*argv[0], "rw");
			*fp_out = *fp_in;
			if (*fp_in == NULL) {
				return 0;
			}
		} else  {
			*fp_in = fopen(*argv[0], "r");
			if (*fp_in == NULL) {
				return 0;
			}
		}
	}

	*argc -= 1;
	*argv += 1;

	return 1;
}
