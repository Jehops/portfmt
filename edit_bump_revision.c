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
#if HAVE_ERR
# include <err.h>
#endif
#include <regex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "parser.h"
#include "rules.h"
#include "token.h"
#include "util.h"
#include "variable.h"

static char *get_revision(struct Parser *, const char *, enum ParserError *, char **);

static char *
get_revision(struct Parser *parser, const char *variable, enum ParserError *error, char **error_msg)
{
	char *revision;
	char *comment;
	char *current_revision;
	struct Variable *var;
	if ((var = parser_lookup_variable_str(parser, variable, &current_revision, &comment)) != NULL) {
		const char *errstr = NULL;
		int rev = strtonum(current_revision, 0, INT_MAX, &errstr);
		free(current_revision);
		if (errstr == NULL) {
			rev++;
		} else {
			*error = PARSER_ERROR_EXPECTED_INT;
			*error_msg = xstrdup(errstr);
			free(comment);
			return NULL;
		}
		char *buf = variable_tostring(var);
		if (parser_lookup_variable(parser, "MASTERDIR", NULL, NULL)) {
			// In slave ports we do not delete the variable first since
			// they have a non-uniform structure and edit_merge will probably
			// insert it into a non-optimal position.
			xasprintf(&revision, "%s%d %s\n", buf, rev, comment);
		} else {
			xasprintf(&revision, "%s!=\n%s%d %s\n", variable, buf, rev, comment);
		}
		free(buf);
		free(comment);
	} else {
		xasprintf(&revision, "%s=1", variable);
	}

	return revision;
}

struct Array *
edit_bump_revision(struct Parser *parser, struct Array *ptokens, enum ParserError *error, char **error_msg, const void *userdata)
{
	const char *variable = userdata;
	if (variable == NULL) {
		variable = "PORTREVISION";
	}

	char *revision = get_revision(parser, variable, error, error_msg);
	if (revision == NULL) {
		return NULL;
	}

	struct ParserSettings settings = parser_settings(parser);
	struct Parser *subparser = parser_new(&settings);
	*error = parser_read_from_buffer(subparser, revision, strlen(revision));
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}
	*error = parser_read_finish(subparser);
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}
	*error = parser_merge(parser, subparser, PARSER_MERGE_SHELL_IS_DELETE | PARSER_MERGE_OPTIONAL_LIKE_ASSIGN);
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}

cleanup:
	free(revision);
	parser_free(subparser);

	return NULL;
}
