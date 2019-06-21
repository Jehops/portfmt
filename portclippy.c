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

static void usage(void);

void
usage()
{
	fprintf(stderr, "usage: portclippy [Makefile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	struct ParserSettings settings;

	parser_init_settings(&settings);
	settings.behavior = PARSER_OUTPUT_RAWLINES;

	argc--;
	argv++;

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	if (!open_file(&argc, &argv, &settings, &fp_in, &fp_out)) {
		if (fp_in == NULL) {
			err(1, "open_file");
		} else {
			usage();
		}
	}
	if (!can_use_colors(fp_out)) {
		settings.behavior |= PARSER_OUTPUT_NO_COLOR;
	}
	enter_sandbox(fp_in, fp_out);

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
	error = parser_edit(parser, lint_order, &status);
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
