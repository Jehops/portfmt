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

struct Array *
edit_set_version(struct Parser *parser, struct Array *ptokens, enum ParserError *error, const void *userdata)
{
	const char *ver = "DISTVERSION";
	if (parser_lookup_variable(parser, "PORTVERSION", NULL, NULL)) {
		ver = "PORTVERSION";
	}

	char *revision;
	int rev = 0;
	if (parser_lookup_variable_str(parser, "PORTREVISION", &revision, NULL)) {
		const char *errstr = NULL;
		rev = strtonum(revision, 0, INT_MAX, &errstr);
		free(revision);
		if (errstr != NULL) {
			*error = PARSER_ERROR_EXPECTED_INT;
			return NULL;
		}
	}

	char *buf;
	if (rev > 0) {
		// Remove PORTREVISION
		xasprintf(&buf, "%s=%s\nPORTREVISION!=", ver, userdata);
	} else {
		xasprintf(&buf, "%s=%s", ver, userdata);
	}

	struct ParserSettings settings = parser_settings(parser);
	struct Parser *subparser = parser_new(&settings);
	*error = parser_read_from_buffer(subparser, buf, strlen(buf));
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}
	*error = parser_read_finish(subparser);
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}
	*error = parser_edit(parser, edit_merge, &(struct EditMergeParams){subparser, 1});
	if (*error != PARSER_ERROR_OK) {
		goto cleanup;
	}

cleanup:
	parser_free(subparser);
	free(buf);

	return NULL;
}
