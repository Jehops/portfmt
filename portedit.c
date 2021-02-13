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
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libias/array.h>
#include <libias/set.h>
#include <libias/util.h>

#include "mainutils.h"
#include "parser.h"
#include "parser/edits.h"
#include "regexp.h"

static int apply(struct ParserSettings *, int, char *[]);
static int bump_epoch(struct ParserSettings *, int, char *[]);
static int bump_revision(struct ParserSettings *, int, char *[]);
static int get_variable(struct ParserSettings *, int, char *[]);
static int merge(struct ParserSettings *, int, char *[]);
static int sanitize_append(struct ParserSettings *, int, char *[]);
static int set_version(struct ParserSettings *, int, char *[]);
static int unknown_targets(struct ParserSettings *, int, char *[]);
static int unknown_vars(struct ParserSettings *, int, char *[]);
static void apply_usage(void);
static void bump_epoch_usage(void);
static void bump_revision_usage(void);
static void get_variable_usage(void);
static void merge_usage(void);
static void sanitize_append_usage(void);
static void set_version_usage(void);
static void unknown_targets_usage(void);
static void unknown_vars_usage(void);
static void usage(void);

static struct Parser *read_file(struct ParserSettings *, FILE **, FILE **, int *, char **[], int);

struct PorteditCommand {
	const char *name;
	int (*main)(struct ParserSettings *, int, char *[]);
};

static struct PorteditCommand cmds[] = {
	{ "apply", apply },
	{ "bump-epoch", bump_epoch },
	{ "bump-revision", bump_revision },
	{ "get", get_variable },
	{ "merge", merge },
	{ "unknown-targets", unknown_targets },
	{ "unknown-vars", unknown_vars },
	{ "sanitize-append", sanitize_append },
	{ "set-version", set_version },
};

struct ParserEdits {
	const char *name;
	ParserEditFn fn;
};
static struct ParserEdits parser_edits[] = {
	{ "edit.bump-revision", edit_bump_revision },
	{ "edit.merge", edit_merge },
	{ "edit.set-version", edit_set_version },
	{ "kakoune.select-object-on-line", kakoune_select_object_on_line },
	{ "lint.clones", lint_clones },
	{ "lint.order", lint_order },
	{ "output.unknown-targets", output_unknown_targets },
	{ "output.unknown-variables", output_unknown_variables },
	{ "output.variable-value", output_variable_value },
	{ "refactor.collapse-adjacent-variables", refactor_collapse_adjacent_variables },
	{ "refactor.dedup-tokens", refactor_dedup_tokens },
	{ "refactor.remove-consecutive-empty_-ines", refactor_remove_consecutive_empty_lines },
	{ "refactor.sanitize-append_modifier", refactor_sanitize_append_modifier },
	{ "refactor.sanitize-cmake_args", refactor_sanitize_cmake_args },
	{ "refactor.sanitize-comments", refactor_sanitize_comments },
	{ "refactor.sanitize-eol-comments", refactor_sanitize_eol_comments },
};

int
apply(struct ParserSettings *settings, int argc, char *argv[])
{
	settings->behavior |= PARSER_ALLOW_FUZZY_MATCHING;

	if (argc < 3) {
		merge_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "DiuUw:", NULL)) {
		apply_usage();
	}
	if (argc < 1) {
		apply_usage();
	}

	const char *apply_edit = argv[0];
	argv++;
	argc--;

	if (str_startswith(apply_edit, "kakoune.") ||
	    str_startswith(apply_edit, "lint.") ||
	    str_startswith(apply_edit, "output.")) {
		settings->behavior |= PARSER_OUTPUT_RAWLINES;
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 1);
	if (parser == NULL) {
		apply_usage();
	}

	void *userdata = NULL;
	if (str_startswith(apply_edit, "output.")) {
		userdata = xmalloc(sizeof(struct ParserEditOutput));
	}

	ParserEditFn editfn = NULL;
	for (size_t i = 0; i < nitems(parser_edits); i++) {
		if (strcmp(parser_edits[i].name, apply_edit) == 0) {
			editfn = parser_edits[i].fn;
			break;
		}
	}
	if (editfn == NULL) {
		errx(1, "%s not found", apply_edit);
	}
	int error = parser_edit(parser, editfn, userdata);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s: %s", apply_edit, parser_error_tostring(parser));
	}
	free(userdata);

	int status = 0;
	error = parser_output_write_to_file(parser, fp_out);
	if (error == PARSER_ERROR_DIFFERENCES_FOUND) {
		status = 2;
	} else if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}

