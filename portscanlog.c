/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Tobias Kortkamp <tobik@FreeBSD.org>
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
#include <sys/stat.h>
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "array.h"
#include "diff.h"
#include "portscanlog.h"
#include "util.h"

struct PortscanLogDir {
	int fd;
	char *path;
	char *rev;
};

struct PortscanLog {
	struct Array *entries;
};

struct PortscanLogEntry {
	enum PortscanLogEntryType type;
	char *origin;
	char *value;
};

#define PORTSCAN_LOG_DATE_FORMAT "portscan-%Y%m%d%H%M%S"
#define PORTSCAN_LOG_INIT "/dev/null"

static void portscan_log_sort(struct PortscanLog *);
static char *log_entry_tostring(const struct PortscanLogEntry *);
static int log_entry_compare(const void *, const void *, void *);
static struct PortscanLogEntry *log_entry_parse(const char *);

static FILE *log_open(struct PortscanLogDir *, const char *);
static int log_update_latest(struct PortscanLogDir *, const char *);
static char *log_filename(const char *);
static char *log_revision(int);

struct PortscanLog *
portscan_log_new()
{
	struct PortscanLog *log = xmalloc(sizeof(struct PortscanLog));
	log->entries = array_new();
	return log;
}

void
portscan_log_free(struct PortscanLog *log)
{
	if (log == NULL) {
		return;
	}

	for (size_t i = 0; i < array_len(log->entries); i++) {
		struct PortscanLogEntry *entry = array_get(log->entries, i);
		free(entry->origin);
		free(entry->value);
		free(entry);
	}
	array_free(log->entries);
	free(log);
}

void
portscan_log_sort(struct PortscanLog *log)
{
	array_sort(log->entries, log_entry_compare, NULL);
}

size_t
portscan_log_len(struct PortscanLog *log)
{
	return array_len(log->entries);
}

char *
log_entry_tostring(const struct PortscanLogEntry *entry)
{
	char *buf;
	switch (entry->type) {
	case PORTSCAN_LOG_ENTRY_UNKNOWN_VAR:
		xasprintf(&buf, "%-7c %-40s %s\n", 'V', entry->origin, entry->value);
		break;
	case PORTSCAN_LOG_ENTRY_UNKNOWN_TARGET:
		xasprintf(&buf, "%-7c %-40s %s\n", 'T', entry->origin, entry->value);
		break;
	case PORTSCAN_LOG_ENTRY_DUPLICATE_VAR:
		xasprintf(&buf, "%-7s %-40s %s\n", "Vc", entry->origin, entry->value);
		break;
	case PORTSCAN_LOG_ENTRY_OPTION_GROUP:
		xasprintf(&buf, "%-7s %-40s %s\n", "OG", entry->origin, entry->value);
		break;
	case PORTSCAN_LOG_ENTRY_OPTION:
		xasprintf(&buf, "%-7c %-40s %s\n", 'O', entry->origin, entry->value);
		break;
	default:
		abort();
	}

	return buf;
}

void
portscan_log_add_entry(struct PortscanLog *log, enum PortscanLogEntryType type, const char *origin, struct Array *values)
{
	array_sort(values, str_compare, NULL);
	for (size_t k = 0; k < array_len(values); k++) {
		char *value = array_get(values, k);
		struct PortscanLogEntry *entry = xmalloc(sizeof(struct PortscanLogEntry));
		entry->type = type;
		entry->origin = xstrdup(origin);
		entry->value = xstrdup(value);
		array_append(log->entries, entry);
		free(value);
	}

	array_free(values);
}

