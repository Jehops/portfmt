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

#include <sys/param.h>
#include <sys/stat.h>
#if HAVE_CAPSICUM
# include <sys/capsicum.h>
#endif
#include <assert.h>
#include <dirent.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libias/array.h>
#include <libias/diff.h>
#include <libias/map.h>
#include <libias/set.h>
#include <libias/util.h>

#include "capsicum_helpers.h"
#include "conditional.h"
#include "mainutils.h"
#include "parser.h"
#include "parser/edits.h"
#include "portscanlog.h"
#include "portscanstatus.h"
#include "regexp.h"
#include "token.h"
#include "variable.h"

enum ScanFlags {
	SCAN_NOTHING = 0,
	SCAN_CATEGORIES = 1 << 0,
	SCAN_CLONES = 1 << 1,
	SCAN_OPTION_DEFAULT_DESCRIPTIONS = 1 << 2,
	SCAN_OPTIONS = 1 << 3,
	SCAN_UNKNOWN_TARGETS = 1 << 4,
	SCAN_UNKNOWN_VARIABLES = 1 << 5,
	SCAN_VARIABLE_VALUES = 1 << 6,
	SCAN_PARTIAL = 1 << 7,
	SCAN_COMMENTS = 1 << 8,
};

struct ScanResult {
	char *origin;
	struct Set *comments;
	struct Set *errors;
	struct Set *unknown_variables;
	struct Set *unknown_targets;
	struct Set *clones;
	struct Set *option_default_descriptions;
	struct Set *option_groups;
	struct Set *options;
	struct Set *variable_values;
	enum ScanFlags flags;
};

struct ScanPortArgs {
	enum ScanFlags flags;
	int portsdir;
	const char *path;
	struct Regexp *keyquery;
	struct Regexp *query;
	ssize_t editdist;
	struct Map *default_option_descriptions;
	struct ScanResult *result;
};

struct CategoryReaderData {
	int portsdir;
	struct Array *categories;
	size_t start;
	size_t end;
	enum ScanFlags flags;
};

struct CategoryReaderResult {
	struct Array *error_origins;
	struct Array *error_msgs;
	struct Array *nonexistent;
	struct Array *origins;
	struct Array *unhooked;
	struct Array *unsorted;
};

struct PortReaderData {
	int portsdir;
	struct Array *origins;
	size_t start;
	size_t end;
	struct Regexp *keyquery;
	struct Regexp *query;
	ssize_t editdist;
	enum ScanFlags flags;
	struct Map *default_option_descriptions;
};

static void lookup_subdirs(int, const char *, const char *, enum ScanFlags, struct Array *, struct Array *, struct Array *, struct Array *, struct Array *, struct Array *);
static void scan_port(struct ScanPortArgs *);
static void *lookup_origins_worker(void *);
static enum ParserError process_include(struct Parser *, struct Set *, const char *, int, const char *);
static PARSER_EDIT(extract_includes);
static PARSER_EDIT(get_default_option_descriptions);
static DIR *diropenat(int, const char *);
static FILE *fileopenat(int, const char *);
static void *scan_ports_worker(void *);
static struct Array *lookup_origins(int, enum ScanFlags, struct PortscanLog *);
static void scan_ports(int, struct Array *, enum ScanFlags, struct Regexp *, struct Regexp *, ssize_t, struct PortscanLog *);
static void usage(void);

static void
add_error(struct Set *errors, char *msg)
{
	if (set_contains(errors, msg)) {
		free(msg);
	} else {
		set_add(errors, msg);
	}
}

DIR *
diropenat(int root, const char *path)
{
	int fd = openat(root, path, O_RDONLY | O_DIRECTORY);
	if (fd == -1) {
		return NULL;
	}
#if HAVE_CAPSICUM
	if (caph_limit_stream(fd, CAPH_READ | CAPH_READDIR) < 0) {
		err(1, "caph_limit_stream: %s", path);
	}
#endif

	DIR *dir = fdopendir(fd);
	if (dir == NULL) {
		close(fd);
	}
	return dir;
}

FILE *
fileopenat(int root, const char *path)
{
	int fd = openat(root, path, O_RDONLY);
	if (fd == -1) {
		return NULL;
	}
#if HAVE_CAPSICUM
	if (caph_limit_stream(fd, CAPH_READ) < 0) {
		err(1, "caph_limit_stream: %s", path);
	}
#endif

	FILE *f = fdopen(fd, "r");
	if (f == NULL) {
		close(fd);
	}

	return f;
}

