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
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
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
#include <time.h>
#include <unistd.h>

#include "array.h"
#include "conditional.h"
#include "diff.h"
#include "mainutils.h"
#include "parser.h"
#include "parser/plugin.h"
#include "token.h"
#include "util.h"

#define PORTSCAN_LOG_DATE_FORMAT "portscan-%Y%m%d%H%M%S"
#define PORTSCAN_LOG_LATEST "portscan-latest.log"
#define PORTSCAN_LOG_PREVIOUS "portscan-previous.log"
#define PORTSCAN_LOG_INIT "/dev/null"

struct ScanResult {
	char *origin;
	struct Array *unknown_variables;
	struct Array *unknown_targets;
	struct Array *clones;
	struct Array *option_groups;
	struct Array *options;
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

enum LogEntryType {
	LOG_ENTRY_UNKNOWN_VAR,
	LOG_ENTRY_UNKNOWN_TARGET,
	LOG_ENTRY_DUPLICATE_VAR,
	LOG_ENTRY_OPTION_GROUP,
	LOG_ENTRY_OPTION,
};

struct LogEntry {
	enum LogEntryType type;
	char *origin;
	char *value;
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
static struct Array *scan_ports(int, struct Array *, int);
static int log_compare(struct Array *, struct Array *);
static void log_add_entry(struct Array *, enum LogEntryType, const char *, struct Array *);
static int log_entry_compare(const void *, const void *, void *);
static void log_entry_parse(const char *, struct LogEntry *);
static char *log_filename(const char *);
static char *log_revision(int);
static FILE *log_open(int, const char *);
static int log_open_dir(const char *);
struct Array *log_read_all(int, const char *);
static void log_update_latest(int, const char *);
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
	retval->unknown_targets = array_new();
	retval->unknown_variables = array_new();
	retval->clones = array_new();
	retval->option_groups = array_new();
	retval->options = array_new();

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

	struct Array *tmp = NULL;
	error = parser_edit(parser, "output.unknown-variables", &tmp);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}
	for (size_t i = 0; i < array_len(tmp); i++) {
		array_append(retval->unknown_variables, xstrdup(array_get(tmp, i)));
	}
	array_free(tmp);

