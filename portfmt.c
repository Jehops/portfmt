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
#define _WITH_GETLINE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "parser.h"

enum PorteditCommand {
	PORTEDIT_BUMP_REVISION,
	PORTEDIT_GET_VARIABLE,
	PORTEDIT_SET_VARIABLE,
	PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES,
	PORTEDIT_PRIVATE_LINT_ORDER,
	PORTEDIT_PRIVATE_LIST_VARIABLES,
};

struct Portedit {
	enum PorteditCommand cmd;
	char **argv;
	int argc;
};

static void usage(void);

void
usage()
{
	if (strcmp(getprogname(), "portfmt") == 0) {
		fprintf(stderr, "usage: portfmt [-aditu] [-w wrapcol] [Makefile]\n");
	} else {
		fprintf(stderr, "usage: portedit [-aditu] [-w wrapcol] bump-revision|get [Makefile]\n");
	}
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	int fd_in = STDIN_FILENO;
	int fd_out = STDOUT_FILENO;
	int iflag = 0;
	struct ParserSettings settings;

	parser_init_settings(&settings);
	settings.behavior = PARSER_COLLAPSE_ADJACENT_VARIABLES |
		PARSER_OUTPUT_REFORMAT;

	if (strcmp(getprogname(), "portedit") == 0) {
		settings.behavior |= PARSER_OUTPUT_EDITED;
		settings.behavior |= PARSER_KEEP_EOL_COMMENTS;
	}

	int ch;
	while ((ch = getopt(argc, argv, "adituw:")) != -1) {
		switch (ch) {
		case 'a':
			settings.behavior |= PARSER_SANITIZE_APPEND;
			break;
		case 'd':
			settings.behavior |= PARSER_OUTPUT_DUMP_TOKENS;
			break;
		case 'i':
			iflag = 1;
			break;
		case 't':
			settings.behavior |= PARSER_FORMAT_TARGET_COMMANDS;
			break;
		case 'u':
			settings.behavior |= PARSER_UNSORTED_VARIABLES;
			break;
		case 'w': {
			const char *errstr = NULL;
			settings.wrapcol = strtonum(optarg, -1, INT_MAX, &errstr);
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

	struct Portedit edit;
	if (settings.behavior & PARSER_OUTPUT_EDITED) {
		if (argc < 1) {
			usage();
		}
		if (strcmp(argv[0], "bump-revision") == 0) {
			edit.cmd = PORTEDIT_BUMP_REVISION;
			edit.argc = 1;
			edit.argv = argv;
		} else if (strcmp(argv[0], "get") == 0 && argc > 0) {
			edit.cmd = PORTEDIT_GET_VARIABLE;
			edit.argc = 2;
			edit.argv = argv;
			settings.behavior |= PARSER_OUTPUT_RAWLINES;
		} else if (strcmp(argv[0], "set") == 0 && argc > 1) {
			edit.cmd = PORTEDIT_SET_VARIABLE;
			edit.argc = 3;
			edit.argv = argv;
		} else if (strcmp(argv[0], "__private__lint-order") == 0 && argc > 0) {
			edit.cmd = PORTEDIT_PRIVATE_LINT_ORDER;
			edit.argc = 1;
			edit.argv = argv;
		} else if (strcmp(argv[0], "__private__list-unknown-variables") == 0 && argc > 0) {
			edit.cmd = PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES;
			edit.argc = 1;
			edit.argv = argv;
		} else if (strcmp(argv[0], "__private__list-variables") == 0 && argc > 0) {
			edit.cmd = PORTEDIT_PRIVATE_LIST_VARIABLES;
			edit.argc = 1;
			edit.argv = argv;
		} else {
			usage();
		}
		argc -= edit.argc;
		argv += edit.argc;
	}

	if (settings.behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		iflag = 0;
	}

	if (argc > 1 || (iflag && argc == 0)) {
		usage();
	} else if (argc == 1) {
		fd_in = open(argv[0], iflag ? O_RDWR : O_RDONLY);
		if (fd_in < 0) {
			err(1, "open");
		}
		if (iflag) {
			fd_out = fd_in;
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
		}
	}

#if HAVE_CAPSICUM
	if (iflag) {
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

	struct Parser *parser = parser_new(&settings);

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

	if (settings.behavior & PARSER_OUTPUT_EDITED) {
		switch (edit.cmd) {
		case PORTEDIT_BUMP_REVISION:
			parser_edit(parser, edit_bump_revision, NULL);
			break;
		case PORTEDIT_GET_VARIABLE:
			parser_edit(parser, edit_output_variable_value, edit.argv[1]);
			break;
		case PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES:
			parser_edit(parser, edit_output_unknown_variables, NULL);
			break;
		case PORTEDIT_PRIVATE_LIST_VARIABLES:
			parser_output_variable_order(parser);
			break;
		case PORTEDIT_PRIVATE_LINT_ORDER:
			parser_output_linted_variable_order(parser);
			break;
		default:
			break;
		}
	}

	parser_output_prepare(parser);
	if (iflag) {
		if (lseek(fd_out, 0, SEEK_SET) < 0) {
			err(1, "lseek");
		}
		if (ftruncate(fd_out, 0) < 0) {
			err(1, "ftruncate");
		}
	}
	parser_output_write(parser, fd_out);

	close(fd_out);
	close(fd_in);

	free(line);
	parser_free(parser);

	return 0;
}