void
lookup_subdirs(int portsdir, const char *category, const char *path, enum ScanFlags flags, struct Array *subdirs, struct Array *nonexistent, struct Array *unhooked, struct Array *unsorted, struct Array *error_origins, struct Array *error_msgs)
{
	FILE *in = fileopenat(portsdir, path);
	if (in == NULL) {
		array_append(error_origins, xstrdup(path));
		array_append(error_msgs, str_printf("fileopenat: %s", strerror(errno)));
		return;
	}

	struct ParserSettings settings;
	parser_init_settings(&settings);
	if (flags & SCAN_CATEGORIES) {
		settings.behavior |= PARSER_OUTPUT_REFORMAT | PARSER_OUTPUT_DIFF;
	}

	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		array_append(error_origins, xstrdup(path));
		array_append(error_msgs, xstrdup(parser_error_tostring(parser)));
		goto cleanup;
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		array_append(error_origins, xstrdup(path));
		array_append(error_msgs, xstrdup(parser_error_tostring(parser)));
		goto cleanup;
	}

	struct Array *tmp;
	if (parser_lookup_variable_all(parser, "SUBDIR", &tmp, NULL) == NULL) {
		goto cleanup;
	}

	if (unhooked && (flags & SCAN_CATEGORIES)) {
		DIR *dir = diropenat(portsdir, category);
		if (dir == NULL) {
			array_append(error_origins, xstrdup(category));
			array_append(error_msgs, str_printf("diropenat: %s", strerror(errno)));
		} else {
			struct dirent *dp;
			while ((dp = readdir(dir)) != NULL) {
				if (dp->d_name[0] == '.') {
					continue;
				}
				char *path = str_printf("%s/%s", category, dp->d_name);
				struct stat sb;
				if (fstatat(portsdir, path, &sb, 0) == -1 ||
				    !S_ISDIR(sb.st_mode)) {
					free(path);
					continue;
				}
				if (array_find(tmp, dp->d_name, str_compare, NULL) == -1) {
					array_append(unhooked, path);
				} else {
					free(path);
				}
			}
			closedir(dir);
		}
	}

	ARRAY_FOREACH(tmp, char *, port) {
		char *origin;
		if (flags != SCAN_NOTHING) {
			origin = str_printf("%s/%s", category, port);
		} else {
			origin = xstrdup(port);
		}
		if (flags & SCAN_CATEGORIES) {
			struct stat sb;
			if (nonexistent &&
			    (fstatat(portsdir, origin, &sb, 0) == -1 ||
			     !S_ISDIR(sb.st_mode))) {
				array_append(nonexistent, xstrdup(origin));
			}
		}
		array_append(subdirs, origin);
	}
	array_free(tmp);

	if ((flags & SCAN_CATEGORIES) && unsorted &&
	    parser_output_write_to_file(parser, NULL) == PARSER_ERROR_DIFFERENCES_FOUND) {
		array_append(unsorted, xstrdup(category));
	}

cleanup:
	parser_free(parser);
	fclose(in);
}

enum ParserError
process_include(struct Parser *parser, struct Set *errors, const char *curdir, int portsdir, const char *filename)
{
	char *path;
	if (str_startswith(filename, "${MASTERDIR}/")) {
		// Do not follow to the master port.  It would already
		// have been processed once, so we do not need to do
		// it again.
		return PARSER_ERROR_OK;
	} else if (str_startswith(filename, "${.PARSEDIR}/")) {
		filename += strlen("${.PARSEDIR}/");
		path = str_printf("%s/%s", curdir, filename);
	} else if (str_startswith(filename, "${.CURDIR}/")) {
		filename += strlen("${.CURDIR}/");
		path = str_printf("%s/%s", curdir, filename);
	} else if (str_startswith(filename, "${.CURDIR:H}/")) {
		filename += strlen("${.CURDIR:H}/");
		path = str_printf("%s/../%s", curdir, filename);
	} else if (str_startswith(filename, "${.CURDIR:H:H}/")) {
		filename += strlen("${.CURDIR:H:H}/");
		path = str_printf("%s/../../%s", curdir, filename);
	} else if (str_startswith(filename, "${PORTSDIR}/")) {
		filename += strlen("${PORTSDIR}/");
		path = xstrdup(filename);
	} else if (str_startswith(filename, "${FILESDIR}/")) {
		filename += strlen("${FILESDIR}/");
		path = str_printf("%s/files/%s", curdir, filename);
	} else {
		path = str_printf("%s/%s", curdir, filename);
	}
	FILE *f = fileopenat(portsdir, path);
	if (f == NULL) {
		add_error(errors, str_printf("cannot open include: %s: %s", path, strerror(errno)));
		free(path);
		return PARSER_ERROR_OK;
	}
	free(path);
	enum ParserError error = parser_read_from_file(parser, f);
	fclose(f);
	return error;
}

