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

static char *get_revision(struct Parser *, const char *, enum ParserError *);

static char *
get_revision(struct Parser *parser, const char *variable, enum ParserError *error)
{
	char *revision;
	char *comment;
	char *current_revision;
	if (parser_lookup_variable_str(parser, variable, &current_revision, &comment)) {
		const char *errstr = NULL;
		int rev = strtonum(current_revision, 0, INT_MAX, &errstr);
		free(current_revision);
		if (errstr == NULL) {
			rev++;
		} else {
			*error = PARSER_ERROR_EXPECTED_INT;
			free(comment);
			return NULL;
		}
		xasprintf(&revision, "%s=%d %s", variable, rev, comment);
		free(comment);
	} else {
		xasprintf(&revision, "%s=1", variable);
	}

	return revision;
}

struct Array *
edit_bump_revision(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	const char *variable = userdata;
	if (variable == NULL) {
		variable = "PORTREVISION";
	}

	char *revision = get_revision(parser, variable, error);
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
	*error = parser_edit(parser, edit_merge, subparser);
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}

cleanup:
	free(revision);
	parser_free(subparser);

	return NULL;
}
