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
# include "capsicum_helpers.h"
#endif
#include <assert.h>
#include <dirent.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
#include "conditional.h"
#include "diff.h"
#include "mainutils.h"
#include "parser.h"
#include "parser/plugin.h"
#include "portscanlog.h"
#include "set.h"
#include "token.h"
#include "util.h"

enum ScanFlags {
	SCAN_NOTHING = 0,
	SCAN_CATEGORIES = 1 << 0,
	SCAN_CLONES = 1 << 1,
	SCAN_OPTIONS = 1 << 2,
	SCAN_UNKNOWN_TARGETS = 1 << 3,
	SCAN_UNKNOWN_VARIABLES = 1 << 4,
};

struct ScanResult {
	char *origin;
	struct Set *errors;
	struct Set *unknown_variables;
	struct Set *unknown_targets;
	struct Set *clones;
	struct Set *option_groups;
	struct Set *options;
	enum ScanFlags flags;
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
	enum ScanFlags flags;
};

static void lookup_subdirs(int, const char *, const char *, enum ScanFlags, struct Array *, struct Array *, struct Array *, struct Array *, struct Array *, struct Array *);
static void scan_port(int, const char *, struct ScanResult *);
static void *lookup_origins_worker(void *);
static enum ParserError process_include(struct Parser *, struct Set *, const char *, int, const char *);
static struct Array *extract_includes(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
static DIR *diropenat(int, const char *);
static FILE *fileopenat(int, const char *);
static void *scan_ports_worker(void *);
static struct Array *lookup_origins(int, enum ScanFlags, struct PortscanLog *);
static void scan_ports(int, struct Array *, enum ScanFlags, struct PortscanLog *);
static void usage(void);

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
		char *msg;
		xasprintf(&msg, "fileopenat: %s", strerror(errno));
		array_append(error_origins, xstrdup(path));
		array_append(error_msgs, msg);
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
			char *msg;
			xasprintf(&msg, "diropenat: %s", strerror(errno));
			array_append(error_msgs, msg);
		} else {
			struct dirent *dp;
			while ((dp = readdir(dir)) != NULL) {
				if (dp->d_name[0] == '.') {
					continue;
				}
				char *path;
				xasprintf(&path, "%s/%s", category, dp->d_name);
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

	for (size_t i = 0; i < array_len(tmp); i++) {
		char *port = array_get(tmp, i);
		char *origin;
		if (flags != SCAN_NOTHING) {
			xasprintf(&origin, "%s/%s", category, port);
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
	} else if (str_startswith(filename, "${.CURDIR}/")) {
		filename += strlen("${.CURDIR}/");
		xasprintf(&path, "%s/%s", curdir, filename);
	} else if (str_startswith(filename, "${.CURDIR:H}/")) {
		filename += strlen("${.CURDIR:H}/");
		xasprintf(&path, "%s/../%s", curdir, filename);
	} else if (str_startswith(filename, "${.CURDIR:H:H}/")) {
		filename += strlen("${.CURDIR:H:H}/");
		xasprintf(&path, "%s/../../%s", curdir, filename);
	} else if (str_startswith(filename, "${PORTSDIR}/")) {
		filename += strlen("${PORTSDIR}/");
		path = xstrdup(filename);
	} else if (str_startswith(filename, "${FILESDIR}/")) {
		filename += strlen("${FILESDIR}/");
		xasprintf(&path, "%s/files/%s", curdir, filename);
	} else {
		xasprintf(&path, "%s/%s", curdir, filename);
	}
	FILE *f = fileopenat(portsdir, path);
	if (f == NULL) {
		char *msg;
		xasprintf(&msg, "cannot open include: %s: %s", path, strerror(errno));
		set_add(errors, msg);
		free(path);
		return PARSER_ERROR_OK;
	}
	free(path);
	enum ParserError error = parser_read_from_file(parser, f);
	fclose(f);
	return error;
}

struct Array *
extract_includes(struct Parser *parser, struct Array *tokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	struct Array **retval = (struct Array **)userdata;

	struct Array *includes = array_new();
	int found = 0;
	for (size_t i = 0; i < array_len(tokens); i++) {
		struct Token *t = array_get(tokens, i);
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

void
scan_port(int portsdir, const char *path, struct ScanResult *retval)
{
	retval->errors = set_new(str_compare, NULL, free);
	retval->option_groups = set_new(str_compare, NULL, free);
	retval->options = set_new(str_compare, NULL, free);

	struct ParserSettings settings;
	parser_init_settings(&settings);
	settings.behavior = PARSER_OUTPUT_RAWLINES;

	FILE *in = fileopenat(portsdir, path);
	if (in == NULL) {
		char *msg;
		xasprintf(&msg, "fileopenat: %s", strerror(errno));
		set_add(retval->errors, msg);
		return;
	}

	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		set_add(retval->errors, xstrdup(parser_error_tostring(parser)));
		goto cleanup;
	}

	struct Array *includes = NULL;
	error = parser_edit_with_fn(parser, extract_includes, &includes);
	if (error != PARSER_ERROR_OK) {
		set_add(retval->errors, xstrdup(parser_error_tostring(parser)));
		goto cleanup;
	}
	for (size_t i = 0; i < array_len(includes); i++) {
		error = process_include(parser, retval->errors, retval->origin, portsdir, array_get(includes, i));
		if (error != PARSER_ERROR_OK) {
			array_free(includes);
			set_add(retval->errors, xstrdup(parser_error_tostring(parser)));
			goto cleanup;
		}
	}
	array_free(includes);

	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		set_add(retval->errors, xstrdup(parser_error_tostring(parser)));
		goto cleanup;
	}

	if (retval->flags & SCAN_UNKNOWN_VARIABLES) {
		error = parser_edit(parser, "output.unknown-variables", &retval->unknown_variables);
		if (error != PARSER_ERROR_OK) {
			char *msg;
			xasprintf(&msg, "output.unknown-variables: %s", parser_error_tostring(parser));
			set_add(retval->errors, msg);
			goto cleanup;
		}
	}

	if (retval->flags & SCAN_UNKNOWN_TARGETS) {
		error = parser_edit(parser, "output.unknown-targets", &retval->unknown_targets);
		if (error != PARSER_ERROR_OK) {
			char *msg;
			xasprintf(&msg, "output.unknown-targets: %s", parser_error_tostring(parser));
			set_add(retval->errors, msg);
			goto cleanup;
		}
	}

	if (retval->flags & SCAN_CLONES) {
		error = parser_edit(parser, "lint.clones", &retval->clones);
		if (error != PARSER_ERROR_OK) {
			char *msg;
			xasprintf(&msg, "lint.clones: %s", parser_error_tostring(parser));
			set_add(retval->errors, msg);
			goto cleanup;
		}
	}

	if (retval->flags & SCAN_OPTIONS) {
		struct Set *groups = parser_metadata(parser, PARSER_METADATA_OPTION_GROUPS);
		SET_FOREACH (groups, const char *, group) { 
			set_add(retval->option_groups, xstrdup(group));
		}
		struct Set *options = parser_metadata(parser, PARSER_METADATA_OPTIONS);
		retval->options = set_new(str_compare, NULL, free);
		SET_FOREACH (options, const char *, option) {
			set_add(retval->options, xstrdup(option));
		}
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
		return retval;
	}

	assert(data->start < data->end);

	for (size_t i = data->start; i < data->end; i++) {
		char *origin = array_get(data->origins, i);
		char *path;
		xasprintf(&path, "%s/Makefile", origin);
		struct ScanResult *result = xmalloc(sizeof(struct ScanResult));
		result->origin = xstrdup(origin);
		result->flags = data->flags;
		scan_port(data->portsdir, path, result);
		free(path);
		array_append(retval, result);
	}

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
		char *category = array_get(data->categories, i);
		char *path;
		xasprintf(&path, "%s/Makefile", category);
		lookup_subdirs(data->portsdir, category, path, data->flags, result->origins, result->nonexistent, result->unhooked, result->unsorted, result->error_origins, result->error_msgs);
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
		for (size_t j = 0; j < array_len(result->nonexistent); j++) {
			char *origin = array_get(result->nonexistent, j);
			array_append(nonexistent, origin);
		}
		for (size_t j = 0; j < array_len(result->unhooked); j++) {
			char *origin = array_get(result->unhooked, j);
			array_append(unhooked, origin);
		}
		for (size_t j = 0; j < array_len(result->unsorted); j++) {
			char *origin = array_get(result->unsorted, j);
			array_append(unsorted, origin);
		}
		for (size_t j = 0; j < array_len(result->origins); j++) {
			char *origin = array_get(result->origins, j);
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

	for (size_t i = 0; i < array_len(error_origins); i++) {
		char *origin = array_get(error_origins, i);
		char *msg = array_get(error_msgs, i);
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_ERROR, origin, msg);
		free(origin);
		free(msg);
	}
	array_free(error_origins);
	array_free(error_msgs);

	array_sort(nonexistent, str_compare, NULL);
	for (size_t j = 0; j < array_len(nonexistent); j++) {
		char *origin = array_get(nonexistent, j);
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_NONEXISTENT_PORT, origin, "entry without existing directory");
		free(origin);
	}
	array_free(nonexistent);

	array_sort(unhooked, str_compare, NULL);
	for (size_t j = 0; j < array_len(unhooked); j++) {
		char *origin = array_get(unhooked, j);
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_UNHOOKED_PORT, origin, "unhooked port");
		free(origin);
	}
	array_free(unhooked);

	array_sort(unsorted, str_compare, NULL);
	for (size_t j = 0; j < array_len(unsorted); j++) {
		char *origin = array_get(unsorted, j);
		portscan_log_add_entry(log, PORTSCAN_LOG_ENTRY_CATEGORY_UNSORTED, origin, "unsorted category or other formatting issues");
		free(origin);
	}
	array_free(unsorted);

	for (size_t i = 0; i < array_len(categories); i++) {
		free(array_get(categories, i));
	}
	array_free(categories);

	free(tid);

	// Get consistent output even when category Makefiles are
	// not sorted correctly
	array_sort(retval, str_compare, NULL);

	return retval;
}