PARSER_EDIT(extract_includes)
{
	struct Array **retval = userdata;

	struct Array *includes = array_new();
	int found = 0;

	ARRAY_FOREACH(ptokens, struct Token *, t) {
		switch (token_type(t)) {
		case CONDITIONAL_START:
			if (conditional_type(token_conditional(t)) == COND_INCLUDE) {
				found = 1;
			}
			break;
		case CONDITIONAL_TOKEN:
			if (found == 1) {
				found++;
			} else if (found > 1) {
				found = 0;
				char *data = token_data(t);
				if (data && *data == '"' && data[strlen(data) - 1] == '"') {
					data++;
					data[strlen(data) - 1] = 0;
					array_append(includes, data);
				}
			}
			break;
		case CONDITIONAL_END:
			found = 0;
			break;
		default:
			break;
		}
	}

	*retval = includes;

	return NULL;
}

static int
variable_value_filter(struct Parser *parser, const char *value, void *userdata)
{
	struct Regexp *query = userdata;
	return !query || regexp_exec(query, value) == 0;
}

static int
unknown_targets_filter(struct Parser *parser, const char *value, void *userdata)
{
	struct Regexp *query = userdata;
	return !query || regexp_exec(query, value) == 0;
}

static int
unknown_variables_filter(struct Parser *parser, const char *value, void *userdata)
{
	struct Regexp *query = userdata;
	return !query || regexp_exec(query, value) == 0;
}

static int
char_cmp(const void *ap, const void *bp, void *userdata)
{
	char a = *(char *)ap;
	char b = *(char *)bp;
	if (a < b) {
		return -1;
	} else if (a > b) {
		return 1;
	} else {
		return 0;
	}
}

static ssize_t
edit_distance(const char *a, const char *b)
{
	if (!a || !b) {
		return -1;
	}

	ssize_t editdist = -1;
	struct diff d;
	if (diff(&d, char_cmp, NULL, sizeof(char), a, strlen(a), b, strlen(b)) > 0) {
		editdist = d.editdist;
		free(d.ses);
		free(d.lcs);
	}

	return editdist;
}

