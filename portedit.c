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
	PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES,
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
	fprintf(stderr, "usage: portedit bump-revision|get [-aditu] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	struct ParserSettings settings;

	parser_init_settings(&settings);
	settings.behavior = PARSER_COLLAPSE_ADJACENT_VARIABLES |
		PARSER_OUTPUT_REFORMAT | PARSER_OUTPUT_EDITED |
		PARSER_KEEP_EOL_COMMENTS;

	struct Portedit edit;
	if (argc < 2) {
		usage();
	}
	const char *command = argv[1];
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, &settings)) {
		usage();
	}

	if (strcmp(command, "bump-revision") == 0) {
		edit.cmd = PORTEDIT_BUMP_REVISION;
		edit.argc = 0;
		edit.argv = argv;
	} else if (strcmp(command, "get") == 0 && argc > 0) {
		edit.cmd = PORTEDIT_GET_VARIABLE;
		edit.argc = 1;
		edit.argv = argv;
		settings.behavior |= PARSER_OUTPUT_RAWLINES;
	} else if (strcmp(command, "__private__list-unknown-variables") == 0 && argc > 0) {
		edit.cmd = PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES;
		edit.argc = 0;
		edit.argv = NULL;
		settings.behavior |= PARSER_OUTPUT_RAWLINES;
	} else {
		usage();
	}
	argc -= edit.argc;
	argv += edit.argc;

	int fd_in = STDIN_FILENO;
	int fd_out = STDOUT_FILENO;
	if (!open_file(&argc, &argv, &settings, &fd_in, &fd_out)) {
		if (fd_in < 0) {
			err(1, "open");
		} else {
			usage();
		}
	}
	if (!can_use_colors(fd_out)) {
		settings.behavior |= PARSER_OUTPUT_NO_COLOR;
	}
	enter_sandbox(fd_in, fd_out);

	struct Parser *parser = parser_new(&settings);
	parser_read_from_fd(parser, fd_in);
	parser_read_finish(parser);
	if (parser_error(parser)) {
		errx(1, "failed to read file: %s", parser_error(parser));
	}

	int status = 0;
	switch (edit.cmd) {
	case PORTEDIT_BUMP_REVISION:
		parser_edit(parser, edit_bump_revision, NULL);
		break;
	case PORTEDIT_GET_VARIABLE:
		parser_edit(parser, edit_output_variable_value, edit.argv[0]);
		break;
	case PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES:
		parser_edit(parser, edit_output_unknown_variables, NULL);
		break;
	default:
		usage();
		break;
	}

	parser_output_write(parser, fd_out);
	if (parser_error(parser)) {
		errx(1, "failed to write file: %s", parser_error(parser));
	}
	parser_free(parser);

	close(fd_out);
	close(fd_in);

	return status;
}
