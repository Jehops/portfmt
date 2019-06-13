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

#if HAVE_ERR
# include <err.h>
#endif
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "mainutils.h"
#include "parser.h"

enum PorteditCommand {
	PORTEDIT_BUMP_REVISION,
	PORTEDIT_GET_VARIABLE,
	PORTEDIT_SET_VARIABLE,
	PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES,
	PORTEDIT_PRIVATE_LINT_ORDER,
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
	int status = 0;
	int fd_in = STDIN_FILENO;
	int fd_out = STDOUT_FILENO;
	struct ParserSettings settings;

	parser_init_settings(&settings);
	settings.behavior = PARSER_COLLAPSE_ADJACENT_VARIABLES |
		PARSER_OUTPUT_REFORMAT;

	if (strcmp(getprogname(), "portedit") == 0) {
		settings.behavior |= PARSER_OUTPUT_EDITED;
		settings.behavior |= PARSER_KEEP_EOL_COMMENTS;
	}

	if (!read_common_args(&argc, &argv, &settings)) {
		usage();
	}

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
			settings.behavior |= PARSER_OUTPUT_RAWLINES;
		} else if (strcmp(argv[0], "__private__list-unknown-variables") == 0 && argc > 0) {
			edit.cmd = PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES;
			edit.argc = 1;
			edit.argv = argv;
			settings.behavior |= PARSER_OUTPUT_RAWLINES;
		} else {
			usage();
		}
		argc -= edit.argc;
		argv += edit.argc;
	}

	if (settings.behavior & PARSER_OUTPUT_DUMP_TOKENS) {
		settings.behavior &= ~PARSER_OUTPUT_INPLACE;
	}

	if (argc > 1 || ((settings.behavior & PARSER_OUTPUT_INPLACE) && argc == 0)) {
		usage();
	} else if (argc == 1) {
		if (settings.behavior & PARSER_OUTPUT_INPLACE) {
			fd_in = open(argv[0], O_RDWR);
			if (fd_in < 0) {
				err(1, "open");
			}
			fd_out = fd_in;
		} else  {
			fd_in = open(argv[0], O_RDONLY);
			if (fd_in < 0) {
				err(1, "open");
			}
		}
	}

	if (!can_use_colors(fd_out)) {
		settings.behavior |= PARSER_OUTPUT_NO_COLOR;
	}
	enter_sandbox(fd_in, fd_out);

	struct Parser *parser = parser_new(&settings);
	if (!parser_read_from_fd(parser, fd_in)) {
		err(1, "parser_read_from_fd");
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
		case PORTEDIT_PRIVATE_LINT_ORDER:
			parser_edit(parser, lint_order, &status);
			break;
		default:
			break;
		}
	}

	parser_output_prepare(parser);
	parser_output_write(parser, fd_out);
	parser_free(parser);

	close(fd_out);
	close(fd_in);

	return status;
}