void
scan_port(struct ScanPortArgs *args)
{
	struct ScanResult *retval = args->result;
	retval->comments = set_new(str_compare, NULL, free);
	retval->errors = set_new(str_compare, NULL, free);
	retval->option_default_descriptions = set_new(str_compare, NULL, free);
	retval->option_groups = set_new(str_compare, NULL, free);
	retval->options = set_new(str_compare, NULL, free);
	retval->unknown_variables = set_new(str_compare, NULL, free);
	retval->unknown_targets = set_new(str_compare, NULL, free);
	retval->variable_values = set_new(str_compare, NULL, free);

	struct ParserSettings settings;
	parser_init_settings(&settings);
	settings.behavior = PARSER_OUTPUT_RAWLINES;

	FILE *in = fileopenat(args->portsdir, args->path);
	if (in == NULL) {
		add_error(retval->errors, str_printf("fileopenat: %s", strerror(errno)));
		return;
	}

	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		add_error(retval->errors, parser_error_tostring(parser));
		goto cleanup;
	}

	if (args->flags & SCAN_PARTIAL) {
		error = parser_edit(parser, lint_bsd_port, NULL);
		if (error != PARSER_ERROR_OK) {
			add_error(retval->errors, parser_error_tostring(parser));
			goto cleanup;
		}
	}

	struct Array *includes = NULL;
	error = parser_edit(parser, extract_includes, &includes);
	if (error != PARSER_ERROR_OK) {
		add_error(retval->errors, parser_error_tostring(parser));
		goto cleanup;
	}
	ARRAY_FOREACH(includes, char *, include) {
		error = process_include(parser, retval->errors, retval->origin, args->portsdir, include);
		if (error != PARSER_ERROR_OK) {
			array_free(includes);
			add_error(retval->errors, parser_error_tostring(parser));
			goto cleanup;
		}
	}
	array_free(includes);

	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		add_error(retval->errors, parser_error_tostring(parser));
		goto cleanup;
	}

	if (retval->flags & SCAN_UNKNOWN_VARIABLES) {
		struct ParserEditOutput param = { unknown_variables_filter, args->query, NULL, NULL, 0, 1, NULL, NULL };
		error = parser_edit(parser, output_unknown_variables, &param);
		if (error != PARSER_ERROR_OK) {
			char *err = parser_error_tostring(parser);
			add_error(retval->errors, str_printf("output.unknown-variables: %s", err));
			free(err);
			goto cleanup;
		}
		for (size_t i = 0; i < array_len(param.keys); i++) {
			char *key = array_get(param.keys, i);
			char *value = array_get(param.values, i);
			set_add(retval->unknown_variables, key);
			free(value);
		}
		array_free(param.keys);
		array_free(param.values);
	}

	if (retval->flags & SCAN_UNKNOWN_TARGETS) {
		struct ParserEditOutput param = { unknown_targets_filter, args->query, NULL, NULL, 0, 1, NULL, NULL };
		error = parser_edit(parser, output_unknown_targets, &param);
		if (error != PARSER_ERROR_OK) {
			char *err = parser_error_tostring(parser);
			add_error(retval->errors, str_printf("output.unknown-targets: %s", err));
			free(err);
			goto cleanup;
		}
		for (size_t i = 0; i < array_len(param.keys); i++) {
			char *key = array_get(param.keys, i);
			char *value = array_get(param.values, i);
			set_add(retval->unknown_targets, key);
			free(value);
		}
		array_free(param.keys);
		array_free(param.values);
	}

	if (retval->flags & SCAN_CLONES) {
		// XXX: Limit by query?
		error = parser_edit(parser, lint_clones, &retval->clones);
		if (error != PARSER_ERROR_OK) {
			char *err = parser_error_tostring(parser);
			add_error(retval->errors, str_printf("lint.clones: %s", err));
			goto cleanup;
		}
	}

	if (retval->flags & SCAN_OPTION_DEFAULT_DESCRIPTIONS) {
		struct Map *descs = parser_metadata(parser, PARSER_METADATA_OPTION_DESCRIPTIONS);
		MAP_FOREACH(descs, char *, var, char *, desc) {
			char *default_desc = map_get(args->default_option_descriptions, var);
			if (!default_desc) {
				continue;
			}
			if (!set_contains(retval->option_default_descriptions, var)) {
				ssize_t editdist = edit_distance(default_desc, desc);
				if (strcasecmp(default_desc, desc) == 0 || (editdist > 0 && editdist <= args->editdist)) {
					set_add(retval->option_default_descriptions, xstrdup(var));
				}
			}
		}
	}

	if (retval->flags & SCAN_OPTIONS) {
		struct Set *groups = parser_metadata(parser, PARSER_METADATA_OPTION_GROUPS);
		SET_FOREACH(groups, char *, group) {
			if (!set_contains(retval->option_groups, group) &&
			    (args->query == NULL || regexp_exec(args->query, group) == 0)) {
				set_add(retval->option_groups, xstrdup(group));
			}
		}
		struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
		SET_FOREACH(options, char *, option) {
			if (!set_contains(retval->options, option) &&
			    (args->query == NULL || regexp_exec(args->query, option) == 0)) {
				set_add(retval->options, xstrdup(option));
			}
		}
	}

	if (retval->flags & SCAN_VARIABLE_VALUES) {
		struct ParserEditOutput param = { variable_value_filter, args->keyquery, variable_value_filter, args->query, 0, 1, NULL, NULL };
		error = parser_edit(parser, output_variable_value, &param);
		if (error != PARSER_ERROR_OK) {
			char *err = parser_error_tostring(parser);
			add_error(retval->errors, str_printf("output.variable-value: %s", err));
			free(err);
			goto cleanup;
		}

		for (size_t i = 0; i < array_len(param.values); i++) {
			char *key = array_get(param.keys, i);
			char *value = array_get(param.values, i);
			set_add(retval->variable_values, str_printf("%-30s\t%s", key, value));
			free(key);
			free(value);
		}
		array_free(param.values);
	}

	if (retval->flags & SCAN_COMMENTS) {
		struct Set *commented_portrevision = NULL;
		error = parser_edit(parser, lint_commented_portrevision, &commented_portrevision);
		if (error != PARSER_ERROR_OK) {
			char *err = parser_error_tostring(parser);
			add_error(retval->errors, str_printf("lint.commented-portrevision: %s", err));
			free(err);
			goto cleanup;
		}
		SET_FOREACH(commented_portrevision, char *, comment) {
			char *msg = str_printf("commented revision or epoch: %s", comment);
			if (set_contains(retval->comments, msg)) {
				free(msg);
			} else {
				set_add(retval->comments, msg);
			}
		}
		set_free(commented_portrevision);
	}

