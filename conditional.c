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

#include <assert.h>
#include <err.h>
#include <stdlib.h>

#include "conditional.h"
#include "rules.h"
#include "util.h"

struct Conditional {
	enum ConditionalType type;
};

struct Conditional *
conditional_new(struct sbuf *s)
{
	struct Conditional *cond = xmalloc(sizeof(struct Conditional));

	regmatch_t match;
	if (!matches(RE_CONDITIONAL, s, &match)) {
		errx(1, "not a conditional: %s", sbuf_data(s));
	}

	struct sbuf *tmp = sbuf_substr_dup(s, match.rm_so, match.rm_eo);
	sbuf_finishx(tmp);
	if (sbuf_len(tmp) < 2) {
		sbuf_delete(tmp);
		errx(1, "not a conditional: %s", sbuf_data(s));
	}

	struct sbuf *type;
	if (sbuf_data(tmp)[0] == '.') {
		struct sbuf *buf = sbuf_dupstr(sbuf_data(tmp) + 1);
		sbuf_finishx(buf);
		sbuf_delete(tmp);
		tmp = sbuf_strip_dup(buf);
		sbuf_delete(buf);
		type = sbuf_dupstr(".");
		sbuf_cat(type, sbuf_data(tmp));
		sbuf_finishx(type);
		sbuf_delete(tmp);
	} else {
		type = sbuf_strip_dup(tmp);
		sbuf_finishx(type);
		sbuf_delete(tmp);
	}

	if (sbuf_strcmp(type, "include") == 0) {
		cond->type = COND_INCLUDE_POSIX;
	} else if (sbuf_strcmp(type, ".include") == 0) {
		cond->type = COND_INCLUDE;
	} else if (sbuf_strcmp(type, ".error") == 0) {
		cond->type = COND_ERROR;
	} else if (sbuf_strcmp(type, ".export") == 0) {
		cond->type = COND_EXPORT;
	} else if (sbuf_strcmp(type, ".export-env") == 0) {
		cond->type = COND_EXPORT_ENV;
	} else if (sbuf_strcmp(type, ".export.env") == 0) {
		cond->type = COND_EXPORT_ENV;
	} else if (sbuf_strcmp(type, ".export-literal") == 0) {
		cond->type = COND_EXPORT_LITERAL;
	} else if (sbuf_strcmp(type, ".info") == 0) {
		cond->type = COND_INFO;
	} else if (sbuf_strcmp(type, ".undef") == 0) {
		cond->type = COND_UNDEF;
	} else if (sbuf_strcmp(type, ".unexport") == 0) {
		cond->type = COND_UNEXPORT;
	} else if (sbuf_strcmp(type, ".for") == 0) {
		cond->type = COND_FOR;
	} else if (sbuf_strcmp(type, ".endfor") == 0) {
		cond->type = COND_ENDFOR;
	} else if (sbuf_strcmp(type, ".unexport-env") == 0) {
		cond->type = COND_UNEXPORT_ENV;
	} else if (sbuf_strcmp(type, ".warning") == 0) {
		cond->type = COND_WARNING;
	} else if (sbuf_strcmp(type, ".if") == 0) {
		cond->type = COND_IF;
	} else if (sbuf_strcmp(type, ".ifdef") == 0) {
		cond->type = COND_IFDEF;
	} else if (sbuf_strcmp(type, ".ifndef") == 0) {
		cond->type = COND_IFNDEF;
	} else if (sbuf_strcmp(type, ".ifmake") == 0) {
		cond->type = COND_IFMAKE;
	} else if (sbuf_strcmp(type, ".ifnmake") == 0) {
		cond->type = COND_IFNMAKE;
	} else if (sbuf_strcmp(type, ".else") == 0) {
		cond->type = COND_ELSE;
	} else if (sbuf_strcmp(type, ".elif") == 0) {
		cond->type = COND_ELIF;
	} else if (sbuf_strcmp(type, ".elifdef") == 0) {
		cond->type = COND_ELIFDEF;
	} else if (sbuf_strcmp(type, ".elifndef") == 0) {
		cond->type = COND_ELIFNDEF;
	} else if (sbuf_strcmp(type, ".elifmake") == 0) {
		cond->type = COND_ELIFMAKE;
	} else if (sbuf_strcmp(type, ".endif") == 0) {
		cond->type = COND_ENDIF;
	} else {
		sbuf_delete(type);
		errx(1, "unknown conditional: %s", sbuf_data(type));
	}
	sbuf_delete(type);

	return cond;
}

void
conditional_free(struct Conditional *cond)
{
	free(cond);
}

struct sbuf *
conditional_tostring(struct Conditional *cond)
{
	const char *type = NULL;

	switch(cond->type) {
	case COND_ELIF:
		type = ".elif";
		break;
	case COND_ELIFDEF:
		type = ".elifdef";
		break;
	case COND_ELIFMAKE:
		type = ".elifmake";
		break;
	case COND_ELIFNDEF:
		type = ".elifndef";
		break;
	case COND_ELSE:
		type = ".else";
		break;
	case COND_ENDFOR:
		type = ".endfor";
		break;
	case COND_ENDIF:
		type = ".endif";
		break;
	case COND_ERROR:
		type = ".error";
		break;
	case COND_EXPORT_ENV:
		type = ".export-env";
		break;
	case COND_EXPORT_LITERAL:
		type = ".export-literal";
		break;
	case COND_EXPORT:
		type = ".export";
		break;
	case COND_FOR:
		type = ".for";
		break;
	case COND_IF:
		type = ".if";
		break;
	case COND_IFDEF:
		type = ".ifdef";
		break;
	case COND_IFMAKE:
		type = ".ifmake";
		break;
	case COND_IFNDEF:
		type = ".ifndef";
		break;
	case COND_IFNMAKE:
		type = ".ifnmake";
		break;
	case COND_INCLUDE_POSIX:
		type = "include";
		break;
	case COND_INCLUDE:
		type = ".include";
		break;
	case COND_INFO:
		type = ".info";
		break;
	case COND_UNDEF:
		type = ".undef";
		break;
	case COND_UNEXPORT_ENV:
		type = ".unexport-env";
		break;
	case COND_UNEXPORT:
		type = ".unexport";
		break;
	case COND_WARNING:
		type = ".warning";
		break;
	}
	assert(type != NULL);

	struct sbuf *buf = sbuf_dupstr(type);
	sbuf_finishx(buf);
	return buf;
}
