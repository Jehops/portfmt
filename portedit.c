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
#include "util.h"

static int bump_epoch(struct ParserSettings *, int, char *[]);
static int bump_revision(struct ParserSettings *, int, char *[]);
static int get_variable(struct ParserSettings *, int, char *[]);
static int merge(struct ParserSettings *, int, char *[]);
static int set_version(struct ParserSettings *, int, char *[]);
static int unknown_vars(struct ParserSettings *, int, char *[]);
static void bump_epoch_usage(void);
static void bump_revision_usage(void);
static void get_variable_usage(void);
static void merge_usage(void);
static void set_version_usage(void);
static void unknown_vars_usage(void);
static void usage(void);

static struct Parser *read_file(struct ParserSettings *, FILE **, FILE **, int *, char **[], int);

struct PorteditCommand {
	const char *name;
	int (*main)(struct ParserSettings *, int, char *[]);
};

static struct PorteditCommand cmds[] = {
	{ "bump-epoch", bump_epoch },
	{ "bump-revision", bump_revision },
	{ "get", get_variable },
	{ "merge", merge },
	{ "unknown-vars", unknown_vars },
	{ "set-version", set_version },
};

int
bump_epoch(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		bump_epoch_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "iuw:")) {
		bump_epoch_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		bump_epoch_usage();
	}

	int error = parser_edit(parser, edit_bump_revision, "PORTEPOCH");
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return 0;
}

int
bump_revision(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		bump_revision_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "iuw:")) {
		bump_revision_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		bump_revision_usage();
	}

	int error = parser_edit(parser, edit_bump_revision, NULL);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return 0;
}

int
get_variable(struct ParserSettings *settings, int argc, char *argv[])
{
	settings->behavior |= PARSER_OUTPUT_RAWLINES;

	if (argc < 3) { 
		get_variable_usage();
	}
	const char *var = argv[2];
	argv += 3;
	argc -= 3;

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		get_variable_usage();
	}

	int error = parser_edit(parser, edit_output_variable_value, var);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return 0;
}

int
merge(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		merge_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "iuw:")) {
		merge_usage();
	}
	if (argc == 0) {
		merge_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 1);
	if (parser == NULL) {
		merge_usage();
	}

	struct Parser *subparser = parser_new(settings);
	int error = parser_read_from_file(subparser, stdin);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	error = parser_read_finish(subparser);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	error = parser_merge(parser, subparser, PARSER_MERGE_SHELL_IS_DELETE | PARSER_MERGE_COMMENTS);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(subparser);

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return 0;
}

int
set_version(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		set_version_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "iuw:")) {
		set_version_usage();
	}

	if (argc < 1) { 
		set_version_usage();
	}
	const char *version = argv[0];
	argv++;
	argc--;

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 1);
	if (parser == NULL) {
		set_version_usage();
	}

	int error = parser_edit(parser, edit_set_version, version);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return 0;
}

int
unknown_vars(struct ParserSettings *settings, int argc, char *argv[])
{
	settings->behavior |= PARSER_OUTPUT_RAWLINES;

	argv += 2;
	argc -= 2;

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		unknown_vars_usage();
	}

	int error = parser_edit(parser, edit_output_unknown_variables, NULL);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return 0;
}


void
bump_epoch_usage()
{
	fprintf(stderr, "usage: portedit bump-epoch [-iu] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

void
bump_revision_usage()
{
	fprintf(stderr, "usage: portedit bump-revision [-iu] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

void
get_variable_usage()
{
	fprintf(stderr, "usage: portedit get <variable-regexp> [Makefile]\n");
	exit(EX_USAGE);
}

void
merge_usage()
{
	fprintf(stderr, "usage: portedit merge [-iu] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

void
set_version_usage()
{
	fprintf(stderr, "usage: portedit set-version [-iu] [-w wrapcol] <version> [Makefile]\n");
	exit(EX_USAGE);
}

void
unknown_vars_usage()
{
	fprintf(stderr, "usage: portedit unknown-vars [Makefile]\n");
	exit(EX_USAGE);
}

void
usage()
{
	fprintf(stderr, "usage: portedit <command> [<args>]\n\n");
	fprintf(stderr, "Supported commands:\n");
	fprintf(stderr, "\t%-16s%s\n", "bump-epoch", "Bump and sanitize PORTEPOCH");
	fprintf(stderr, "\t%-16s%s\n", "bump-revision", "Bump and sanitize PORTREVISION");
	fprintf(stderr, "\t%-16s%s\n", "get", "Get raw variable tokens");
	fprintf(stderr, "\t%-16s%s\n", "merge", "Merge variables into the Makefile");
	fprintf(stderr, "\t%-16s%s\n", "set-version", "Bump port version, set DISTVERSION{,PREFIX,SUFFIX}");
	fprintf(stderr, "\t%-16s%s\n", "unknown-vars", "List unknown variables");
	exit(EX_USAGE);
}

struct Parser *
read_file(struct ParserSettings *settings, FILE **fp_in, FILE **fp_out, int *argc, char **argv[], int keep_stdin_open)
{
	if (!open_file(argc, argv, settings, fp_in, fp_out, keep_stdin_open)) {
		if (*fp_in == NULL) {
			err(1, "fopen");
		} else {
			return NULL;
		}
	}
	if (!can_use_colors(*fp_out)) {
		settings->behavior |= PARSER_OUTPUT_NO_COLOR;
	}

	enter_sandbox();

	struct Parser *parser = parser_new(settings);
	enum ParserError error = parser_read_from_file(parser, *fp_in);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	return parser;
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		usage();
	}
	const char *command = argv[1];

	struct ParserSettings settings;
	parser_init_settings(&settings);
	settings.behavior = PARSER_COLLAPSE_ADJACENT_VARIABLES |
		PARSER_OUTPUT_REFORMAT | PARSER_OUTPUT_EDITED |
		PARSER_KEEP_EOL_COMMENTS;

	for (size_t i = 0; i < nitems(cmds); i++) {
		if (strcmp(command, cmds[i].name) == 0) {
			return cmds[i].main(&settings, argc, argv);
		}
	}

	usage();
	abort();
}