cleanup:
	parser_free(parser);
	fclose(in);
}

void *
scan_ports_worker(void *userdata)
{
	struct PortReaderData *data = userdata;
	struct Array *retval = array_new();

	if (data->start == data->end) {
		free(data);
		return retval;
	}

	assert(data->start < data->end);

	for (size_t i = data->start; i < data->end; i++) {
		portscan_status_print();
		char *origin = array_get(data->origins, i);
		char *path = str_printf("%s/Makefile", origin);
		struct ScanResult *result = xmalloc(sizeof(struct ScanResult));
		result->origin = xstrdup(origin);
		result->flags = data->flags;
		struct ScanPortArgs scan_port_args = {
			.flags = data->flags,
			.portsdir = data->portsdir,
			.path = path,
			.keyquery = data->keyquery,
			.query = data->query,
			.editdist = data->editdist,
			.result = result,
			.default_option_descriptions = data->default_option_descriptions,
		};
		scan_port(&scan_port_args);
		portscan_status_inc();
		free(path);
		array_append(retval, result);
	}

	free(data);
	return retval;
}

void *
lookup_origins_worker(void *userdata)
{
	struct CategoryReaderData *data = userdata;
	struct CategoryReaderResult *result = xmalloc(sizeof(struct CategoryReaderResult));
	result->error_origins = array_new();
	result->error_msgs = array_new();
	result->nonexistent = array_new();
	result->unhooked = array_new();
	result->unsorted = array_new();
	result->origins = array_new();

	for (size_t i = data->start; i < data->end; i++) {
		portscan_status_print();
		char *category = array_get(data->categories, i);
		char *path = str_printf("%s/Makefile", category);
		lookup_subdirs(data->portsdir, category, path, data->flags, result->origins, result->nonexistent, result->unhooked, result->unsorted, result->error_origins, result->error_msgs);
		portscan_status_inc();
		free(path);
	}

	free(data);

	return result;
}

struct Array *
lookup_origins(int portsdir, enum ScanFlags flags, struct PortscanLog *log)
{
	struct Array *retval = array_new();

	struct Array *categories = array_new();
	struct Array *error_origins = array_new();
	struct Array *error_msgs = array_new();
	lookup_subdirs(portsdir, "", "Makefile", SCAN_NOTHING, categories, NULL, NULL, NULL, error_origins, error_msgs);

	portscan_status_reset(PORTSCAN_STATUS_CATEGORIES, array_len(categories));
	ssize_t n_threads = sysconf(_SC_NPROCESSORS_ONLN);
	if (n_threads < 0) {
		err(1, "sysconf");
	}
	pthread_t *tid = reallocarray(NULL, n_threads, sizeof(pthread_t));
	if (tid == NULL) {
		err(1, "reallocarray");
	}
	size_t start = 0;
	size_t step = array_len(categories) / n_threads + 1;
	size_t end = MIN(start + step, array_len(categories));
	for (ssize_t i = 0; i < n_threads; i++) {
		struct CategoryReaderData *data = xmalloc(sizeof(struct CategoryReaderData));
		data->portsdir = portsdir;
		data->categories = categories;
		data->start = start;
		data->end = end;
		data->flags = flags;
		if (pthread_create(&tid[i], NULL, lookup_origins_worker, data) != 0) {
			err(1, "pthread_create");
		}

		start = MIN(start + step, array_len(categories));
		end = MIN(end + step, array_len(categories));
	}

	struct Array *nonexistent = array_new();
	struct Array *unhooked = array_new();
	struct Array *unsorted = array_new();
	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}

		struct CategoryReaderResult *result = data;
		for (size_t j = 0; j < array_len(result->error_origins); j++) {
			char *origin = array_get(result->error_origins, j);
			char *msg = array_get(result->error_msgs, j);
			array_append(error_origins, origin);
			array_append(error_msgs, msg);
		}
		ARRAY_FOREACH(result->nonexistent, char *, origin) {
			array_append(nonexistent, origin);
		}
		ARRAY_FOREACH(result->unhooked, char *, origin) {
			array_append(unhooked, origin);
		}
		ARRAY_FOREACH(result->unsorted, char *, origin) {
			array_append(unsorted, origin);
		}
		ARRAY_FOREACH(result->origins, char *, origin) {
			array_append(retval, origin);
		}
		array_free(result->error_origins);
		array_free(result->error_msgs);
		array_free(result->nonexistent);
		array_free(result->unhooked);
		array_free(result->unsorted);
		array_free(result->origins);
		free(result);
	}

	ARRAY_FOREACH(error_origins, char *, origin) {
		char *msg = array_get(error_msgs, origin_index);
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_ERROR, origin, msg);
		free(origin);
		free(msg);
	}
	array_free(error_origins);
	array_free(error_msgs);

	array_sort(nonexistent, str_compare, NULL);
	ARRAY_FOREACH(nonexistent, char *, origin) {
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_NONEXISTENT_PORT, origin, "entry without existing directory");
		free(origin);
	}
	array_free(nonexistent);

	array_sort(unhooked, str_compare, NULL);
	ARRAY_FOREACH(unhooked, char *, origin) {
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_UNHOOKED_PORT, origin, "unhooked port");
		free(origin);
	}
	array_free(unhooked);

	array_sort(unsorted, str_compare, NULL);
	ARRAY_FOREACH(unsorted, char *, origin) {
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_UNSORTED, origin, "unsorted category or other formatting issues");
		free(origin);
	}
	array_free(unsorted);

	ARRAY_FOREACH(categories, char *, category) {
		free(category);
	}
	array_free(categories);

	free(tid);

	// Get consistent output even when category Makefiles are
	// not sorted correctly
	array_sort(retval, str_compare, NULL);

	return retval;
}

