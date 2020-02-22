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
#if HAVE_CAPSICUM
# include <sys/capsicum.h>
# include "capsicum_helpers.h"
#endif
#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
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

struct ScanResult {
	char *origin;
	struct Set *unknown_variables;
	struct Set *unknown_targets;
	struct Set *clones;
	struct Set *option_groups;
	struct Set *options;
	int include_options;
};

struct CategoryReaderData {
	int portsdir;
	struct Array *categories;
	size_t start;
	size_t end;
};

struct PortReaderData {
	int portsdir;
	struct Array *origins;
	size_t start;
	size_t end;
	int include_options;
};

// Ignore these ports when processing .include
static const char *ports_include_blacklist_[] = {
	"devel/llvm",
	"ports-mgmt/wanted-ports",
	"lang/gnatdroid-armv7",
};

static struct Array *lookup_subdirs(int, const char *);
static void lookup_unknowns(int, const char *, struct ScanResult *);
static void *lookup_origins_worker(void *);
static enum ParserError process_include(struct Parser *, const char *, int, const char *);
static struct Array *extract_includes(struct Parser *, struct Array *, enum ParserError *, char **, const void *);
static FILE *fileopenat(int, const char *);
static void *scan_ports_worker(void *);
static struct Array *lookup_origins(int);
static struct PortscanLog *scan_ports(int, struct Array *, int);
static void usage(void);

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

struct Array *
lookup_subdirs(int portsdir, const char *path)
{
	struct Array *subdirs = array_new();

	FILE *in = fileopenat(portsdir, path);
	if (in == NULL) {
		warn("open_file: %s", path);
		return subdirs;
	}

	struct ParserSettings settings;
	parser_init_settings(&settings);

	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	struct Array *tmp;
	if (parser_lookup_variable_all(parser, "SUBDIR", &tmp, NULL) == NULL) {
		goto cleanup;
	}

	for (size_t i = 0; i < array_len(tmp); i++) {
		array_append(subdirs, xstrdup(array_get(tmp, i)));
	}
	array_free(tmp);

cleanup:
	parser_free(parser);
	fclose(in);
	return subdirs;
}

enum ParserError
process_include(struct Parser *parser, const char *curdir, int portsdir, const char *filename)
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
		warn("open_file: %s", path);
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
lookup_unknowns(int portsdir, const char *path, struct ScanResult *retval)
{
	retval->option_groups = set_new(str_compare, NULL, free);
	retval->options = set_new(str_compare, NULL, free);

	struct ParserSettings settings;
	parser_init_settings(&settings);
	settings.behavior = PARSER_OUTPUT_RAWLINES;

	FILE *in = fileopenat(portsdir, path);
	if (in == NULL) {
		warn("open_file: %s", path);
		return;
	}

	struct Parser *parser = parser_new(&settings);
	enum ParserError error = parser_read_from_file(parser, in);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	int ignore_port = 0;
	for (size_t i = 0; i < nitems(ports_include_blacklist_); i++) {
		if (strcmp(retval->origin, ports_include_blacklist_[i]) == 0) {
			ignore_port = 1;
			break;
		}
	}
	if (!ignore_port) {
		struct Array *includes = NULL;
		error = parser_edit_with_fn(parser, extract_includes, &includes);
		if (error != PARSER_ERROR_OK) {
			warnx("%s: %s", path, parser_error_tostring(parser));
			goto cleanup;
		}
		for (size_t i = 0; i < array_len(includes); i++) {
			error = process_include(parser, retval->origin, portsdir, array_get(includes, i));
			if (error != PARSER_ERROR_OK) {
				array_free(includes);
				warnx("%s: %s", path, parser_error_tostring(parser));
				goto cleanup;
			}
		}
		array_free(includes);
	}

	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	error = parser_edit(parser, "output.unknown-variables", &retval->unknown_variables);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	error = parser_edit(parser, "output.unknown-targets", &retval->unknown_targets);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	error = parser_edit(parser, "lint.clones", &retval->clones);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	if (retval->include_options) {
		struct Set *groups;
		parser_port_options(parser, &groups, NULL);
		SET_FOREACH (groups, const char *, group) { 
			set_add(retval->option_groups, xstrdup(group));
		}
		struct Set *options;
		parser_port_options(parser, NULL, &options);
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
		result->include_options = data->include_options;
		lookup_unknowns(data->portsdir, path, result);
		free(path);
		array_append(retval, result);
	}

	return retval;
}

