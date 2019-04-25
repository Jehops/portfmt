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

#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rules.h"
#include "util.h"
#include "variable.h"

#define free(x)

struct Variable {
	char *name;
	enum VariableModifier modifier;
};

struct Variable *
variable_new(char *buf) {
	struct Variable *var = xmalloc(sizeof(struct Variable));

	regmatch_t match;
	if (!matches(RE_MODIFIER, buf, &match)) {
		errx(1, "Variable with no modifier");
	}

	var->name = str_trim(str_substr_dup(buf, 0, match.rm_so));

	char *modifier = str_substr_dup(buf, match.rm_so, match.rm_eo);
	if (strcmp(modifier, "+=") == 0) {
		var->modifier = MODIFIER_APPEND;
	} else if (strcmp(modifier, "=") == 0) {
		var->modifier = MODIFIER_ASSIGN;
	} else if (strcmp(modifier, ":=") == 0) {
		var->modifier = MODIFIER_EXPAND;
	} else if (strcmp(modifier, "?=") == 0) {
		var->modifier = MODIFIER_OPTIONAL;
	} else if (strcmp(modifier, "!=") == 0) {
		var->modifier = MODIFIER_SHELL;
	} else {
		errx(1, "Unknown variable modifier: %s", modifier);
	}
	free(modifier);

	return var;
}

void
variable_free(struct Variable *var)
{
	free(var->name);
	free(var);
}

int
variable_cmp(struct Variable *a, struct Variable *b)
{
	assert(a != NULL);
	assert(b != NULL);
	return strcmp(a->name, b->name);
}

enum VariableModifier
variable_modifier(struct Variable *var)
{
	assert(var != NULL);
	return var->modifier;
}

void
variable_set_modifier(struct Variable *var, enum VariableModifier modifier)
{
	assert(var != NULL);
	var->modifier = modifier;
}

char *
variable_name(struct Variable *var)
{
	assert(var != NULL);
	return var->name;
}

char *
variable_tostring(struct Variable *var)
{
	assert(var != NULL);

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

	char *s;
	if (asprintf(&s, "%s%s", var->name, mod) < 0) {
		warn("asprintf");
		abort();
	}
	return s;
}