	error = parser_edit(parser, "output.unknown-targets", &tmp);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}
	for (size_t i = 0; i < array_len(tmp); i++) {
		array_append(retval->unknown_targets, xstrdup(array_get(tmp, i)));
	}
	array_free(tmp);

	error = parser_edit(parser, "lint.clones", &tmp);
	if (error != PARSER_ERROR_OK) {
		warnx("%s: %s", path, parser_error_tostring(parser));
		goto cleanup;
	}
	for (size_t i = 0; i < array_len(tmp); i++) {
		array_append(retval->clones, xstrdup(array_get(tmp, i)));
	}

	array_free(tmp);
	if (retval->include_options) {
		parser_port_options(parser, &tmp, NULL);
		for (size_t i = 0; i < array_len(tmp); i++) {
			array_append(retval->option_groups, xstrdup(array_get(tmp, i)));
		}
		parser_port_options(parser, NULL, &tmp);
		for (size_t i = 0; i < array_len(tmp); i++) {
			array_append(retval->options, xstrdup(array_get(tmp, i)));
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

struct Array *
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

	struct Array *retval = array_new();
	for (ssize_t i = 0; i < n_threads; i++) {
		void *data;
		if (pthread_join(tid[i], &data) != 0) {
			err(1, "pthread_join");
		}
		struct Array *result = data;
		for (size_t j = 0; j < array_len(result); j++) {
			struct ScanResult *r = array_get(result, j);
			log_add_entry(retval, LOG_ENTRY_UNKNOWN_VAR, r->origin, r->unknown_variables);
			log_add_entry(retval, LOG_ENTRY_UNKNOWN_TARGET, r->origin, r->unknown_targets);
			log_add_entry(retval, LOG_ENTRY_DUPLICATE_VAR, r->origin, r->clones);
			log_add_entry(retval, LOG_ENTRY_OPTION_GROUP, r->origin, r->option_groups);
			log_add_entry(retval, LOG_ENTRY_OPTION, r->origin, r->options);
			free(r->origin);
			free(r);
		}
		array_free(result);
	}

	array_sort(retval, log_entry_compare, NULL);
	free(tid);

	return retval;
}

void
log_add_entry(struct Array *log, enum LogEntryType type, const char *origin, struct Array *values)
{
	array_sort(values, str_compare, NULL);

	for (size_t k = 0; k < array_len(values); k++) {
		char *value = array_get(values, k);
		char *buf;
		switch (type) {
		case LOG_ENTRY_UNKNOWN_VAR:
			xasprintf(&buf, "%-7c %-40s %s\n", 'V', origin, value);
			break;
		case LOG_ENTRY_UNKNOWN_TARGET:
			xasprintf(&buf, "%-7c %-40s %s\n", 'T', origin, value);
			break;
		case LOG_ENTRY_DUPLICATE_VAR:
			xasprintf(&buf, "%-7s %-40s %s\n", "Vc", origin, value);
			break;
		case LOG_ENTRY_OPTION_GROUP:
			xasprintf(&buf, "%-7s %-40s %s\n", "OG", origin, value);
			break;
		case LOG_ENTRY_OPTION:
			xasprintf(&buf, "%-7c %-40s %s\n", 'O', origin, value);
			break;
		default:
			abort();
		}
		array_append(log, buf);
		free(value);
	}

	array_free(values);
}

void
log_entry_parse(const char *s, struct LogEntry *e)
{
	if (str_startswith(s, "V ")) {
		e->type = LOG_ENTRY_UNKNOWN_VAR;
		s++;
	} else if (str_startswith(s, "T ")) {
		e->type = LOG_ENTRY_UNKNOWN_TARGET;
		s++;
	} else if (str_startswith(s, "Vc ")) {
		e->type = LOG_ENTRY_DUPLICATE_VAR;
		s += 2;
	} else if (str_startswith(s, "OG ")) {
		e->type = LOG_ENTRY_OPTION_GROUP;
		s += 2;
	} else if (str_startswith(s, "O ")) {
		e->type = LOG_ENTRY_OPTION;
		s++;
	} else {
		errx(1, "unable to parse: %s", s);
	}

	while (*s != 0 && isspace(*s)) {
		s++;
	}
	const char *origin_start = s;
	while (*s != 0 && !isspace(*s)) {
		s++;
	}
	const char *value = s;
	while (*value != 0 && isspace(*value)) {
		value++;
	}
	size_t value_len = strlen(value);
	if (value_len > 0 && value[value_len - 1] == '\n') {
		value_len--;
	}

	e->origin = xstrndup(origin_start, s - origin_start);
	e->value = xstrndup(value, value_len);

	if (strlen(e->origin) == 0 || strlen(value) == 0) {
		errx(1, "unable to parse: %s", s);
	}
}

int
log_entry_compare(const void *ap, const void *bp, void *userdata)
{
	const char *aline = *(const char **)ap;
	const char *bline = *(const char **)bp;

	struct LogEntry a;
	log_entry_parse(aline, &a);
	struct LogEntry b;
	log_entry_parse(bline, &b);

	int retval = strcmp(a.origin, b.origin);

	if (retval == 0) {
		if (a.type > b.type) {
			retval = 1;
		} else if (a.type < b.type) {
			retval = -1;
		} else {
			retval = strcmp(a.value, b.value);
		}
	}

	free(a.origin);
	free(a.value);
	free(b.origin);
	free(b.value);

	return retval;
}

int
log_compare(struct Array *prev_result, struct Array *result)
{
	struct diff p;
	int rc = array_diff(prev_result, result, &p, str_compare, NULL);
	if (rc <= 0) {
		errx(1, "array_diff failed");
	}
	int equal = 1;
	for (size_t i = 0; i < p.sessz; i++) {
		if (p.ses[i].type != DIFF_COMMON) {
			equal = 0;
			break;
		}
	}
	free(p.ses);
	free(p.lcs);
	for (size_t i = 0; i < array_len(prev_result); i++) {
		free(array_get(prev_result, i));
	}
	array_free(prev_result);

	return equal;
}

char *
log_filename(const char *rev)
{
	time_t date = time(NULL);
	if (date == -1) {
		err(1, "time");
	}
	struct tm *tm = gmtime(&date);

	char buf[PATH_MAX];
	if (strftime(buf, sizeof(buf), PORTSCAN_LOG_DATE_FORMAT, tm) == 0) {
		errx(1, "strftime: buffer too small");
	}

	char *log_path;
	xasprintf(&log_path, "%s-%s.log", buf, rev);

	return log_path;
}

char *
log_revision(int portsdir)
{
	if (fchdir(portsdir) == -1) {
		err(1, "fchdir");
	}

	FILE *fp = popen("if [ -d .svn ]; then svn info --show-item revision --no-newline 2>/dev/null; exit; fi; if [ -d .git ]; then git rev-parse HEAD 2>/dev/null; fi", "r");
	if (fp == NULL) {
		err(1, "popen");
	}

	char *revision = NULL;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	if ((linelen = getline(&line, &linecap, fp)) > 0) {
		if (linelen > 0 && line[linelen - 1] == '\n') {
			line[linelen - 1] = 0;
		}

		if (strlen(line) == 40) {
			// Assume git commit
			xasprintf(&revision, "%s", line);
		} else {
			xasprintf(&revision, "r%s", line);
		}
	}
	free(line);
	pclose(fp);

	if (revision == NULL) {
		revision = xstrdup("unknown");
	}
	return revision;
}

FILE *
log_open(int logdir, const char *log_path)
{
	int outfd = openat(logdir, log_path, O_CREAT | O_WRONLY, 0660);
	if (outfd == -1) {
		err(1, "openat: %s", log_path);
	}

	FILE *f = fdopen(outfd, "w");
	if (f == NULL) {
		err(1, "fdopen");
	}

	return f;
}

int
log_open_dir(const char *logdir_path)
{
	int created_dir = 0;
	int logdir;
	while ((logdir = open(logdir_path, O_DIRECTORY)) == -1) {
		if (errno == ENOENT) {
			if (mkdir(logdir_path, 0777) == -1) {
				return -1;
			}
			created_dir = 1;
		} else {
			return -1;
		}
	}
	if (created_dir) {
		if (symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_PREVIOUS) == -1) {
			return -1;
		}
		if (symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_LATEST) == -1) {
			return -1;
		}
	} else {
		char *prev = read_symlink(logdir, PORTSCAN_LOG_PREVIOUS);
		if (prev == NULL &&
		    symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_PREVIOUS) == -1) {
			return -1;
		}
		free(prev);

		char *latest = read_symlink(logdir, PORTSCAN_LOG_LATEST);
		if (latest == NULL &&
		    symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_LATEST) == -1) {
			return -1;
		}
		free(latest);
	}

	return logdir;
}