int
bump_epoch(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		bump_epoch_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "DiuUw:", NULL)) {
		bump_epoch_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		bump_epoch_usage();
	}

	struct ParserEdit params = { NULL, "PORTEPOCH", PARSER_MERGE_DEFAULT };
	int error = parser_edit(parser, edit_bump_revision, &params);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	int status = 0;
	error = parser_output_write_to_file(parser, fp_out);
	if (error == PARSER_ERROR_DIFFERENCES_FOUND) {
		status = 2;
	} else if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}

int
bump_revision(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		bump_revision_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "DiuUw:", NULL)) {
		bump_revision_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		bump_revision_usage();
	}

	struct ParserEdit params = { NULL, NULL, PARSER_MERGE_DEFAULT };
	int error = parser_edit(parser, edit_bump_revision, &params);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	int status = 0;
	error = parser_output_write_to_file(parser, fp_out);
	if (error == PARSER_ERROR_DIFFERENCES_FOUND) {
		status = 2;
	} else if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}

static int
get_variable_filter(struct Parser *parser, const char *key, void *userdata)
{
	struct Regexp *regexp = userdata;
	return regexp_exec(regexp, key) == 0;
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

	struct Regexp *regexp = regexp_new_from_str(var, REG_EXTENDED);
	if (regexp == NULL) {
		errx(1, "invalid regexp");
	}
	struct ParserEditOutput param = { get_variable_filter, regexp, NULL, NULL, 0, 0, NULL, NULL };
	int error = parser_edit(parser, output_variable_value, &param);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	regexp_free(regexp);

	error = parser_output_write_to_file(parser, fp_out);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	if (param.found) {
		return 0;
	}
	return 1;
}

int
merge(struct ParserSettings *settings, int argc, char *argv[])
{
	settings->behavior |= PARSER_ALLOW_FUZZY_MATCHING;

	if (argc < 2) {
		merge_usage();
	}
	argv++;
	argc--;

	struct Array *expressions = array_new();
	if (!read_common_args(&argc, &argv, settings, "De:iuUw:", expressions)) {
		merge_usage();
	}
	if (argc == 0 && array_len(expressions) == 0) {
		merge_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 1);
	if (parser == NULL) {
		merge_usage();
	}

	struct Parser *subparser = parser_new(settings);
	int error = PARSER_ERROR_OK;
	if (array_len(expressions) > 0) {
		for (size_t i = 0; i < array_len(expressions); i++) {
			char *expr = array_get(expressions, i);
			error = parser_read_from_buffer(subparser, expr, strlen(expr));
			if (error != PARSER_ERROR_OK) {
				errx(1, "%s", parser_error_tostring(subparser));
			}
			free(expr);
		}
	} else {
		error = parser_read_from_file(subparser, stdin);
		if (error != PARSER_ERROR_OK) {
			errx(1, "%s", parser_error_tostring(subparser));
		}
	}
	array_free(expressions);
	expressions = NULL;

	error = parser_read_finish(subparser);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(subparser));
	}
	error = parser_merge(parser, subparser, PARSER_MERGE_SHELL_IS_DELETE | PARSER_MERGE_COMMENTS);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(subparser);

	int status = 0;
	error = parser_output_write_to_file(parser, fp_out);
	if (error == PARSER_ERROR_DIFFERENCES_FOUND) {
		status = 2;
	} else if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}

int
sanitize_append(struct ParserSettings *settings, int argc, char *argv[])
{
	settings->behavior |= PARSER_SANITIZE_APPEND;

	if (argc < 1) {
		sanitize_append_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "DiuUw:", NULL)) {
		sanitize_append_usage();
	}

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 1);
	if (parser == NULL) {
		sanitize_append_usage();
	}

	int error = parser_edit(parser, refactor_sanitize_append_modifier, NULL);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	int status = 0;
	error = parser_output_write_to_file(parser, fp_out);
	if (error == PARSER_ERROR_DIFFERENCES_FOUND) {
		status = 2;
	} else if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}

