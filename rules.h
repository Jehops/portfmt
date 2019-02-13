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
#pragma once

#include <regex.h>
#include "sbuf.h"

enum RegularExpression {
	RE_BACKSLASH_AT_END = 0,
	RE_COMMENT,
	RE_CONDITIONAL,
	RE_EMPTY_LINE,
	RE_LICENSE_NAME,
	RE_LICENSE_PERMS,
	RE_OPTIONS_HELPER,
	RE_PLIST_FILES,
	RE_PLIST_KEYWORDS,
	RE_STRIP_MODIFIER,
	RE_TARGET_2,
	RE_TARGET,
	RE_USE_QT,
	RE_VAR,
	//RE_VAR_SORT_HACK,
};

int compare_license_perms(struct sbuf *, struct sbuf *);
int compare_use_qt(struct sbuf *, struct sbuf *);
void compile_regular_expressions(void);
int ignore_wrap_col(struct sbuf *);
int indent_goalcol(struct sbuf *);
int leave_unsorted(struct sbuf *);
int matches(enum RegularExpression, struct sbuf *, regmatch_t *);
int print_as_newlines(struct sbuf *);
int skip_goalcol(struct sbuf *);
struct sbuf *strip_modifier(struct sbuf *);
struct sbuf *sub(enum RegularExpression, const char *, struct sbuf *);