struct PortscanLogEntry *
log_entry_parse(const char *s)
{
	enum PortscanLogEntryType type = PORTSCAN_LOG_ENTRY_UNKNOWN_VAR;
	if (str_startswith(s, "V ")) {
		type = PORTSCAN_LOG_ENTRY_UNKNOWN_VAR;
		s++;
	} else if (str_startswith(s, "T ")) {
		type = PORTSCAN_LOG_ENTRY_UNKNOWN_TARGET;
		s++;
	} else if (str_startswith(s, "Vc ")) {
		type = PORTSCAN_LOG_ENTRY_DUPLICATE_VAR;
		s += 2;
	} else if (str_startswith(s, "OG ")) {
		type = PORTSCAN_LOG_ENTRY_OPTION_GROUP;
		s += 2;
	} else if (str_startswith(s, "O ")) {
		type = PORTSCAN_LOG_ENTRY_OPTION;
		s++;
	} else {
		fprintf(stderr, "unable to parse log entry: %s\n", s);
		return NULL;
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

	char *origin = xstrndup(origin_start, s - origin_start);
	char *entry_value = xstrndup(value, value_len);

	if (strlen(origin) == 0 || strlen(entry_value) == 0) {
		fprintf(stderr, "unable to parse log entry: %s\n", s);
		free(origin);
		free(entry_value);
		return NULL;
	}

	struct PortscanLogEntry *e = xmalloc(sizeof(struct PortscanLogEntry));
	e->type = type;
	e->origin = origin;
	e->value = entry_value;
	return e;
}

int
log_entry_compare(const void *ap, const void *bp, void *userdata)
{
	const struct PortscanLogEntry *a = *(const struct PortscanLogEntry **)ap;
	const struct PortscanLogEntry *b = *(const struct PortscanLogEntry **)bp;

	int retval = strcmp(a->origin, b->origin);
	if (retval == 0) {
		if (a->type > b->type) {
			retval = 1;
		} else if (a->type < b->type) {
			retval = -1;
		} else {
			retval = strcmp(a->value, b->value);
		}
	}

	return retval;
}

int
portscan_log_compare(struct PortscanLog *prev, struct PortscanLog *log)
{
	portscan_log_sort(prev);
	portscan_log_sort(log);

	struct diff p;
	int rc = array_diff(prev->entries, log->entries, &p, log_entry_compare, NULL);
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

	return equal;
}

int
portscan_log_serialize_to_file(struct PortscanLog *log, FILE *out)
{
	portscan_log_sort(log);

	for (size_t i = 0; i < array_len(log->entries); i++) {
		const struct PortscanLogEntry *entry = array_get(log->entries, i);
		char *line = log_entry_tostring(entry);
		if (write(fileno(out), line, strlen(line)) == -1) {
			free(line);
			return 0;
		}
		free(line);
	}

	return 1;
}

FILE *
log_open(struct PortscanLogDir *logdir, const char *log_path)
{
	int outfd = openat(logdir->fd, log_path, O_CREAT | O_WRONLY, 0660);
	if (outfd == -1) {
		return NULL;
	}

	FILE *f = fdopen(outfd, "w");
	if (f == NULL) {
		return NULL;
	}

	return f;
}

int
log_update_latest(struct PortscanLogDir *logdir, const char *log_path)
{
	char *prev = NULL;
	if (!update_symlink(logdir->fd, log_path, PORTSCAN_LOG_LATEST, &prev)) {
		free(prev);
		return 0;
	}
	if (prev != NULL && !update_symlink(logdir->fd, prev, PORTSCAN_LOG_PREVIOUS, NULL)) {
		free(prev);
		return 0;
	}
	free(prev);
	return 1;
}

char *
log_filename(const char *rev)
{
	time_t date = time(NULL);
	if (date == -1) {
		return NULL;
	}
	struct tm *tm = gmtime(&date);

	char buf[PATH_MAX];
	if (strftime(buf, sizeof(buf), PORTSCAN_LOG_DATE_FORMAT, tm) == 0) {
		return NULL;
	}

	char *log_path;
	xasprintf(&log_path, "%s-%s.log", buf, rev);

	return log_path;
}

int
portscan_log_serialize_to_dir(struct PortscanLog *log, struct PortscanLogDir *logdir)
{
	char *log_path = log_filename(logdir->rev);
	FILE *out = log_open(logdir, log_path);
	if (out == NULL) {
		free(log_path);
		return 0;
	}
	if (!portscan_log_serialize_to_file(log, out) ||
	    !log_update_latest(logdir, log_path)) {
		fclose(out);
		free(log_path);
		return 0;
	}

	fclose(out);
	free(log_path);
	return 1;
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

struct PortscanLogDir *
portscan_log_dir_open(const char *logdir_path, int portsdir)
{
	int created_dir = 0;
	int logdir;
	while ((logdir = open(logdir_path, O_DIRECTORY)) == -1) {
		if (errno == ENOENT) {
			if (mkdir(logdir_path, 0777) == -1) {
				return NULL;
			}
			created_dir = 1;
		} else {
			return NULL;
		}
	}
	if (created_dir) {
		if (symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_PREVIOUS) == -1) {
			return NULL;
		}
		if (symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_LATEST) == -1) {
			return NULL;
		}
	} else {
		char *prev = read_symlink(logdir, PORTSCAN_LOG_PREVIOUS);
		if (prev == NULL &&
		    symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_PREVIOUS) == -1) {
			return NULL;
		}
		free(prev);

		char *latest = read_symlink(logdir, PORTSCAN_LOG_LATEST);
		if (latest == NULL &&
		    symlinkat(PORTSCAN_LOG_INIT, logdir, PORTSCAN_LOG_LATEST) == -1) {
			return NULL;
		}
		free(latest);
	}

#if HAVE_CAPSICUM
	if (caph_limit_stream(logdir, CAPH_CREATE | CAPH_READ | CAPH_SYMLINK) < 0) {
		err(1, "caph_limit_stream");
	}
#endif
	struct PortscanLogDir *dir = xmalloc(sizeof(struct PortscanLogDir));
	dir->fd = logdir;
	dir->path = xstrdup(logdir_path);
	dir->rev = log_revision(portsdir);

	return dir;
}

void
portscan_log_dir_close(struct PortscanLogDir *dir)
{
	if (dir == NULL) {
		return;
	}
	close(dir->fd);
	free(dir->path);
	free(dir->rev);
	free(dir);
}

struct PortscanLog *
portscan_log_read_all(struct PortscanLogDir *logdir, const char *log_path)
{
	struct PortscanLog *log = portscan_log_new();

	char *buf = read_symlink(logdir->fd, log_path);
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

	int fd = openat(logdir->fd, log_path, O_RDONLY);
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
		struct PortscanLogEntry *entry = log_entry_parse(line);
		if (entry != NULL) {
			array_append(log->entries, entry);
		}
	}
	free(line);
	fclose(fp);

	portscan_log_sort(log);

	return log;
}

