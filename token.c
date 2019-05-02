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

#include <stdlib.h>
#include <string.h>

#include "conditional.h"
#include "target.h"
#include "token.h"
#include "util.h"
#include "variable.h"

struct Token {
	enum TokenType type;
	char *data;
	struct Conditional *cond;
	struct Variable *var;
	struct Target *target;
	int goalcol;
	struct Range lines;
	int ignore;
	int edited;
};

struct Token *
token_new(enum TokenType type, struct Range *lines, char *data,
	  char *varname, char *condname, char *targetname)
{
	struct Token *t = xmalloc(sizeof(struct Token));

	t->type = type;
	t->data = data;
	t->lines = *lines;

	if (targetname) {
		t->target = target_new(targetname);
	}
	if (condname) {
		t->cond = conditional_new(condname);
	}
	if (varname) {
		t->var = variable_new(varname);
	}

	return t;
}

void
token_free(struct Token *token)
{
	if (token->data) {
		free(token->data);
	}
	if (token->var) {
		variable_free(token->var);
	}
	if (token->target) {
		target_free(token->target);
	}
	free(token);
}

struct Token *
token_clone(struct Token *token)
{
	struct Token *t = xmalloc(sizeof(struct Token));
	memcpy(t, token, sizeof(struct Token));
	return t;
}

struct Conditional *
token_conditional(struct Token *token)
{
	return token->cond;
}

char *
token_data(struct Token *token)
{
	return token->data;
}

int
token_edited(struct Token *token)
{
	return token->edited;
}

int
token_goalcol(struct Token *token)
{
	return token->goalcol;
}

int
token_ignore(struct Token *token)
{
	return token->ignore;
}

struct Range *
token_lines(struct Token *token)
{
	return &token->lines;
}

struct Target *
token_target(struct Token *token)
{
	return token->target;
}

enum TokenType
token_type(struct Token *token)
{
	return token->type;
}

struct Variable *
token_variable(struct Token *token)
{
	return token->var;
}

void
token_set_conditional(struct Token *token, struct Conditional *cond)
{
	token->cond = cond;
}

void
token_set_data(struct Token *token, char *data)
{
	token->data = data;
}

void
token_set_edited(struct Token *token, int edited)
{
	token->edited = edited;
}

void
token_set_goalcol(struct Token *token, int goalcol)
{
	token->goalcol = goalcol;
}

void
token_set_ignore(struct Token *token, int ignore)
{
	token->ignore = ignore;
}

void
token_set_target(struct Token *token, struct Target *target)
{
	token->target = target;
}

void
token_set_type(struct Token *token, enum TokenType type)
{
	token->type = type;
}

void
token_set_variable(struct Token *token, struct Variable *var)
{
	token->var = var;
}

