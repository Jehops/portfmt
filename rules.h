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

#include "config.h"

#include <regex.h>

#include "variable.h"

enum RegularExpression {
	RE_CONDITIONAL = 0,
	RE_CONTINUE_LINE,
	RE_EMPTY_LINE,
	RE_LICENSE_NAME,
	RE_LICENSE_PERMS,
	RE_OPTIONS_HELPER,
	RE_OPT_USE,
	RE_OPT_VARS,
	RE_PLIST_FILES,
	RE_PLIST_KEYWORDS,
	RE_MODIFIER,
	RE_TARGET,
	RE_VAR,
};

int compare_tokens(struct Variable *, const char *, const char *);
void compile_regular_expressions(void);
int ignore_wrap_col(struct Variable *);
int indent_goalcol(struct Variable *);
int leave_unsorted(struct Variable *);
int matches(enum RegularExpression, const char *, regmatch_t *);
int preserve_eol_comment(const char *);
int print_as_newlines(struct Variable *);
int skip_goalcol(struct Variable *);
char *sub(enum RegularExpression, const char *, const char *);
int target_command_should_wrap(char *);
