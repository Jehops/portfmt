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
	PORTEDIT_MERGE,
	PORTEDIT_SET_VERSION,
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
	fprintf(stderr, "usage: portedit bump-revision [-aditu] [-w wrapcol] [Makefile]\n");
	fprintf(stderr, "       portedit get [-aditu] [-w wrapcol] [Makefile]\n");
	fprintf(stderr, "       portedit merge [-aditu] [-w wrapcol] Makefile\n");
	fprintf(stderr, "       portedit set-version [-aditu] [-w wrapcol] Makefile\n");
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
	} else if (strcmp(command, "merge") == 0) {
		edit.cmd = PORTEDIT_MERGE;
		edit.argc = 0;
		edit.argv = argv;
	} else if (strcmp(command, "set-version") == 0 && argc > 0) {
		edit.cmd = PORTEDIT_SET_VERSION;
		edit.argc = 1;
		edit.argv = argv;
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

	if (edit.cmd == PORTEDIT_MERGE && argc == 0) {
		usage();
	}
	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	if (!open_file(&argc, &argv, &settings, &fp_in, &fp_out)) {
		if (fp_in == NULL) {
			err(1, "fopen");
		} else {
			usage();
		}
	}
	if (!can_use_colors(fp_out)) {
		settings.behavior |= PARSER_OUTPUT_NO_COLOR;
	}

	if (edit.cmd == PORTEDIT_MERGE) {
		// TODO
	} else {
		enter_sandbox(fp_in, fp_out);
	}

	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, fp_in);
	if (error != PARSER_ERROR_OK) {
		errx(1, "parser_read_from_fd: %s", parser_error_tostring(parser));
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		errx(1, "parser_read_finish: %s", parser_error_tostring(parser));
	}

	int status = 0;
	switch (edit.cmd) {
	case PORTEDIT_BUMP_REVISION:
		error = parser_edit(parser, edit_bump_revision, NULL);
		break;
	case PORTEDIT_GET_VARIABLE:
		error = parser_edit(parser, edit_output_variable_value, edit.argv[0]);
		break;
	case PORTEDIT_MERGE: {
		struct Parser *subparser = parser_new(&settings);
		error = parser_read_from_file(subparser, stdin);
		if (error != PARSER_ERROR_OK) {
			errx(1, "parser_read_from_file: %s", parser_error_tostring(parser));
		}
		error = parser_read_finish(subparser);
		if (error != PARSER_ERROR_OK) {
			errx(1, "parser_read_finish: %s", parser_error_tostring(parser));
		}
		error = parser_edit(parser, edit_merge, subparser);
		if (error != PARSER_ERROR_OK) {
			errx(1, "parser_edit: %s", parser_error_tostring(parser));
		}
		parser_free(subparser);
		break;
	} case PORTEDIT_SET_VERSION:
		error = parser_edit(parser, edit_set_version, edit.argv[0]);
		break;
	case PORTEDIT_PRIVATE_LIST_UNKNOWN_VARIABLES:
		error = parser_edit(parser, edit_output_unknown_variables, NULL);
		break;
	default:
		usage();
		break;
	}

	if (error != PARSER_ERROR_OK) {
		errx(1, "parser_edit: %s", parser_error_tostring(parser));
	}

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "parser_output_write: %s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}
