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
#pragma once

enum TokenType {
	COMMENT,
	CONDITIONAL_END,
	CONDITIONAL_TOKEN,
	CONDITIONAL_START,
	TARGET_COMMAND_END,
	TARGET_COMMAND_START,
	TARGET_COMMAND_TOKEN,
	TARGET_END,
	TARGET_START,
	VARIABLE_END,
	VARIABLE_START,
	VARIABLE_TOKEN,
};

struct Range {
	size_t start;
	size_t end;
};

struct Conditional;
struct Token;
struct Variable;
struct Target;

struct Token *token_new(enum TokenType, struct Range *, const char *, char *, char *, char *);
struct Token *token_new_comment(struct Range *, const char *, struct Conditional *);
struct Token *token_new_variable_end(struct Range *, struct Variable *);
struct Token *token_new_variable_start(struct Range *, struct Variable *);
struct Token *token_new_variable_token(struct Range *, struct Variable *, const char *);
void token_free(struct Token *);
struct Token *token_as_comment(struct Token *);
struct Token *token_clone(struct Token *, const char *);
struct Conditional *token_conditional(struct Token *);
char *token_data(struct Token *);
int token_goalcol(struct Token *);
struct Range *token_lines(struct Token *);
struct Target *token_target(struct Token *);
enum TokenType token_type(struct Token *);
const char *token_type_tostring(enum TokenType);
struct Variable *token_variable(struct Token *);
void token_set_goalcol(struct Token *, int);