int
set_version(struct ParserSettings *settings, int argc, char *argv[])
{
	if (argc < 2) {
		set_version_usage();
	}
	argv++;
	argc--;

	if (!read_common_args(&argc, &argv, settings, "DiuUw:", NULL)) {
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

	struct ParserEdit params = { NULL, version, PARSER_MERGE_DEFAULT };
	int error = parser_edit(parser, edit_set_version, &params);
	if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}

	int status = 0;
	error = parser_output_write_to_file(parser, fp_out);
	if (error == PARSER_ERROR_DIFFERENCES_FOUND) {
		status = 2;
	} else if (error != PARSER_ERROR_OK) {
		errx(1, "%s", parser_error_tostring(parser));
	}
	parser_free(parser);

	fclose(fp_out);
	if (fp_out != fp_in) {
		fclose(fp_in);
	}

	return status;
}

int
unknown_targets(struct ParserSettings *settings, int argc, char *argv[])
{
	settings->behavior |= PARSER_OUTPUT_RAWLINES;

	argv += 2;
	argc -= 2;

	FILE *fp_in = stdin;
	FILE *fp_out = stdout;
	struct Parser *parser = read_file(settings, &fp_in, &fp_out, &argc, &argv, 0);
	if (parser == NULL) {
		unknown_targets_usage();
	}

	struct ParserEditOutput param = { NULL, NULL, NULL, NULL, 0, 0, NULL, NULL };
	enum ParserError error = parser_edit(parser, output_unknown_targets, &param);
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

	if (param.found) {
		return 1;
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

	struct ParserEditOutput param = { NULL, NULL, NULL, NULL, 0, 0, NULL, NULL };
	enum ParserError error = parser_edit(parser, output_unknown_variables, &param);
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

	if (param.found) {
		return 1;
	}
	return 0;
}

void
apply_usage()
{
	fprintf(stderr, "usage: portedit apply [-DiuU] [-w wrapcol] <edit> [Makefile]\n");
	exit(EX_USAGE);
}

void
bump_epoch_usage()
{
	fprintf(stderr, "usage: portedit bump-epoch [-DiuU] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

void
bump_revision_usage()
{
	fprintf(stderr, "usage: portedit bump-revision [-DiuU] [-w wrapcol] [Makefile]\n");
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
	fprintf(stderr, "usage: portedit merge [-DiuU] [-w wrapcol] [-e expr] [Makefile]\n");
	exit(EX_USAGE);
}

void
sanitize_append_usage()
{
	fprintf(stderr, "usage: portedit sanitize-append [-DiuU] [-w wrapcol] [Makefile]\n");
	exit(EX_USAGE);
}

void
set_version_usage()
{
	fprintf(stderr, "usage: portedit set-version [-DiuU] [-w wrapcol] <version> [Makefile]\n");
	exit(EX_USAGE);
}

void
unknown_targets_usage()
{
	fprintf(stderr, "usage: portedit unknown-targets [Makefile]\n");
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
	fprintf(stderr, "\t%-16s%s\n", "apply", "Call an edit plugin");
	fprintf(stderr, "\t%-16s%s\n", "bump-epoch", "Bump and sanitize PORTEPOCH");
	fprintf(stderr, "\t%-16s%s\n", "bump-revision", "Bump and sanitize PORTREVISION");
	fprintf(stderr, "\t%-16s%s\n", "get", "Get raw variable tokens");
	fprintf(stderr, "\t%-16s%s\n", "merge", "Merge variables into the Makefile");
	fprintf(stderr, "\t%-16s%s\n", "sanitize-append", "Sanitize += before bsd.port.{options,pre}.mk");
	fprintf(stderr, "\t%-16s%s\n", "set-version", "Bump port version, set DISTVERSION{,PREFIX,SUFFIX}");
	fprintf(stderr, "\t%-16s%s\n", "unknown-targets", "List unknown targets");
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
	free(settings->filename);
	settings->filename = NULL;
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
	settings.behavior = PARSER_COLLAPSE_ADJACENT_VARIABLES | PARSER_DEDUP_TOKENS |
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