struct Array *
log_read_all(int logdir, const char *log_path)
{
	struct Array *log = array_new();

	char *buf = read_symlink(logdir, log_path);
	if (buf == NULL) {
		if (errno == ENOENT) {
			return log;
		} else if (errno != EINVAL) {
			err(1, "read_symlink: %s", log_path);
		}
	} else if (strcmp(buf, PORTSCAN_LOG_INIT) == 0) {
		free(buf);
		return log;
	}
	free(buf);

	int fd = openat(logdir, log_path, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT) {
			return log;
		}
		err(1, "openat: %s", log_path);
	}

	FILE *fp = fdopen(fd, "r");
	if (fp == NULL) {
		close(fd);
		return log;
	}

	ssize_t linelen;
	size_t linecap = 0;
	char *line = NULL;
	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		array_append(log, xstrdup(line));
	}
	free(line);
	fclose(fp);

	array_sort(log, log_entry_compare, NULL);

	return log;
}

void
log_update_latest(int logdir, const char *log_path)
{
	char *prev = NULL;
	if (!update_symlink(logdir, log_path, PORTSCAN_LOG_LATEST, &prev)) {
		err(1, "update_symlink");
	}
	if (prev != NULL && !update_symlink(logdir, prev, PORTSCAN_LOG_PREVIOUS, NULL)) {
		err(1, "update_symlink");
	}
	free(prev);
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

	int logdir = -1;
	FILE *out = stdout;
	char *log_rev = NULL;
	if (logdir_path != NULL) {
		logdir = log_open_dir(logdir_path);
		if (logdir == -1) {
			err(1, "log_open_dir: %s", logdir_path);
		}
		log_rev = log_revision(portsdir);
		fclose(out);
		out = NULL;
	}

#if HAVE_CAPSICUM
	if (caph_limit_stream(portsdir, CAPH_LOOKUP | CAPH_READ) < 0) {
		err(1, "caph_limit_stream");
	}

	if (logdir > -1 && caph_limit_stream(logdir, CAPH_CREATE | CAPH_READ | CAPH_SYMLINK) < 0) {
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
	struct Array *result = scan_ports(portsdir, origins, oflag);
	char *log_path = NULL;
	if (array_len(result) > 0) {
		if (logdir != -1) {
			struct Array *prev_result = log_read_all(logdir, PORTSCAN_LOG_LATEST);
			if (log_compare(prev_result, result)) {
				warnx("no changes compared to previous result");
				status = 2;
				goto cleanup;
			}
			log_path = log_filename(log_rev);
			out = log_open(logdir, log_path);
		}
		for (size_t i = 0; i < array_len(result); i++) {
			char *line = array_get(result, i);
			if (write(fileno(out), line, strlen(line)) == -1) {
				err(1, "write");
			}
			free(line);
		}
		if (logdir != -1) {
			log_update_latest(logdir, log_path);
			logdir = -1;
		}
	}

cleanup:
	if (logdir != -1) {
		close(logdir);
	}
	free(log_path);
	free(log_rev);
	array_free(result);

	for (size_t i = 0; i < array_len(origins); i++) {
		free(array_get(origins, i));
	}
	array_free(origins);

	return status;
}