PARSER_EDIT(get_default_option_descriptions)
{
	struct Array *desctokens = array_new();
	struct Map *default_option_descriptions = map_new(str_compare, NULL, free, free);
	ARRAY_FOREACH(ptokens, struct Token *, t) {
		switch (token_type(t)) {
		case VARIABLE_TOKEN:
			if (str_endswith(variable_name(token_variable(t)), "_DESC")) {
				array_append(desctokens, token_data(t));
			}
			break;
		case VARIABLE_END:
			if (!map_contains(default_option_descriptions, variable_name(token_variable(t)))) {
				map_add(default_option_descriptions, xstrdup(variable_name(token_variable(t))), str_join(desctokens, " "));
			}
			array_truncate(desctokens);
			break;
		default:
			break;
		}
	}

	array_free(desctokens);

	struct Map **retval = (struct Map **)userdata;
	*retval = default_option_descriptions;

	return NULL;
}

void
scan_ports(int portsdir, struct Array *origins, enum ScanFlags flags, struct Regexp *keyquery, struct Regexp *query, ssize_t editdist, struct PortscanLog *retval)
{
	if (!(flags & (SCAN_CLONES |
		       SCAN_COMMENTS |
		       SCAN_OPTION_DEFAULT_DESCRIPTIONS |
		       SCAN_OPTIONS |
		       SCAN_UNKNOWN_TARGETS |
		       SCAN_UNKNOWN_VARIABLES |
		       SCAN_VARIABLE_VALUES))) {
		return;
	}

	FILE *in = fileopenat(portsdir, "Mk/bsd.options.desc.mk");
	if (in == NULL) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk",
			str_printf("fileopenat: %s", strerror(errno)));
		return;
	}

	struct ParserSettings settings;
	parser_init_settings(&settings);
	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk", parser_error_tostring(parser));
		parser_free(parser);
		fclose(in);
		return;
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk", parser_error_tostring(parser));
		parser_free(parser);
		fclose(in);
		return;
	}

	struct Map *default_option_descriptions = NULL;
	if (parser_edit(parser, get_default_option_descriptions, &default_option_descriptions) != PARSER_ERROR_OK) {
		portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_ERROR, "Mk/bsd.options.desc.mk", parser_error_tostring(parser));
		parser_free(parser);
		fclose(in);
		return;
	}
	assert(default_option_descriptions);

	parser_free(parser);
	parser = NULL;
	fclose(in);
	in = NULL;

	ssize_t n_threads = sysconf(_SC_NPROCESSORS_ONLN);
	if (n_threads < 0) {
		err(1, "sysconf");
	}
	pthread_t *tid = reallocarray(NULL, n_threads, sizeof(pthread_t));
	if (tid == NULL) {
		err(1, "reallocarray");
	}

	size_t start = 0;
	size_t step = array_len(origins) / n_threads + 1;
	size_t end = MIN(start + step, array_len(origins));
	for (ssize_t i = 0; i < n_threads; i++) {
		struct PortReaderData *data = xmalloc(sizeof(struct PortReaderData));
		data->portsdir = portsdir;
		data->origins = origins;
		data->start = start;
		data->end = end;
		data->keyquery = keyquery;
		data->query = query;
		data->editdist = editdist;
		data->flags = flags;
		data->default_option_descriptions = default_option_descriptions;
		if (pthread_create(&tid[i], NULL, scan_ports_worker, data) != 0) {
			err(1, "pthread_create");
		}
		start = MIN(start + step, array_len(origins));
		end = MIN(end + step, array_len(origins));
	}

	struct Array *results = array_new();
	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}
		struct Array *result = data;
		ARRAY_FOREACH(result, struct ScanResult *, r) {
			array_append(results, r);
		}
		array_free(result);
	}
	ARRAY_FOREACH(results, struct ScanResult *, r) {
		portscan_status_print();
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_ERROR, r->origin, r->errors);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_VAR, r->origin, r->unknown_variables);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_TARGET, r->origin, r->unknown_targets);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_DUPLICATE_VAR, r->origin, r->clones);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION_DEFAULT_DESCRIPTION, r->origin, r->option_default_descriptions);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION_GROUP, r->origin, r->option_groups);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION, r->origin, r->options);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_VARIABLE_VALUE, r->origin, r->variable_values);
		portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_COMMENT, r->origin, r->comments);
		free(r->origin);
		free(r);
	}
	array_free(results);

	map_free(default_option_descriptions);
	free(tid);
}