void
scan_ports(int portsdir, struct Array *origins, enum ScanFlags flags, struct PortscanLog *retval)
{
	if (!(flags & (SCAN_CLONES |
		       SCAN_OPTIONS |
		       SCAN_UNKNOWN_TARGETS |
		       SCAN_UNKNOWN_VARIABLES))) {
		return;
	}

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
		data->flags = flags;
		if (pthread_create(&tid[i], NULL, scan_ports_worker, data) != 0) {
			err(1, "pthread_create");
		}
		start = MIN(start + step, array_len(origins));
		end = MIN(end + step, array_len(origins));
	}

	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}
		struct Array *result = data;
		for (size_t j = 0; j < array_len(result); j++) {
			struct ScanResult *r = array_get(result, j);
			portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_ERROR, r->origin, r->errors);
			portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_VAR, r->origin, r->unknown_variables);
			portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_TARGET, r->origin, r->unknown_targets);
			portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_DUPLICATE_VAR, r->origin, r->clones);
			portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION_GROUP, r->origin, r->option_groups);
			portscan_log_add_entries(retval, PORTSCAN_LOG_ENTRY_OPTION, r->origin, r->options);
			free(r->origin);
			free(r);
		}
		array_free(result);
	}

	free(tid);
}

void
usage()
{
	fprintf(stderr, "usage: portscan [-l <logdir>] [-o <flag>] -p <portsdir> [<origin1> ...]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	const char *portsdir_path = NULL;
	const char *logdir_path = NULL;
	int ch;
	enum ScanFlags flags = SCAN_NOTHING;
	while ((ch = getopt(argc, argv, "l:o:p:")) != -1) {
		switch (ch) {
		case 'l':
			logdir_path = optarg;
			break;
		case 'o':
			if (strcasecmp(optarg, "all") == 0) {
				flags = ~SCAN_NOTHING;
			} else if (strcasecmp(optarg, "categories") == 0) {
				flags |= SCAN_CATEGORIES;
			} else if (strcasecmp(optarg, "clones") == 0) {
				flags |= SCAN_CLONES;
			} else if (strcasecmp(optarg, "options") == 0) {
				flags |= SCAN_OPTIONS;
			} else if (strcasecmp(optarg, "unknown-targets") == 0) {
				flags |= SCAN_UNKNOWN_TARGETS;
			} else if (strcasecmp(optarg, "unknown-variables") == 0) {
				flags |= SCAN_UNKNOWN_VARIABLES;
			} else {
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
		flags = SCAN_CLONES | SCAN_UNKNOWN_TARGETS | SCAN_UNKNOWN_VARIABLES;
	}

	int portsdir = -1;
	if (portsdir_path == NULL) {
		usage();
	}

	parser_plugin_load_all();

#if HAVE_CAPSICUM
	if (caph_limit_stdio() < 0) {
		err(1, "caph_limit_stdio");
	}

	closefrom(STDERR_FILENO + 1);
	close(STDIN_FILENO);
#endif

	portsdir = open(portsdir_path, O_DIRECTORY);
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

	struct PortscanLog *result = portscan_log_new();
	struct Array *origins = NULL;
	if (argc == 0) {
		origins = lookup_origins(portsdir, flags, result);
	} else {
		origins = array_new();
		for (int i = 0; i < argc; i++) {
			array_append(origins, xstrdup(argv[i]));
		}
	}

	int status = 0;
	scan_ports(portsdir, origins, flags, result);
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

cleanup:
	portscan_log_dir_close(logdir);
	portscan_log_free(result);

	for (size_t i = 0; i < array_len(origins); i++) {
		free(array_get(origins, i));
	}
	array_free(origins);

	return status;
}