void *
lookup_origins_worker(void *userdata)
{
	struct CategoryReaderData *data = userdata;
	struct Array *origins = array_new();

	for (size_t i = data->start; i < data->end; i++) {
		char *category = array_get(data->categories, i);
		char *path;
		xasprintf(&path, "%s/Makefile", category);
		struct Array *ports = lookup_subdirs(data->portsdir, path);
		free(path);
		for (size_t j = 0; j < array_len(ports); j++) {
			char *port = array_get(ports, j);
			xasprintf(&path, "%s/%s", category, port);
			array_append(origins, path);
			free(port);
		}
		array_free(ports);
	}

	free(data);

	return origins;
}

struct Array *
lookup_origins(int portsdir)
{
	struct Array *retval = array_new();

	struct Array *categories = lookup_subdirs(portsdir, "Makefile");
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
		if (pthread_create(&tid[i], NULL, lookup_origins_worker, data) != 0) {
			err(1, "pthread_create");
		}

		start = MIN(start + step, array_len(categories));
		end = MIN(end + step, array_len(categories));
	}
	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}

		struct Array *origins = data;
		for (size_t j = 0; j < array_len(origins); j++) {
			char *origin = array_get(origins, j);
			array_append(retval, origin);
		}
		array_free(origins);
	}

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

struct PortscanLog *
scan_ports(int portsdir, struct Array *origins, int include_options)
{
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
		data->include_options = include_options;
		if (pthread_create(&tid[i], NULL, scan_ports_worker, data) != 0) {
			err(1, "pthread_create");
		}
		start = MIN(start + step, array_len(origins));
		end = MIN(end + step, array_len(origins));
	}

	struct PortscanLog *retval = portscan_log_new();
	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}
		struct Array *result = data;
		for (size_t j = 0; j < array_len(result); j++) {
			struct ScanResult *r = array_get(result, j);
			portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_VAR, r->origin, r->unknown_variables);
			portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_UNKNOWN_TARGET, r->origin, r->unknown_targets);
			portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_DUPLICATE_VAR, r->origin, r->clones);
			portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_OPTION_GROUP, r->origin, r->option_groups);
			portscan_log_add_entry(retval, PORTSCAN_LOG_ENTRY_OPTION, r->origin, r->options);
			free(r->origin);
			free(r);
		}
		array_free(result);
	}

	free(tid);

	return retval;
}

void
usage()
{
	fprintf(stderr, "usage: portscan [-l <logdir>] [-o] -p <portsdir> [<origin1> ...]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	const char *portsdir_path = NULL;
	const char *logdir_path = NULL;
	int ch;
	int oflag = 0;
	while ((ch = getopt(argc, argv, "l:op:")) != -1) {
		switch (ch) {
		case 'l':
			logdir_path = optarg;
			break;
		case 'o':
			oflag = 1;
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
	if (caph_limit_stream(portsdir, CAPH_LOOKUP | CAPH_READ) < 0) {
		err(1, "caph_limit_stream");
	}

	if (caph_enter() < 0) {
		err(1, "caph_enter");
	}
#endif

	struct Array *origins = NULL;
	if (argc == 0) {
		origins = lookup_origins(portsdir);
	} else {
		origins = array_new();
		for (int i = 0; i < argc; i++) {
			array_append(origins, xstrdup(argv[i]));
		}
	}

	int status = 0;
	struct PortscanLog *result = scan_ports(portsdir, origins, oflag);
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