void
usage()
{
	fprintf(stderr, "usage: portscan [-l <logdir>] [-o <flag>] [-q <regexp>] [-p <portsdir>] [<origin1> ...]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	const char *portsdir_path = getenv("PORTSDIR");
	const char *logdir_path = NULL;
	const char *keyquery = NULL;
	const char *query = NULL;
	const char *editdiststr = NULL;
	const char *progressintervalstr = NULL;
	unsigned int progressinterval = 0;
	int ch;
	enum ScanFlags flags = SCAN_NOTHING;
	while ((ch = getopt(argc, argv, "l:q:o:p:")) != -1) {
		switch (ch) {
		case 'l':
			logdir_path = optarg;
			break;
		case 'q':
			query = optarg;
			break;
		case 'o':
			if (strcasecmp(optarg, "all") == 0) {
				flags = ~SCAN_NOTHING;
			} else if (strcasecmp(optarg, "categories") == 0) {
				flags |= SCAN_CATEGORIES;
			} else if (strcasecmp(optarg, "clones") == 0) {
				flags |= SCAN_CLONES;
			} else if (strcasecmp(optarg, "comments") == 0) {
				flags |= SCAN_COMMENTS;
			} else if (strcasecmp(optarg, "option-default-descriptions") == 0) {
				flags |= SCAN_OPTION_DEFAULT_DESCRIPTIONS;
			} else if (strncasecmp(optarg, "option-default-descriptions=", strlen("option-default-descriptions=")) == 0) {
				flags |= SCAN_OPTION_DEFAULT_DESCRIPTIONS;
				editdiststr = optarg + strlen("option-default-descriptions=");
			} else if (strcasecmp(optarg, "options") == 0) {
				flags |= SCAN_OPTIONS;
			} else if (strcasecmp(optarg, "progress") == 0) {
				progressinterval = 5;
			} else if (strncasecmp(optarg, "progress=", strlen("progress=")) == 0) {
				progressintervalstr = optarg + strlen("progress=");
			} else if (strcasecmp(optarg, "unknown-targets") == 0) {
				flags |= SCAN_UNKNOWN_TARGETS;
			} else if (strcasecmp(optarg, "unknown-variables") == 0) {
				flags |= SCAN_UNKNOWN_VARIABLES;
			} else if (strncasecmp(optarg, "variable-values=", strlen("variable-values=")) == 0) {
				keyquery = optarg + strlen("variable-values=");
				flags |= SCAN_VARIABLE_VALUES;
			} else if (strcasecmp(optarg, "variable-values") == 0) {
				flags |= SCAN_VARIABLE_VALUES;
			} else {
				warnx("unknown -o flag");
				usage();
			}
			break;
		case 'p':
			portsdir_path = optarg;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (flags == SCAN_NOTHING) {
		flags = SCAN_CATEGORIES | SCAN_CLONES | SCAN_COMMENTS |
			SCAN_OPTION_DEFAULT_DESCRIPTIONS | SCAN_UNKNOWN_TARGETS |
			SCAN_UNKNOWN_VARIABLES;
	}

	if (portsdir_path == NULL) {
		portsdir_path = "/usr/ports";
	}

#if HAVE_CAPSICUM
	if (caph_limit_stdio() < 0) {
		err(1, "caph_limit_stdio");
	}

	closefrom(STDERR_FILENO + 1);
	close(STDIN_FILENO);
#endif

	int portsdir = open(portsdir_path, O_DIRECTORY);
	if (portsdir == -1) {
		err(1, "open: %s", portsdir_path);
	}

	struct PortscanLogDir *logdir = NULL;
	FILE *out = stdout;
	if (logdir_path != NULL) {
		logdir = portscan_log_dir_open(logdir_path, portsdir);
		if (logdir == NULL) {
			err(1, "portscan_log_dir_open: %s", logdir_path);
		}
		fclose(out);
		out = NULL;
	}

#if HAVE_CAPSICUM
	if (caph_limit_stream(portsdir, CAPH_LOOKUP | CAPH_READ | CAPH_READDIR) < 0) {
		err(1, "caph_limit_stream");
	}

	if (caph_enter() < 0) {
		err(1, "caph_enter");
	}
#endif

	if (progressintervalstr) {
		const char *error;
		progressinterval = strtonum(progressintervalstr, 1, 100000000, &error);
		if (error) {
			errx(1, "-o progress=%s is %s (must be >=1)", progressintervalstr, error);
		}
	}
	portscan_status_init(progressinterval);

	struct Regexp *keyquery_regexp = NULL;
	if (keyquery) {
		keyquery_regexp = regexp_new_from_str(keyquery, REG_EXTENDED);
		if (keyquery_regexp == NULL) {
			errx(1, "invalid regexp");
		}
	}
	struct Regexp *query_regexp = NULL;
	if (query) {
		query_regexp = regexp_new_from_str(query, REG_EXTENDED);
		if (query_regexp == NULL) {
			errx(1, "invalid regexp");
		}
	}

	ssize_t editdist = 3;
	if (editdiststr) {
		const char *error;
		editdist = strtonum(editdiststr, 0, INT_MAX, &error);
		if (error) {
			errx(1, "-o option-default-descriptions=%s is %s (must be >=0)", editdiststr, error);
		}
	}

	struct PortscanLog *result = portscan_log_new();
	struct Array *origins = NULL;
	if (argc == 0) {
		origins = lookup_origins(portsdir, flags, result);
	} else {
		flags |= SCAN_PARTIAL;
		origins = array_new();
		for (int i = 0; i < argc; i++) {
			array_append(origins, xstrdup(argv[i]));
		}
	}

	int status = 0;
	portscan_status_reset(PORTSCAN_STATUS_PORTS, array_len(origins));
	scan_ports(portsdir, origins, flags, keyquery_regexp, query_regexp, editdist, result);
	if (portscan_log_len(result) > 0) {
		if (logdir != NULL) {
			struct PortscanLog *prev_result = portscan_log_read_all(logdir, PORTSCAN_LOG_LATEST);
			if (portscan_log_compare(prev_result, result)) {
				warnx("no changes compared to previous result");
				status = 2;
				goto cleanup;
			}
			if (!portscan_log_serialize_to_dir(result, logdir)) {
				err(1, "portscan_log_serialize_to_dir");
			}
		} else {
			if (!portscan_log_serialize_to_file(result, out)) {
				err(1, "portscan_log_serialize");
			}
		}
	}

	if (progressinterval) {
		portscan_status_reset(PORTSCAN_STATUS_FINISHED, 0);
		portscan_status_print();
	}

cleanup:
	regexp_free(keyquery_regexp);
	regexp_free(query_regexp);
	portscan_log_dir_close(logdir);
	portscan_log_free(result);

	ARRAY_FOREACH(origins, char *, origin) {
		free(origin);
	}
	array_free(origins);

	return status;
}
