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

/* Order is significant here and should match variable_order_ in rules.c */
enum BlockType {
	BLOCK_PORTNAME,
	BLOCK_PATCHFILES,
	BLOCK_MAINTAINER,
	BLOCK_LICENSE,
	BLOCK_LICENSE_OLD,
	BLOCK_BROKEN,
	BLOCK_DEPENDS,
	BLOCK_FLAVORS,
	BLOCK_FLAVORS_HELPER,
	BLOCK_USES,
	BLOCK_SHEBANGFIX,
	BLOCK_UNIQUEFILES,
	BLOCK_APACHE,
	BLOCK_ELIXIR,
	BLOCK_EMACS,
	BLOCK_ERLANG,
	BLOCK_CMAKE,
	BLOCK_CONFIGURE,
	BLOCK_QMAKE,
	BLOCK_MESON,
	BLOCK_SCONS,
	BLOCK_CABAL,
	BLOCK_CARGO1,
	BLOCK_CARGO2,
	BLOCK_GO,
	BLOCK_LAZARUS,
	BLOCK_LINUX,
	BLOCK_NUGET,
	BLOCK_MAKE,
	BLOCK_CFLAGS,
	BLOCK_CONFLICTS,
	BLOCK_STANDARD,
	BLOCK_WRKSRC,
	BLOCK_USERS,
	BLOCK_PLIST,
	BLOCK_OPTDEF,
	BLOCK_OPTDESC,
	BLOCK_OPTHELPER,
	BLOCK_UNKNOWN,
};

enum RegularExpression {
	RE_CONDITIONAL = 0,
	RE_CONTINUE_LINE,
	RE_EMPTY_LINE,
	RE_FLAVORS_HELPER,
	RE_LICENSE_NAME,
	RE_LICENSE_PERMS,
	RE_OPTIONS_GROUP,
	RE_OPTIONS_HELPER,
	RE_OPT_USE_PREFIX,
	RE_OPT_USE,
	RE_OPT_VARS,
	RE_PLIST_FILES,
	RE_PLIST_KEYWORDS,
	RE_MODIFIER,
	RE_TARGET,
	RE_VAR,
};

struct Token;
struct Variable;
struct Match;

const char *blocktype_tostring(enum BlockType);
int compare_order(const void *, const void *);
int compare_tokens(const void *, const void *);
int ignore_wrap_col(struct Variable *);
int indent_goalcol(struct Variable *);
int is_comment(struct Token *);
int leave_unsorted(struct Variable *);
regex_t *regex(enum RegularExpression);
int matches(enum RegularExpression, const char *);
int preserve_eol_comment(struct Token *);
int print_as_newlines(struct Variable *);
void rules_init(void);
int skip_goalcol(struct Variable *);
char *sub(enum RegularExpression, const char *, const char *);
int target_command_should_wrap(char *);
enum BlockType variable_order_block(const char *);
