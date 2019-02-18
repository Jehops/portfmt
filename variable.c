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
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <regex.h>
#include <stdlib.h>

#include "rules.h"
#include "util.h"
#include "variable.h"

struct Variable {
	struct sbuf *name;
	enum VariableModifier modifier;
};

struct Variable *
variable_new(struct sbuf *var_with_mod) {
	struct Variable *var= malloc(sizeof(struct Variable));
	if (var == NULL) {
		err(1, "malloc");
	}

	struct sbuf *tmp = sbuf_strip_all_dup(var_with_mod);
	sbuf_finishx(tmp);

	regmatch_t match;
	if (!matches(RE_MODIFIER, tmp, &match)) {
		errx(1, "Variable with no modifier");
	}

	struct sbuf *modifier = sbuf_substr_dup(tmp, match.rm_so, match.rm_eo);
	sbuf_finishx(modifier);

	if (sbuf_strcmp(modifier, "+=") == 0) {
		var->modifier = MODIFIER_APPEND;
	} else if (sbuf_strcmp(modifier, "=") == 0) {
		var->modifier = MODIFIER_ASSIGN;
	} else if (sbuf_strcmp(modifier, ":=") == 0) {
		var->modifier = MODIFIER_EXPAND;
	} else if (sbuf_strcmp(modifier, "?=") == 0) {
		var->modifier = MODIFIER_OPTIONAL;
	} else if (sbuf_strcmp(modifier, "!=") == 0) {
		var->modifier = MODIFIER_SHELL;
	} else {
		errx(1, "Unknown variable modifier: %s", sbuf_data(modifier));
	}

	var->name = sbuf_substr_dup(tmp, 0, match.rm_so);
	sbuf_finishx(var->name);

	sbuf_delete(tmp);

	return var;
}

int
variable_cmp(struct Variable *a, struct Variable *b)
{
	assert(a != NULL);
	assert(b != NULL);
	return sbuf_cmp(a->name, b->name);
}

struct sbuf *
variable_cat(struct Variable *var)
{
	assert(var != NULL);
	struct sbuf *s = sbuf_dup(var->name);

	const char *mod = NULL;
	switch (var->modifier) {
	case MODIFIER_APPEND:
		mod = "+=";
		break;
	case MODIFIER_ASSIGN:
		mod = "=";
		break;
	case MODIFIER_EXPAND:
		mod = ":=";
		break;
	case MODIFIER_OPTIONAL:
		mod = "?=";
		break;
	case MODIFIER_SHELL:
		mod = "!=";
		break;
	default:
		errx(1, "Unknown variable modifier: %d", var->modifier);
	}

	sbuf_cat(s, mod);
	sbuf_finishx(s);
	return s;
}

enum VariableModifier
variable_modifier(struct Variable *var)
{
	assert(var != NULL);
	return var->modifier;
}

struct sbuf *
variable_name(struct Variable *var)
{
	assert(var != NULL);
	return var->name;
}

