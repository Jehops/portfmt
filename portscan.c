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
#include <sys/param.h>
#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
#include "mainutils.h"
#include "parser.h"
#include "util.h"

struct ScanResult {
	char *origin;
	struct Array *unknown_variables;
	struct Array *unknown_targets;
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
};

static struct Array *lookup_subdirs(int, const char *);
static void lookup_unknowns(int, const char *, struct ScanResult *);
static void *lookup_origins_worker(void *);
static FILE *fileopenat(int, const char *);
static void *scan_ports_worker(void *);
static struct Array *lookup_origins(int);
struct Array *scan_ports(int, struct Array *, int);
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
	struct Array *subdirs = array_new(sizeof(char *));

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
		warnx("%s", parser_error_tostring(parser));
		goto cleanup;
	}
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		warnx("%s", parser_error_tostring(parser));
		goto cleanup;
	}

	struct Array *tmp;
	if (parser_lookup_variable_all(parser, "SUBDIR", &tmp, NULL) == NULL) {
		warnx("%s", parser_error_tostring(parser));
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

void
lookup_unknowns(int portsdir, const char *path, struct ScanResult *retval)
{
	retval->unknown_targets = array_new(sizeof(char *));
	retval->unknown_variables = array_new(sizeof(char *));

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
	error = parser_read_finish(parser);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}
	struct Array *tmp = NULL;
	error = parser_edit(parser, edit_output_unknown_variables, &tmp);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	for (size_t i = 0; i < array_len(tmp); i++) {
		array_append(retval->unknown_variables, xstrdup(array_get(tmp, i)));
	}
	array_free(tmp);

	error = parser_edit(parser, edit_output_unknown_targets, &tmp);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}

	for (size_t i = 0; i < array_len(tmp); i++) {
		array_append(retval->unknown_targets, xstrdup(array_get(tmp, i)));
	}
	array_free(tmp);

cleanup:
	parser_free(parser);
	fclose(in);
}

void *
scan_ports_worker(void *userdata)
{
	struct PortReaderData *data = userdata;
	struct Array *retval = array_new(sizeof(char *));

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
	struct Array *origins = array_new(sizeof(char *));

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
	struct Array *retval = array_new(sizeof(char *));

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
	array_sort(retval, str_compare);

	return retval;
}

struct Array *
scan_ports(int portsdir, struct Array *origins, int can_use_colors)
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
		if (pthread_create(&tid[i], NULL, scan_ports_worker, data) != 0) {
			err(1, "pthread_create");
		}
		start = MIN(start + step, array_len(origins));
		end = MIN(end + step, array_len(origins));
	}

	struct Array *retval = array_new(sizeof(struct iovec*));
	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}
		struct Array *result = data;
		for (size_t j = 0; j < array_len(result); j++) {
			struct ScanResult *r = array_get(result, j);

			array_sort(r->unknown_variables, str_compare);
			for (size_t k = 0; k < array_len(r->unknown_variables); k++) {
				char *var = array_get(r->unknown_variables, k);
				char *buf;
				if (can_use_colors) {
					xasprintf(&buf, "%s%-7c%s %-40s %s%s%s\n",
						ANSI_COLOR_CYAN, 'V', ANSI_COLOR_RESET,
						r->origin,
						ANSI_COLOR_CYAN, var, ANSI_COLOR_RESET);
				} else {
					xasprintf(&buf, "%-7c %-40s %s\n", 'V', r->origin, var);
				}
				array_append(retval, buf);
				free(var);
			}
			array_free(r->unknown_variables);

			array_sort(r->unknown_targets, str_compare);
			for (size_t k = 0; k < array_len(r->unknown_targets); k++) {
				char *target = array_get(r->unknown_targets, k);
				char *buf;
				if (can_use_colors) {
					xasprintf(&buf, "%s%-7c%s %-40s %s%s%s\n",
						ANSI_COLOR_MAGENTA, 'T', ANSI_COLOR_RESET,
						r->origin,
						ANSI_COLOR_MAGENTA, target, ANSI_COLOR_RESET);
				} else {
					xasprintf(&buf, "%-7c %-40s %s\n", 'T', r->origin, target);
				}
				array_append(retval, buf);
				free(target);
			}
			array_free(r->unknown_targets);

			free(r->origin);
			free(r);
		}
		array_free(result);
	}

	return retval;
}

void
usage()
{
	fprintf(stderr, "usage: portscan -p <portsdir> [<origin1> ...]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	const char *portsdir_path = NULL;
	int ch;
	while ((ch = getopt(argc, argv, "p:")) != -1) {
		switch (ch) {
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

#if HAVE_CAPSICUM
	if (caph_limit_stdio() < 0) {
		err(1, "caph_limit_stdio");
	}

	closefrom(STDERR_FILENO + 1);
	close(STDIN_FILENO);
#endif

	portsdir = open(portsdir_path, O_DIRECTORY);
	if (portsdir == -1) {
		err(1, "open");
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
		origins = array_new(sizeof(char *));
		for (int i = 0; i < argc; i++) {
			array_append(origins, xstrdup(argv[i]));
		}
	}

	struct Array *result = scan_ports(portsdir, origins, can_use_colors(stdout));
	for (size_t i = 0; i < array_len(result); i++) {
		char *line = array_get(result, i);
		if (write(STDOUT_FILENO, line, strlen(line)) == -1) {
			err(1, "write");
		}
		free(line);
	}
	array_free(result);

	for (size_t i = 0; i < array_len(origins); i++) {
		free(array_get(origins, i));
	}
	array_free(origins);

	return 0;
}
